#pragma once
/**
 * TimelinePlayer — Echtzeit-Wiedergabe einer Timeline
 * ====================================================
 *
 * Architektur:
 *
 *   TimelinePlayer
 *   ├── Timeline& (Referenz auf das Datenmodell)
 *   ├── ClipRenderer[] (ein Decoder pro aktivem Clip)
 *   │   ├── DecoderBase  (liest PCM direkt aus der Datei)
 *   │   ├── lastGain     (vorheriger Gain → verhindert Knackser)
 *   │   └── currentTimelineSec
 *   ├── MixThread        (jthread, mischt alle aktiven Clips)
 *   │   ├── für jeden ClipRenderer: DecodeFloat → * Envelope-Gain
 *   │   ├── akkumuliert in mixBuf_[]
 *   │   └── IAudioOutput::WriteBlock
 *   └── IAudioOutput (WaveOutOutput oder NullOutput)
 *
 * Kein Resampling: alle Quell-Dateien sollten dieselbe Sample-Rate
 * wie Timeline::sampleRate haben. Unterschiedliche Raten werden
 * erkannt und eine Warnung ausgegeben; der Clip läuft dann mit
 * falscher Tonhöhe (Resampler-Integration ist als nächster Schritt geplant).
 */

#include "timeline.h"
#include "output/audio_output.h"
#include "decoders/decoder_base.h"

#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <stop_token>

namespace libZFork {

// ── Wiedergabe-Status ─────────────────────────────────────────────────────────

enum class TimelineStatus { Stopped, Playing, Paused };

// ── Callback ──────────────────────────────────────────────────────────────────

/**
 * Wird vom Mix-Thread aufgerufen (nicht vom API-Thread!).
 * param: aktuelle Wiedergabeposition in Sekunden.
 */
using TimelinePositionCallback = std::function<void(double positionSec)>;

// ── TimelinePlayer ────────────────────────────────────────────────────────────

class TimelinePlayer {
public:
    explicit TimelinePlayer(Timeline& timeline,
                            std::unique_ptr<IAudioOutput> output = nullptr);
    ~TimelinePlayer();

    TimelinePlayer(const TimelinePlayer&)            = delete;
    TimelinePlayer& operator=(const TimelinePlayer&) = delete;

    // ── Transport ─────────────────────────────────────────────────────────────

    /** Initialisiert WaveOut und startet den Mix-Thread. */
    bool Start();

    /** Stoppt den Mix-Thread und schließt WaveOut. */
    void Stop();

    bool Play();
    bool Pause();
    bool Resume();

    /** Springt zu einer Position in Sekunden. */
    bool Seek(double seconds);

    // ── Position / Status ─────────────────────────────────────────────────────

    [[nodiscard]] double         GetPosition() const;
    [[nodiscard]] TimelineStatus GetStatus()   const;

    // ── Lautstärke ────────────────────────────────────────────────────────────

    /** Master-Lautstärke 0.0–1.0 */
    void SetMasterGain(float gain);
    [[nodiscard]] float GetMasterGain() const;

    // ── Callback ──────────────────────────────────────────────────────────────

    /**
     * Callback wird periodisch während der Wiedergabe aufgerufen (ca. alle
     * kMixFrames / sampleRate Sekunden = ~23 ms bei 44100 Hz / 1024 Frames).
     */
    void SetPositionCallback(TimelinePositionCallback cb);

    // ── Export ────────────────────────────────────────────────────────────────

    /**
     * Offline-Export der gesamten Timeline in eine WAV-Datei.
     * Gibt Fortschritt als 0.0–1.0 über progressCb zurück.
     * Blockiert den aufrufenden Thread.
     */
    bool ExportWav(const wchar_t* outPath,
                   std::function<void(float progress)> progressCb = nullptr);

    const char* GetError() const;

private:
    // ── Interner Decoder-Zustand pro Clip ────────────────────────────────────

    struct ClipRenderer {
        const Clip*                    clip      = nullptr;
        const TimelineTrack*           track     = nullptr;
        std::unique_ptr<DecoderBase>   decoder;
        double                         decoderPosSec = 0.0; // decoder-interne Position
        float                          lastGain  = 0.0f;    // für Gain-Smoothing
        bool                           eof       = false;
    };

    // ── Mix-Helpers ───────────────────────────────────────────────────────────

    /** Erstellt einen DecoderBase für den angegebenen Clip-Pfad. */
    static std::unique_ptr<DecoderBase> MakeDecoder(const std::wstring& path);

    /**
     * Stellt sicher dass für alle Clips, die im Fenster
     * [positionSec, positionSec + windowSec] aktiv sind,
     * ein ClipRenderer existiert.
     */
    void SyncRenderers(double positionSec);

    /**
     * Mischt alle aktiven ClipRenderer für einen Block von `frames` Frames
     * in mixBuf_. mixBuf_ wird vorher auf 0 gesetzt.
     *
     * @returns true wenn noch Daten vorhanden, false wenn Timeline-Ende.
     */
    bool RenderBlock(uint32_t frames, double& positionSec);

    /** Konvertiert float PCM → int16 mit Clipping. */
    static void FloatToInt16(const float* src, int16_t* dst, uint32_t count);

    // ── Mix-Thread ────────────────────────────────────────────────────────────

    void MixLoop(std::stop_token st);

    // ── Export-Helpers ────────────────────────────────────────────────────────

    bool WriteWavHeader(void* file, uint32_t sampleRate, uint16_t channels,
                        uint32_t dataBytes);

    // ── Zustand ───────────────────────────────────────────────────────────────

    Timeline&                      timeline_;
    std::unique_ptr<IAudioOutput>  output_;
    mutable std::string            error_;

    std::atomic<TimelineStatus>    status_     { TimelineStatus::Stopped };
    std::atomic<double>            position_   { 0.0 };   // Sekunden
    std::atomic<float>             masterGain_ { 1.0f };

    // Mix-Thread
    std::jthread                   mixThread_;
    mutable std::mutex             rendererMtx_;
    std::vector<ClipRenderer>      renderers_;

    // Seek
    std::atomic<bool>              seekPending_  { false };
    std::atomic<double>            seekTarget_   { 0.0 };

    // Callback
    mutable std::mutex             cbMtx_;
    TimelinePositionCallback       posCallback_;

    // PCM-Puffer (float: Mixing-Puffer, int16: Ausgabe-Puffer)
    static constexpr uint32_t kMixFrames = 1024;
    std::vector<float>   mixBuf_;     // interleaved float [kMixFrames * channels]
    std::vector<float>   clipBuf_;    // temporär pro Clip
    std::vector<int16_t> outBuf_;     // int16 für WaveOut
};

} // namespace libZFork
