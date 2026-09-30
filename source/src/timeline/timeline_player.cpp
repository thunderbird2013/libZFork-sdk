#include "timeline/timeline_player.h"
#include "output/audio_output.h"

// Decoder-Includes (spiegelbildlich zu sound.cpp)
#include "decoders/wav_decoder.h"
#include "decoders/pcm_decoder.h"
#include "decoders/aiff_decoder.h"
#ifdef LIBZFORK_WITH_MP3
#  include "decoders/mp3_decoder.h"
#endif
#ifdef LIBZFORK_WITH_OGG
#  include "decoders/ogg_decoder.h"
#endif
#ifdef LIBZFORK_WITH_FLAC
#  include "decoders/flac_decoder.h"
#  include "decoders/flac_ogg_decoder.h"
#endif
#ifdef LIBZFORK_WITH_OPUS
#  include "decoders/opus_decoder.h"
#endif
#ifdef LIBZFORK_WITH_AAC
#  include "decoders/aac_decoder.h"
#endif

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cwctype>
#include <cmath>
#include <filesystem>
#include <format>

// WAV-Header schreiben
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace libZFork {

// ── Konstruktor / Destruktor ───────────────────────────────────────────────────

TimelinePlayer::TimelinePlayer(Timeline& timeline,
                               std::unique_ptr<IAudioOutput> output)
    : timeline_(timeline)
    , output_(output ? std::move(output) : CreatePlatformAudioOutput())
{
    const uint32_t totalSamples = kMixFrames * static_cast<uint32_t>(timeline_.channels);
    mixBuf_.resize(totalSamples, 0.0f);
    clipBuf_.resize(totalSamples, 0.0f);
    outBuf_.resize(totalSamples, 0);
}

TimelinePlayer::~TimelinePlayer() {
    Stop();
}

// ── Decoder-Factory ───────────────────────────────────────────────────────────

std::unique_ptr<DecoderBase> TimelinePlayer::MakeDecoder(const std::wstring& path) {
    // Extension bestimmen
    auto pos = path.rfind(L'.');
    std::wstring ext = (pos != std::wstring::npos) ? path.substr(pos + 1) : L"";
    for (auto& c : ext) c = static_cast<wchar_t>(::towlower(c));

    std::unique_ptr<DecoderBase> dec;

    if (ext == L"wav")                      dec = std::make_unique<WavDecoder>();
    else if (ext == L"pcm" || ext == L"raw") dec = std::make_unique<PcmDecoder>();
    else if (ext == L"aif" || ext == L"aiff") dec = std::make_unique<AiffDecoder>();
#ifdef LIBZFORK_WITH_MP3
    else if (ext == L"mp3")                 dec = std::make_unique<Mp3Decoder>();
#endif
#ifdef LIBZFORK_WITH_OGG
    else if (ext == L"ogg" || ext == L"oga") {
#  ifdef LIBZFORK_WITH_FLAC
        dec = std::make_unique<FlacOggDecoder>();
#  else
        dec = std::make_unique<OggDecoder>();
#  endif
    }
#endif
#ifdef LIBZFORK_WITH_FLAC
    else if (ext == L"flac")                dec = std::make_unique<FlacDecoder>();
#endif
#ifdef LIBZFORK_WITH_OPUS
    else if (ext == L"opus")                dec = std::make_unique<OpusDecoder>();
#endif
#ifdef LIBZFORK_WITH_AAC
    else if (ext == L"aac" || ext == L"m4a") dec = std::make_unique<AacDecoder>();
#endif

    if (!dec) return nullptr;
    if (!dec->OpenFile(path.c_str())) return nullptr;
    return dec;
}

// ── Transport ─────────────────────────────────────────────────────────────────

bool TimelinePlayer::Start() {
    if (status_ != TimelineStatus::Stopped) return true;

    if (!output_->Init(static_cast<uint32_t>(timeline_.sampleRate),
                       static_cast<uint32_t>(timeline_.channels),
                       16, 200)) {
        error_ = std::format("WaveOut Init fehlgeschlagen");
        return false;
    }
    if (!output_->Start()) {
        error_ = std::format("WaveOut Start fehlgeschlagen");
        return false;
    }

    status_ = TimelineStatus::Paused;  // erst Pause, Play() setzt auf Playing
    mixThread_ = std::jthread([this](std::stop_token st){ MixLoop(st); });
    return true;
}

void TimelinePlayer::Stop() {
    if (status_ == TimelineStatus::Stopped) return;
    mixThread_.request_stop();
    mixThread_.join();
    {
        std::lock_guard lock(rendererMtx_);
        renderers_.clear();
    }
    output_->Stop();
    output_->Close();
    status_   = TimelineStatus::Stopped;
    position_ = 0.0;
}

bool TimelinePlayer::Play() {
    if (status_ == TimelineStatus::Stopped) {
        if (!Start()) return false;
    }
    status_ = TimelineStatus::Playing;
    return true;
}

bool TimelinePlayer::Pause() {
    if (status_ != TimelineStatus::Playing) return false;
    status_ = TimelineStatus::Paused;
    return true;
}

bool TimelinePlayer::Resume() {
    if (status_ != TimelineStatus::Paused) return false;
    status_ = TimelineStatus::Playing;
    return true;
}

bool TimelinePlayer::Seek(double seconds) {
    seconds = std::max(0.0, seconds);
    seekTarget_  = seconds;
    seekPending_ = true;
    return true;
}

// ── Position / Status ─────────────────────────────────────────────────────────

double TimelinePlayer::GetPosition() const {
    return position_.load(std::memory_order_relaxed);
}

TimelineStatus TimelinePlayer::GetStatus() const {
    return status_.load(std::memory_order_relaxed);
}

void TimelinePlayer::SetMasterGain(float gain) {
    masterGain_ = std::max(0.0f, gain);
}

float TimelinePlayer::GetMasterGain() const {
    return masterGain_.load(std::memory_order_relaxed);
}

void TimelinePlayer::SetPositionCallback(TimelinePositionCallback cb) {
    std::lock_guard lock(cbMtx_);
    posCallback_ = std::move(cb);
}

// ── SyncRenderers ─────────────────────────────────────────────────────────────

void TimelinePlayer::SyncRenderers(double positionSec) {
    // Lookahead damit Decoder rechtzeitig geöffnet werden
    constexpr double kLookaheadSec = 0.5;

    auto active = timeline_.GetActiveClips(positionSec + kLookaheadSec * 0.5);

    // Neue Clips hinzufügen
    for (const auto& ac : active) {
        bool found = false;
        for (const auto& r : renderers_)
            if (r.clip == ac.clip) { found = true; break; }
        if (found) continue;

        // Decoder öffnen
        auto dec = MakeDecoder(ac.clip->sourcePath);
        if (!dec) continue;  // Datei nicht gefunden → überspringen

        // Sample-Rate-Warnung
        if (dec->SampleRate() != timeline_.sampleRate) {
            std::fprintf(stderr,
                "[TimelinePlayer] Warnung: Clip '%ls' hat %d Hz, Timeline %d Hz — "
                "kein Resampling, Pitch stimmt nicht!\n",
                ac.clip->sourcePath.c_str(), dec->SampleRate(), timeline_.sampleRate);
        }

        // Seek zum Einstiegspunkt im Quell-File
        double seekToSec = ac.clip->sourceOffsetSec;
        // Falls wir bereits innerhalb des Clips sind → zusätzlich den Clip-Offset addieren
        if (positionSec > ac.clip->timelinePosSec)
            seekToSec += positionSec - ac.clip->timelinePosSec;

        StreamTime seekTime{};
        seekTime.ms = static_cast<uint32_t>(seekToSec * 1000.0);
        (void)dec->Seek(SeekOrigin::Begin, seekTime);

        ClipRenderer r;
        r.clip           = ac.clip;
        r.track          = ac.track;
        r.decoder        = std::move(dec);
        r.decoderPosSec  = seekToSec;
        r.lastGain       = 0.0f;
        r.eof            = false;
        renderers_.push_back(std::move(r));
    }

    // Abgelaufene Clips entfernen
    renderers_.erase(
        std::remove_if(renderers_.begin(), renderers_.end(),
            [&active](const ClipRenderer& r) {
                bool stillActive = false;
                for (const auto& ac : active)
                    if (ac.clip == r.clip) { stillActive = true; break; }
                return !stillActive || r.eof;
            }),
        renderers_.end());
}

// ── RenderBlock ───────────────────────────────────────────────────────────────

bool TimelinePlayer::RenderBlock(uint32_t frames, double& positionSec) {
    const uint32_t channels  = static_cast<uint32_t>(timeline_.channels);
    const uint32_t totalSamp = frames * channels;
    const float    secPerBlock = static_cast<float>(frames)
                                 / static_cast<float>(timeline_.sampleRate);

    std::fill(mixBuf_.begin(), mixBuf_.end(), 0.0f);

    bool anyActive = false;

    for (auto& r : renderers_) {
        if (r.eof) continue;

        // Gain am Anfang und Ende des Blocks interpolieren (verhindert Knackser)
        double blockStart = positionSec;
        double blockEnd   = positionSec + secPerBlock;
        float  gainStart  = r.clip->SampleGain(blockStart) * r.track->gain;
        float  gainEnd    = r.clip->SampleGain(blockEnd)   * r.track->gain;

        if (gainStart <= 0.0f && gainEnd <= 0.0f) {
            // Außerhalb des Clips — trotzdem Frames konsumieren damit Decoder-Position stimmt
            // (Clip könnte noch aktiv sein, nur gerade im Fade-Out auf 0)
            r.decoder->DecodeFloat(clipBuf_.data(), frames);
            r.decoderPosSec += secPerBlock;
            continue;
        }

        std::fill(clipBuf_.begin(), clipBuf_.end(), 0.0f);
        uint32_t decoded = r.decoder->DecodeFloat(clipBuf_.data(), frames);
        if (decoded == 0) { r.eof = true; continue; }

        anyActive = true;
        r.decoderPosSec += static_cast<double>(decoded) / timeline_.sampleRate;

        // Per-Frame Gain-Interpolation + Akkumulierung
        for (uint32_t f = 0; f < decoded; ++f) {
            float t    = static_cast<float>(f) / static_cast<float>(frames);
            float gain = gainStart + t * (gainEnd - gainStart);
            for (uint32_t ch = 0; ch < channels; ++ch) {
                mixBuf_[f * channels + ch] += clipBuf_[f * channels + ch] * gain;
            }
        }
        r.lastGain = gainEnd;
    }

    positionSec += secPerBlock;
    return anyActive;
}

// ── FloatToInt16 ──────────────────────────────────────────────────────────────

void TimelinePlayer::FloatToInt16(const float* src, int16_t* dst, uint32_t count) {
    constexpr float kScale = 32767.0f;
    for (uint32_t i = 0; i < count; ++i) {
        float v = std::clamp(src[i] * kScale, -32768.0f, 32767.0f);
        dst[i]  = static_cast<int16_t>(v);
    }
}

// ── MixLoop (Mix-Thread) ──────────────────────────────────────────────────────

void TimelinePlayer::MixLoop(std::stop_token st) {
    const double totalDuration = timeline_.GetDuration();
    double pos = position_.load(std::memory_order_relaxed);

    while (!st.stop_requested()) {

        // Seek-Request verarbeiten
        if (seekPending_.exchange(false)) {
            pos = seekTarget_.load();
            position_.store(pos, std::memory_order_relaxed);
            std::lock_guard lock(rendererMtx_);
            renderers_.clear();  // alle Decoder neu öffnen
        }

        if (status_ != TimelineStatus::Playing) {
            // Pause — Stille ausgeben damit WaveOut nicht blockiert
            std::fill(outBuf_.begin(), outBuf_.end(), int16_t(0));
            output_->WriteBlock(outBuf_.data(), kMixFrames);
            continue;
        }

        // Timeline-Ende?
        if (pos >= totalDuration) {
            status_ = TimelineStatus::Paused;
            pos     = 0.0;
            {
                std::lock_guard lock(rendererMtx_);
                renderers_.clear();
            }
            position_.store(0.0, std::memory_order_relaxed);
            continue;
        }

        // Renderer synchronisieren
        {
            std::lock_guard lock(rendererMtx_);
            SyncRenderers(pos);
            RenderBlock(kMixFrames, pos);
        }

        // Master-Gain anwenden + Float → Int16
        const float mg = masterGain_.load(std::memory_order_relaxed);
        const uint32_t totalSamp = kMixFrames * static_cast<uint32_t>(timeline_.channels);
        if (mg != 1.0f) {
            for (uint32_t i = 0; i < totalSamp; ++i)
                mixBuf_[i] *= mg;
        }
        FloatToInt16(mixBuf_.data(), outBuf_.data(), totalSamp);
        output_->WriteBlock(outBuf_.data(), kMixFrames);

        position_.store(pos, std::memory_order_relaxed);

        // Position-Callback
        {
            std::lock_guard lock(cbMtx_);
            if (posCallback_) posCallback_(pos);
        }
    }
}

// ── WAV-Export ────────────────────────────────────────────────────────────────

bool TimelinePlayer::ExportWav(const wchar_t* outPath,
                               std::function<void(float)> progressCb) {
    // Export läuft offline — Timeline muss nicht spielen
    if (status_ == TimelineStatus::Playing) {
        error_ = "ExportWav: Stoppt zuerst die Wiedergabe";
        return false;
    }

    FILE* f = nullptr;
#ifdef _WIN32
    _wfopen_s(&f, outPath, L"wb");
#else
    if (outPath) {
        const auto native = std::filesystem::path(outPath).string();
        f = std::fopen(native.c_str(), "wb");
    }
#endif
    if (!f) {
        error_ = "ExportWav: Datei konnte nicht geöffnet werden";
        return false;
    }

    const uint32_t sr       = static_cast<uint32_t>(timeline_.sampleRate);
    const uint16_t ch       = static_cast<uint16_t>(timeline_.channels);
    const double   duration = timeline_.GetDuration();
    const uint32_t totalFrames = static_cast<uint32_t>(duration * sr);

    // WAV-Header Platzhalter schreiben (wird am Ende aktualisiert)
    struct WavHdr {
        char     riff[4]        = { 'R','I','F','F' };
        uint32_t fileSize       = 0;
        char     wave[4]        = { 'W','A','V','E' };
        char     fmt[4]         = { 'f','m','t',' ' };
        uint32_t fmtSize        = 16;
        uint16_t audioFmt       = 1;   // PCM
        uint16_t channels       = 2;
        uint32_t sampleRate     = 44100;
        uint32_t byteRate       = 0;
        uint16_t blockAlign     = 0;
        uint16_t bitsPerSample  = 16;
        char     data[4]        = { 'd','a','t','a' };
        uint32_t dataSize       = 0;
    } hdr;

    hdr.channels       = ch;
    hdr.sampleRate     = sr;
    hdr.bitsPerSample  = 16;
    hdr.blockAlign     = static_cast<uint16_t>(ch * 2);
    hdr.byteRate       = sr * ch * 2;

    std::fwrite(&hdr, sizeof(hdr), 1, f);

    // Renderer für alle Clips aufbauen (ab Position 0)
    std::vector<ClipRenderer> exportRenderers;
    double pos = 0.0;
    uint32_t framesWritten = 0;

    const uint32_t kBlockFrames = kMixFrames;
    const uint32_t kBlockSamps  = kBlockFrames * ch;
    std::vector<float>   exportMix(kBlockSamps, 0.0f);
    std::vector<float>   exportClip(kBlockSamps, 0.0f);
    std::vector<int16_t> exportOut(kBlockSamps, 0);

    uint32_t dataBytes = 0;

    while (framesWritten < totalFrames) {
        uint32_t framesToRender = std::min(kBlockFrames, totalFrames - framesWritten);

        // Renderer synchronisieren
        {
            auto active = timeline_.GetActiveClips(pos + 0.25);
            // Neue Clips hinzufügen
            for (const auto& ac : active) {
                bool found = false;
                for (auto& r : exportRenderers)
                    if (r.clip == ac.clip) { found = true; break; }
                if (found) continue;

                auto dec = MakeDecoder(ac.clip->sourcePath);
                if (!dec) continue;

                double seekToSec = ac.clip->sourceOffsetSec;
                if (pos > ac.clip->timelinePosSec)
                    seekToSec += pos - ac.clip->timelinePosSec;

                StreamTime st2{};
                st2.ms = static_cast<uint32_t>(seekToSec * 1000.0);
                (void)dec->Seek(SeekOrigin::Begin, st2);

                ClipRenderer r;
                r.clip          = ac.clip;
                r.track         = ac.track;
                r.decoder       = std::move(dec);
                r.decoderPosSec = seekToSec;
                r.lastGain      = 0.0f;
                r.eof           = false;
                exportRenderers.push_back(std::move(r));
            }
            // Abgelaufene entfernen
            exportRenderers.erase(
                std::remove_if(exportRenderers.begin(), exportRenderers.end(),
                    [&active](const ClipRenderer& r) {
                        for (const auto& ac : active)
                            if (ac.clip == r.clip) return false;
                        return true;
                    }),
                exportRenderers.end());
        }

        // Block mixen
        const float secPerBlock = static_cast<float>(framesToRender) / static_cast<float>(sr);
        std::fill(exportMix.begin(), exportMix.end(), 0.0f);

        for (auto& r : exportRenderers) {
            if (r.eof) continue;
            float gainStart = r.clip->SampleGain(pos)               * r.track->gain;
            float gainEnd   = r.clip->SampleGain(pos + secPerBlock) * r.track->gain;

            std::fill(exportClip.begin(), exportClip.end(), 0.0f);
            uint32_t decoded = r.decoder->DecodeFloat(exportClip.data(), framesToRender);
            if (decoded == 0) { r.eof = true; continue; }

            for (uint32_t frame = 0; frame < decoded; ++frame) {
                float t    = static_cast<float>(frame) / static_cast<float>(framesToRender);
                float gain = gainStart + t * (gainEnd - gainStart);
                for (uint32_t c = 0; c < ch; ++c)
                    exportMix[frame * ch + c] += exportClip[frame * ch + c] * gain;
            }
        }

        FloatToInt16(exportMix.data(), exportOut.data(), framesToRender * ch);
        uint32_t bytesOut = framesToRender * ch * 2;
        std::fwrite(exportOut.data(), 1, bytesOut, f);
        dataBytes    += bytesOut;
        framesWritten += framesToRender;
        pos           += secPerBlock;

        if (progressCb)
            progressCb(static_cast<float>(framesWritten) / static_cast<float>(totalFrames));
    }

    // WAV-Header mit echten Größen aktualisieren
    hdr.dataSize = dataBytes;
    hdr.fileSize = 36 + dataBytes;
    std::fseek(f, 0, SEEK_SET);
    std::fwrite(&hdr, sizeof(hdr), 1, f);
    std::fclose(f);
    return true;
}

const char* TimelinePlayer::GetError() const {
    return error_.c_str();
}

} // namespace libZFork
