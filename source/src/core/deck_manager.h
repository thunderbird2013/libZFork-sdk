#pragma once
/**
 * DeckManager — Multi-Player Manager für DJ / Radio Anwendungen
 * =============================================================
 * Verwaltet mehrere Player-Instanzen (Deck A, B, C, …) mit einer
 * einheitlichen API für Transport, Lautstärke, Crossfader, Pitch,
 * Tempo, EQ, Echo und BPM-Synchronisation.
 *
 * Grundkonzept:
 *   DeckManager mgr;
 *   int a = mgr.CreateDeck("A");
 *   int b = mgr.CreateDeck("B");
 *
 *   mgr.LoadTrack(a, L"track1.mp3");
 *   mgr.LoadTrack(b, L"track2.mp3");
 *   mgr.Play(a);
 *
 *   // Crossfade von A nach B in 4 Sekunden
 *   mgr.Crossfade(a, b, 4000);
 */

#include <libzfork/libzfork.h>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace libZFork {

class Player;

// ── DeckInfo — Snapshot des aktuellen Deck-Zustands ──────────────────────────

struct DeckInfo {
    int          id          = -1;
    std::wstring name;
    std::wstring trackPath;
    PlayerStatus status      = PlayerStatus::Stopped;
    float        positionSec = 0.0f;   // aktuelle Wiedergabeposition
    float        durationSec = 0.0f;   // Gesamtlänge des Tracks
    float        bpm         = 0.0f;   // erkannter oder gesetzter BPM-Wert
    int          volumeLeft  = 100;    // 0–100
    int          volumeRight = 100;    // 0–100
    float        pitchSemi   = 0.0f;   // Semitöne relativ zum Original
    float        tempoPercent= 0.0f;   // Tempo-Verschiebung in Prozent (-50…+50)
};

// ── DeckManager ───────────────────────────────────────────────────────────────

class DeckManager {
public:
    DeckManager();
    ~DeckManager();

    // Nicht kopierbar, aber verschiebbar
    DeckManager(const DeckManager&)            = delete;
    DeckManager& operator=(const DeckManager&) = delete;
    DeckManager(DeckManager&&)                 = default;
    DeckManager& operator=(DeckManager&&)      = default;

    // ── Deck-Verwaltung ───────────────────────────────────────────────────────

    /** Erstellt ein neues Deck. Gibt die Deck-ID zurück, oder -1 bei Fehler. */
    int  CreateDeck(std::string_view name = "");

    /** Löscht ein Deck (stoppt Wiedergabe, gibt Player frei). */
    bool DestroyDeck(int deckId);

    /** Anzahl der aktuell vorhandenen Decks. */
    int  GetDeckCount() const;

    /** Gibt alle Deck-IDs zurück. */
    std::vector<int> GetDeckIds() const;

    /** Gibt den aktuellen Snapshot-Zustand eines Decks zurück. */
    bool GetDeckInfo(int deckId, DeckInfo& out) const;

    // ── Track laden ───────────────────────────────────────────────────────────

    /** Lädt einen Track (Unicode-Pfad, Format wird automatisch erkannt). */
    bool LoadTrack(int deckId, const wchar_t* path,
                   StreamFormat format = StreamFormat::Autodetect);

    /** Lädt einen Track (ANSI/UTF-8-Pfad). */
    bool LoadTrackA(int deckId, const char* path,
                    StreamFormat format = StreamFormat::Autodetect);

    /** Gibt den Pfad des aktuell geladenen Tracks zurück. */
    std::wstring GetTrackPath(int deckId) const;

    // ── Transport ─────────────────────────────────────────────────────────────

    bool Play  (int deckId);
    bool Pause (int deckId);
    bool Resume(int deckId);
    bool Stop  (int deckId);

    /** Springt zu einer Position in Sekunden. */
    bool Seek(int deckId, float seconds);

    /** Aktiviert Loop-Wiedergabe. count=0 → kein Loop, 0xFFFFFFFF → endlos. */
    bool SetLoop(int deckId, uint32_t count);

    // ── Position / Status ─────────────────────────────────────────────────────

    float        GetPosition(int deckId) const;   // aktuelle Position in Sekunden
    float        GetDuration(int deckId) const;   // Gesamtlänge in Sekunden
    PlayerStatus GetStatus  (int deckId) const;

    // ── Lautstärke ────────────────────────────────────────────────────────────

    /** Setzt die Deck-Lautstärke (0–100 je Kanal). */
    bool SetVolume(int deckId, int left, int right);
    bool GetVolume(int deckId, int& outLeft, int& outRight) const;

    /**
     * Crossfader zwischen zwei Decks.
     * position: 0.0 → Deck A voll, 1.0 → Deck B voll
     * Nutzt konstant-Leistungs-Kurve (cos/sin).
     */
    bool SetCrossfader(int deckA, int deckB, float position);

    /**
     * Weicher Übergang: blendet deckFrom aus, deckTo ein.
     * durationMs: Dauer des Crossfades in Millisekunden.
     * deckTo wird automatisch gestartet wenn es noch stoppt.
     */
    bool Crossfade(int deckFrom, int deckTo, uint32_t durationMs);

    // ── Pitch / Tempo / Rate (SoundTouch) ─────────────────────────────────────

    /** Verschiebt die Tonhöhe um Semitöne (+12 = Oktave hoch, -12 = Oktave tief). */
    bool SetPitch(int deckId, float semitones);

    /** Ändert das Tempo ohne Pitch-Shift. percent: -50 bis +50. */
    bool SetTempo(int deckId, float percent);

    /** Ändert Rate (Pitch + Tempo zusammen). */
    bool SetRate(int deckId, float rate);

    // ── BPM ───────────────────────────────────────────────────────────────────

    /** Analysiert den geladenen Track und gibt den erkannten BPM zurück. */
    bool DetectBPM(int deckId, float& outBpm);

    /** Setzt den BPM-Wert manuell (ohne Analyse). */
    bool SetBPM(int deckId, float bpm);

    float GetBPM(int deckId) const;

    /**
     * Passt das Tempo von deckSlave an, damit es mit deckMaster synchron läuft.
     * Beide BPM-Werte müssen bekannt sein (DetectBPM oder SetBPM).
     */
    bool SyncBPM(int deckMaster, int deckSlave);

    // ── Equalizer (10-Band) ───────────────────────────────────────────────────

    bool EnableEQ(int deckId, bool enable);

    /** band: 0–9, gainDb: typisch -12.0 bis +12.0 */
    bool SetEQBand(int deckId, int band, float gainDb);
    bool GetEQBand(int deckId, int band, float& outGainDb) const;
    bool ResetEQ  (int deckId);

    // ── Echo / Hall ───────────────────────────────────────────────────────────

    bool EnableEcho   (int deckId, bool enable);
    bool SetEchoParams(int deckId, const EchoParams& params);
    bool GetEchoParams(int deckId, EchoParams& outParams) const;

    // ── Analyse ───────────────────────────────────────────────────────────────

    bool GetVU (int deckId, VUData& outVU) const;
    bool GetFFT(int deckId, float* outLeft, float* outRight, uint32_t points) const;

    // ── Track-Info / ID3-Tags ─────────────────────────────────────────────────

    bool GetStreamInfo(int deckId, StreamInfo& outInfo) const;
    bool GetID3       (int deckId, ID3Info&    outInfo) const;
    bool GetID3Ex     (int deckId, ID3InfoEx&  outInfo) const;

    // ── Callback ──────────────────────────────────────────────────────────────

    /** Setzt einen Player-Callback für das angegebene Deck. */
    bool SetCallback(int deckId, PlayerCallback callback, void* userData);

    // ── Soundkarte ────────────────────────────────────────────────────────────

    /** Weist einem Deck ein bestimmtes Ausgabegerät zu. */
    bool SetDevice(int deckId, int deviceId);

    /** Listet verfügbare Ausgabegeräte auf (alle Decks nutzen dieselbe Liste). */
    int  EnumerateDevices() const;
    bool GetDeviceInfo(int deviceId, WaveDeviceInfo& outInfo) const;

    // ── Direktzugriff auf den Player ─────────────────────────────────────────

    /**
     * Gibt einen Rohzeiger auf den internen Player des Decks zurück.
     * Nützlich für erweiterte Operationen die DeckManager nicht direkt abdeckt.
     * Der Zeiger ist nur solange gültig wie das Deck existiert.
     */
    Player* GetPlayer(int deckId);
    const Player* GetPlayer(int deckId) const;

    // ── Fehlerbehandlung ──────────────────────────────────────────────────────

    const char* GetError() const;

private:
    struct DeckEntry {
        int                      id;
        std::string              name;
        std::wstring             trackPath;
        float                    bpm          = 0.0f;
        float                    pitchSemi    = 0.0f;
        float                    tempoPercent = 0.0f;
        int                      volumeLeft   = 100;
        int                      volumeRight  = 100;
        std::unique_ptr<Player>  player;
    };

    DeckEntry*       FindDeck(int id);
    const DeckEntry* FindDeck(int id) const;
    void             SetError(std::string_view msg) const;

    mutable std::mutex               mtx_;
    std::vector<DeckEntry>           decks_;
    int                              nextId_ = 1;
    mutable std::string              error_;
};

} // namespace libZFork
