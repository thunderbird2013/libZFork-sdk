#include "player.h"
#ifdef _WIN32
#  include <windows.h>  // MultiByteToWideChar, Sleep, waveIn* functions
#  include <mmsystem.h>
#  include "output/auto_output.h"
#endif
#ifdef LIBZFORK_WITH_PORTAUDIO
#  include <portaudio.h>
#endif
#include "decoders/decoder_base.h"
#include "decoders/mp3_decoder.h"
#include "decoders/ogg_decoder.h"
#include "decoders/flac_decoder.h"
#include "decoders/wav_decoder.h"
#include "decoders/aac_decoder.h"
#include "decoders/m4a_decoder.h"
#include "decoders/pcm_decoder.h"
#include "decoders/aiff_decoder.h"
#include "decoders/cda_decoder.h"
#include "decoders/flac_ogg_decoder.h"
#include "decoders/decoder_factory.h"
#ifdef LIBZFORK_WITH_OPUS
#  include "decoders/opus_decoder.h"
#endif
#include "encoders/encoder_base.h"
#include "encoders/mp3_encoder.h"
#include "encoders/ogg_encoder.h"
#include "encoders/flac_encoder.h"
#include "encoders/wav_encoder.h"
#include "effects/equalizer.h"
#include "effects/echo.h"
#include "effects/soundtouch_proc.h"
#include "effects/center_cut.h"
#include "effects/stereo_enhancer.h"
#include "analysis/spectrum.h"
#ifdef LIBZFORK_WITH_BROADCAST
#  include "streaming/broadcast_transport.h"
#  include "streaming/mp3_broadcast_encoder.h"
#endif
#include "utils/tags.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <format>
#include <locale>
#include <ranges>
#include <sstream>
#include <string_view>
#include <thread>
#include <codecvt>
#include <utility>

namespace libZFork {

namespace fs = std::filesystem;
namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime  = 1099511628211ull;

static inline uint64_t Fnv1aAppend(uint64_t h, const void* data, size_t len) noexcept {
    auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<uint64_t>(p[i]);
        h *= kFnvPrime;
    }
    return h;
}

static inline uint64_t HashWStringLower(std::wstring_view s) noexcept {
    uint64_t h = kFnvOffset;
    for (wchar_t c : s) {
        const wchar_t lower = static_cast<wchar_t>(std::towlower(c));
        h = Fnv1aAppend(h, &lower, sizeof(lower));
    }
    return h;
}

template<typename T>
static inline uint64_t HashValue(uint64_t h, const T& v) noexcept {
    return Fnv1aAppend(h, &v, sizeof(v));
}

static inline uint8_t PeakToByte(float x) noexcept {
    x = std::clamp(x, 0.0f, 1.0f);
    // leichte Expansion leiser Peaks, ohne alles flach zu machen
    x = std::pow(x, 0.65f);
    return static_cast<uint8_t>(x * 255.0f + 0.5f);
}

static size_t CountCsvBins(std::string_view csv) {
    if (csv.empty()) return 0;
    // optional "v4," / "v3," Prefix ueberspringen
    if (csv.size() >= 3 && csv[0] == 'v' && (csv[1] == '3' || csv[1] == '4') && csv[2] == ',')
        csv.remove_prefix(3);
    if (csv.empty()) return 0;
    size_t n = 1;
    for (char c : csv) if (c == ',') ++n;
    return n;
}

static bool NeedsWaveformRebuild(std::string_view csv) {
    // v4 = 131072 Time-Bins × 3 Baender — Zoom braucht Dichte, sonst Karton-Bloecke
    if (csv.size() < 3 || !(csv[0] == 'v' && csv[1] == '4' && csv[2] == ','))
        return true;
    const size_t n = CountCsvBins(csv);
    return (n % 3 != 0) || (n / 3 < 65536);
}

/** Full-file 3-band peak overview: Kick/Mid/High interleaved (3 bytes / time-bin).
 *  Layout: [K0,M0,H0, K1,M1,H1, ...] — Virtual-DJ-Style Frequency-Coloring. */
static std::string BuildWaveformPreview(DecoderBase& dec) {
    constexpr uint32_t kTargetBins = 131072;
    constexpr uint32_t kChunk = 4096;
    const int ch = std::max(1, dec.Channels());
    const int sr = std::max(1, dec.SampleRate());
    const uint32_t lengthMs = dec.GetLength().ms;
    const uint64_t totalFrames = (lengthMs > 0)
        ? (static_cast<uint64_t>(lengthMs) * static_cast<uint64_t>(sr) / 1000ull)
        : 0ull;

    // One-pole LP ~ bass; residual = mid+high; HP tip = highs
    const float lpAlpha = 0.92f;

    std::vector<float> buf(static_cast<size_t>(kChunk) * static_cast<size_t>(ch));
    std::vector<uint8_t> bins;
    bins.reserve(static_cast<size_t>(kTargetBins) * 3ull);

    auto flushBin = [&](float pkKick, float pkMid, float pkHigh) {
        bins.push_back(PeakToByte(pkKick));
        bins.push_back(PeakToByte(pkMid));
        bins.push_back(PeakToByte(pkHigh));
    };

    if (totalFrames > 0) {
        const uint64_t framesPerBin =
            std::max<uint64_t>(1ull, (totalFrames + kTargetBins - 1ull) / kTargetBins);
        uint64_t frameInBin = 0;
        float pkK = 0.f, pkM = 0.f, pkH = 0.f;
        float lp = 0.f;
        uint32_t binIdx = 0;

        while (binIdx < kTargetBins) {
            const uint32_t got = dec.DecodeFloat(buf.data(), kChunk);
            if (got == 0) break;
            for (uint32_t i = 0; i < got; ++i) {
                float l = buf[static_cast<size_t>(i) * ch];
                float r = (ch > 1) ? buf[static_cast<size_t>(i) * ch + 1] : l;
                float s = 0.5f * (l + r);
                lp = lpAlpha * lp + (1.f - lpAlpha) * s;
                float bass = lp;
                float hp = s - lp;
                // Mid = Rest ohne starken Bass/High-Anteil → farbliche Trennung
                float mid = s - 0.70f * bass - 0.50f * hp;
                pkK = std::max(pkK, std::abs(bass) * 1.15f);
                pkM = std::max(pkM, std::abs(mid) * 1.10f);
                pkH = std::max(pkH, std::abs(hp) * 1.35f);
                ++frameInBin;
                if (frameInBin >= framesPerBin) {
                    flushBin(pkK, pkM, pkH);
                    pkK = pkM = pkH = 0.f;
                    frameInBin = 0;
                    ++binIdx;
                    if (binIdx >= kTargetBins) break;
                }
            }
            if (got < kChunk) break;
        }
        if (binIdx < kTargetBins) {
            flushBin(pkK, pkM, pkH);
            ++binIdx;
            while (binIdx < kTargetBins) {
                flushBin(0.f, 0.f, 0.f);
                ++binIdx;
            }
        }
    } else {
        struct Trip { float k, m, h; };
        std::vector<Trip> peaks;
        peaks.reserve(8192);
        float lp = 0.f;
        while (true) {
            const uint32_t got = dec.DecodeFloat(buf.data(), kChunk);
            if (got == 0) break;
            float pkK = 0.f, pkM = 0.f, pkH = 0.f;
            for (uint32_t i = 0; i < got; ++i) {
                float l = buf[static_cast<size_t>(i) * ch];
                float r = (ch > 1) ? buf[static_cast<size_t>(i) * ch + 1] : l;
                float s = 0.5f * (l + r);
                lp = lpAlpha * lp + (1.f - lpAlpha) * s;
                float bass = lp;
                float hp = s - lp;
                // Mid = Rest ohne starken Bass/High-Anteil → farbliche Trennung
                float mid = s - 0.70f * bass - 0.50f * hp;
                pkK = std::max(pkK, std::abs(bass) * 1.15f);
                pkM = std::max(pkM, std::abs(mid) * 1.10f);
                pkH = std::max(pkH, std::abs(hp) * 1.35f);
            }
            peaks.push_back({pkK, pkM, pkH});
        }
        for (uint32_t b = 0; b < kTargetBins; ++b) {
            const size_t i0 = static_cast<size_t>(b) * peaks.size() / kTargetBins;
            const size_t i1 = static_cast<size_t>(b + 1) * peaks.size() / kTargetBins;
            float pkK = 0.f, pkM = 0.f, pkH = 0.f;
            for (size_t i = i0; i < i1 && i < peaks.size(); ++i) {
                pkK = std::max(pkK, peaks[i].k);
                pkM = std::max(pkM, peaks[i].m);
                pkH = std::max(pkH, peaks[i].h);
            }
            flushBin(pkK, pkM, pkH);
        }
    }

    std::ostringstream oss;
    oss << "v4,";
    for (size_t i = 0; i < bins.size(); ++i) {
        if (i) oss << ',';
        oss << static_cast<int>(bins[i]);
    }
    return oss.str();
}

static std::string WideToUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
    return conv.to_bytes(w.data(), w.data() + w.size());
}

static std::wstring Utf8ToWide(std::string_view s) {
    if (s.empty()) return {};
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
    return conv.from_bytes(s.data(), s.data() + s.size());
}

static bool IsLikelyShoutcast(const BroadcastConfig& cfg) {
    return cfg.serverType == BroadcastServerType::Shoutcast;
}

static bool IsSupportedMp3Bitrate(int kbps) {
    if (kbps == 0) return true; // 0 keeps legacy VBR auto mode.
    switch (kbps) {
    case 32: case 40: case 48: case 56: case 64:
    case 80: case 96: case 112: case 128: case 160:
    case 192: case 224: case 256: case 320:
        return true;
    default:
        return false;
    }
}

static bool IsValidStereoMode(BroadcastStereoMode mode) {
    switch (mode) {
    case BroadcastStereoMode::Auto:
    case BroadcastStereoMode::Stereo:
    case BroadcastStereoMode::JointStereo:
    case BroadcastStereoMode::Mono:
        return true;
    default:
        return false;
    }
}

static bool IsValidBroadcastConfig(const BroadcastConfig& cfg) {
    return cfg.host[0] != '\0' && cfg.password[0] != '\0' && cfg.port > 0 && cfg.sampleRate > 0 &&
           (cfg.channels == 1 || cfg.channels == 2) &&
           IsSupportedMp3Bitrate(cfg.bitrateKbps) &&
           IsValidStereoMode(cfg.stereoMode);
}

static std::string TrimAscii(std::string s) {
    auto isWs = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && isWs(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && isWs(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

static std::string ToLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static std::wstring NormalizePlaylistPathForOs(std::wstring path) {
    if (path.empty()) return path;
#ifdef _WIN32
    if (path.rfind(L"\\\\?\\", 0) == 0) return path;
    // Keep URL-like strings untouched.
    if (path.find(L"://") != std::wstring::npos) return path;
    // Long path prefix for absolute Windows paths.
    if (path.size() >= 240) {
        if (path.rfind(L"\\\\", 0) == 0) {
            return L"\\\\?\\UNC\\" + path.substr(2);
        }
        if (path.size() >= 2 && path[1] == L':') {
            return L"\\\\?\\" + path;
        }
    }
#endif
    return path;
}

static std::wstring ResolvePlaylistEntry(const fs::path& playlistPath, std::string rawEntryUtf8) {
    rawEntryUtf8 = TrimAscii(std::move(rawEntryUtf8));
    if (rawEntryUtf8.empty()) return {};
    if (rawEntryUtf8.size() >= 2 && rawEntryUtf8.front() == '"' && rawEntryUtf8.back() == '"') {
        rawEntryUtf8 = rawEntryUtf8.substr(1, rawEntryUtf8.size() - 2);
    }
    const std::string lower = ToLowerAscii(rawEntryUtf8);
    if (lower.rfind("file://", 0) == 0) {
        rawEntryUtf8 = rawEntryUtf8.substr(7);
        if (!rawEntryUtf8.empty() && rawEntryUtf8.front() == '/') rawEntryUtf8.erase(rawEntryUtf8.begin());
    } else if (lower.find("://") != std::string::npos) {
        // Keep URL-like entries as-is (decoder may or may not support them).
        return Utf8ToWide(rawEntryUtf8);
    }
    std::wstring asWide = Utf8ToWide(rawEntryUtf8);
    if (asWide.empty()) return {};
    fs::path entryPath(asWide);
    if (entryPath.is_absolute()) return NormalizePlaylistPathForOs(entryPath.lexically_normal().wstring());
    return NormalizePlaylistPathForOs((playlistPath.parent_path() / entryPath).lexically_normal().wstring());
}

static std::vector<std::wstring> ParseM3uPlaylist(const fs::path& playlistPath, const std::vector<std::string>& lines) {
    std::vector<std::wstring> out;
    out.reserve(lines.size());
    for (const auto& ln : lines) {
        std::string line = TrimAscii(ln);
        if (line.empty()) continue;
        if (line.front() == '#') continue;
        std::wstring resolved = ResolvePlaylistEntry(playlistPath, line);
        if (!resolved.empty()) out.push_back(std::move(resolved));
    }
    return out;
}

static std::vector<std::wstring> ParsePlsPlaylist(const fs::path& playlistPath, const std::vector<std::string>& lines) {
    struct EntryItem {
        int index = -1;
        size_t seq = 0;
        std::wstring path;
    };
    std::vector<EntryItem> items;
    size_t seq = 0;
    for (const auto& ln : lines) {
        std::string line = TrimAscii(ln);
        if (line.empty()) continue;
        if (line.front() == ';' || line.front() == '[') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = ToLowerAscii(TrimAscii(line.substr(0, eq)));
        std::string value = TrimAscii(line.substr(eq + 1));
        if (key.rfind("file", 0) != 0 || key.size() <= 4) continue;
        std::wstring resolved = ResolvePlaylistEntry(playlistPath, value);
        if (resolved.empty()) continue;
        int idx = -1;
        const std::string suffix = key.substr(4);
        if (!suffix.empty() && std::all_of(suffix.begin(), suffix.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
            idx = std::atoi(suffix.c_str());
        }
        items.push_back({idx, seq++, std::move(resolved)});
    }

    std::stable_sort(items.begin(), items.end(), [](const EntryItem& a, const EntryItem& b) {
        if (a.index >= 0 && b.index >= 0) return a.index < b.index;
        if (a.index >= 0) return true;
        if (b.index >= 0) return false;
        return a.seq < b.seq;
    });

    std::vector<std::wstring> out;
    out.reserve(items.size());
    for (auto& it : items) out.push_back(std::move(it.path));
    return out;
}

static float DbToLinear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

static bool IsValidDuckingConfig(const BroadcastDuckingConfig& cfg) {
    return cfg.attackMs > 0 && cfg.releaseMs > 0 &&
           cfg.duckDb <= 0.0f && cfg.duckDb >= -60.0f &&
           cfg.threshold >= 0.0f && cfg.threshold <= 1.0f;
}

} // namespace

struct Player::AnalysisHandleData {
    uint64_t trackId = 0;
    uint32_t flags = 0;
    float bpm = 0.0f;
    uint32_t lengthMs = 0;
    std::vector<uint8_t> waveformBins;
    std::vector<std::pair<float, float>> peaks;
};

// ── Constructor / Destructor ───────────────────────────────────────────────────

Player::Player()
    : Player(CreatePlatformAudioOutput())
{}

bool Player::SetOutputBackendPreference(int backend) {
#ifdef _WIN32
    auto* autoOut = dynamic_cast<::libZFork::AutoOutput*>(output_.get());
    if (!autoOut) return false;
    autoOut->SetBackendPreference(static_cast<::libZFork::AudioBackendPreference>(backend));
    return true;
#else
    (void)backend;
    return false;
#endif
}

Player::Player(std::unique_ptr<IAudioOutput> output)
    : pcmQueue_(1024 * 1024 * 8) // 8 MB PCM ring buffer
    , output_(std::move(output))
    , equalizer_(std::make_unique<Equalizer>())
    , echo_(std::make_unique<EchoEffect>())
    , soundTouch_(std::make_unique<SoundTouchProc>())
    , centerCut_(std::make_unique<CenterCutProc>())
    , stereoEnhancer_(std::make_unique<StereoEnhancer>())
    , spectrum_(std::make_unique<SpectrumAnalyzer>())
{
    std::memset(&broadcastConfig_, 0, sizeof(broadcastConfig_));
    std::strncpy(broadcastConfig_.mount, "/stream", sizeof(broadcastConfig_.mount) - 1);
    std::strncpy(broadcastConfig_.user, "source", sizeof(broadcastConfig_.user) - 1);
    std::strncpy(broadcastConfig_.streamName, "libZFork Stream", sizeof(broadcastConfig_.streamName) - 1);
    broadcastConfig_.port = 8000;
    broadcastConfig_.bitrateKbps = 192;
    broadcastConfig_.sampleRate = 44100;
    broadcastConfig_.channels = 2;
    broadcastConfig_.stereoMode = BroadcastStereoMode::JointStereo;
    broadcastConfig_.serverType = BroadcastServerType::Icecast;
    broadcastConfig_.autoReconnect = true;
    broadcastConfig_.reconnectDelayMs = 2000;
    LoadTrackCache();
}

Player::~Player() {
    StopBroadcast();
    SaveTrackCache();
    Close();
}

// ── Error handling ─────────────────────────────────────────────────────────────

void Player::SetError(std::string_view msg) {
    std::lock_guard lock(stateMtx_);
    errorMsg_ = msg;
}

const char* Player::GetError() const {
    std::lock_guard lock(stateMtx_);
    return errorMsg_.c_str();
}

// ── Format detection ───────────────────────────────────────────────────────────

StreamFormat Player::DetectFormat(const wchar_t* path) {
    if (!path) return StreamFormat::Unknown;
    std::wstring ext = fs::path(path).extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(tolower(c));
    if (ext == L".mp3")                 return StreamFormat::Mp3;
    if (ext == L".ogg")                 return StreamFormat::Ogg;
    if (ext == L".wav" || ext == L".wave") return StreamFormat::Wav;
    if (ext == L".flac")                return StreamFormat::FLAC;
    if (ext == L".aac")                      return StreamFormat::AacADTS;
    if (ext == L".m4a" || ext == L".mp4")    return StreamFormat::M4a;
    if (ext == L".pcm" || ext == L".raw")    return StreamFormat::PCM;
    if (ext == L".aif" || ext == L".aiff")   return StreamFormat::Aiff;
    if (ext == L".oga")                      return StreamFormat::FLACOgg;
    if (ext == L".opus")                     return StreamFormat::Opus;
    if (ext == L".cda")                      return StreamFormat::CDAudio;
    return StreamFormat::Unknown;
}

StreamFormat Player::DetectFormatFromMemory(const void* data, uint32_t size) {
    if (!data || size < 4) return StreamFormat::Unknown;
    auto* b = static_cast<const uint8_t*>(data);
    // MP3 sync word
    if ((b[0] == 0xFF) && ((b[1] & 0xE0) == 0xE0)) return StreamFormat::Mp3;
    // OGG
    if (b[0]=='O' && b[1]=='g' && b[2]=='g' && b[3]=='S') return StreamFormat::Ogg;
    // RIFF WAV
    if (b[0]=='R' && b[1]=='I' && b[2]=='F' && b[3]=='F') return StreamFormat::Wav;
    // FLAC
    if (b[0]=='f' && b[1]=='L' && b[2]=='a' && b[3]=='C') return StreamFormat::FLAC;
    // AIFF / AIFC — "FORM" header (type check needs 12 bytes)
    if (b[0]=='F' && b[1]=='O' && b[2]=='R' && b[3]=='M' && size >= 12) {
        if (std::memcmp(b + 8, "AIFF", 4) == 0 || std::memcmp(b + 8, "AIFC", 4) == 0)
            return StreamFormat::Aiff;
    }
    // OGG containing FLAC: "OggS" page + FLAC stream prefix "\x7FFLAC"
    if (b[0]=='O' && b[1]=='g' && b[2]=='g' && b[3]=='S' && size >= 33) {
        if (b[28] == 0x7F && b[29]=='F' && b[30]=='L' && b[31]=='A' && b[32]=='C')
            return StreamFormat::FLACOgg;
    }
    // Opus: "OggS" page with OpusHead at byte 28
    if (b[0]=='O' && b[1]=='g' && b[2]=='g' && b[3]=='S' && size >= 36) {
        if (std::memcmp(b + 28, "OpusHead", 8) == 0)
            return StreamFormat::Opus;
    }
    return StreamFormat::Unknown;
}

uint64_t Player::BuildFileTrackId(const wchar_t* path, StreamFormat format) {
    if (!path || !path[0]) return 0;

    std::error_code ec;
    fs::path p(path);
    fs::path canonical = fs::weakly_canonical(p, ec);
    const std::wstring norm = ec ? p.lexically_normal().wstring() : canonical.wstring();

    uint64_t h = HashWStringLower(norm);
    const auto fmt = static_cast<uint32_t>(format);
    h = HashValue(h, fmt);

    const uint64_t size = fs::file_size(p, ec);
    if (!ec) h = HashValue(h, size);
    ec.clear();

    const auto ts = fs::last_write_time(p, ec).time_since_epoch().count();
    if (!ec) h = HashValue(h, ts);
    return h;
}

uint64_t Player::BuildMemoryTrackId(const void* data, uint32_t size, StreamFormat format) {
    uint64_t h = kFnvOffset;
    constexpr uint64_t kMemMagic = 0x4D454D4F52595F31ull; // "MEMORY_1"
    h = HashValue(h, kMemMagic);
    const auto fmt = static_cast<uint32_t>(format);
    h = HashValue(h, fmt);
    h = HashValue(h, size);

    if (data && size > 0) {
        const uint32_t sampleBytes = std::min<uint32_t>(size, 128u);
        h = Fnv1aAppend(h, data, sampleBytes);
    }
    return h;
}

void Player::RegisterTrackId(uint64_t id) {
    if (!id) return;
    ++trackRefCount_[id];
}

void Player::UnregisterTrackId(uint64_t id) {
    if (!id) return;
    auto it = trackRefCount_.find(id);
    if (it == trackRefCount_.end()) return;
    if (it->second <= 1u) trackRefCount_.erase(it);
    else --it->second;
}

void Player::FireQueueChangedEvent() const {
    if (callback_) {
        callback_(const_cast<Player*>(this), callbackData_,
                  CallbackMessage::QueueChangedAsync,
                  static_cast<uint32_t>(songs_.size()), 0);
    }
}

std::filesystem::path Player::GetCachePath() {
    std::error_code ec;
    auto base = fs::temp_directory_path(ec);
    if (ec || base.empty()) base = fs::current_path(ec);
    return base / "libzfork_track_cache_v1.tsv";
}

std::string Player::SanitizeCacheField(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\t' || c == '\r' || c == '\n') out.push_back(' ');
        else out.push_back(c);
    }
    return out;
}

void Player::LoadTrackCache() {
    std::ifstream in(GetCachePath(), std::ios::binary);
    if (!in) return;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string tok;
        std::array<std::string, 9> cols{};
        size_t idx = 0;
        while (idx < cols.size() && std::getline(ss, tok, '\t')) cols[idx++] = tok;
        if (idx < 8) continue;

        uint64_t id = 0;
        try { id = static_cast<uint64_t>(std::stoull(cols[0])); } catch (...) { continue; }
        if (!id) continue;

        TrackCacheEntry e{};
        e.path = Utf8ToWide(cols[1]);
        e.title = cols[2];
        e.artist = cols[3];
        e.album = cols[4];
        try { e.bpm = std::stof(cols[5]); } catch (...) { e.bpm = 0.0f; }
        try { e.lengthMs = static_cast<uint32_t>(std::stoul(cols[6])); } catch (...) { e.lengthMs = 0; }
        e.waveform = cols[7];
        if (idx >= 9) e.peaks = cols[8];
        trackCache_[id] = std::move(e);
    }
}

void Player::SaveTrackCache() const {
    std::ofstream out(GetCachePath(), std::ios::binary | std::ios::trunc);
    if (!out) return;
    for (const auto& [id, e] : trackCache_) {
        std::string pathUtf8 = WideToUtf8(e.path);
        out << id << '\t'
            << SanitizeCacheField(pathUtf8) << '\t'
            << SanitizeCacheField(e.title) << '\t'
            << SanitizeCacheField(e.artist) << '\t'
            << SanitizeCacheField(e.album) << '\t'
            << e.bpm << '\t'
            << e.lengthMs << '\t'
            << SanitizeCacheField(e.waveform) << '\t'
            << SanitizeCacheField(e.peaks) << '\n';
    }
}

void Player::PrimeTrackCache(const SongEntry& e) {
    if (e.trackId == 0 || e.path.empty()) return;
    auto& c = trackCache_[e.trackId];
    c.path = e.path;

    if (c.title.empty() && c.artist.empty() && c.album.empty()) {
        ID3Info tag{};
        if (ID3Tags::LoadBasic(e.path.c_str(), tag)) {
            c.title = tag.title;
            c.artist = tag.artist;
            c.album = tag.album;
        }
    }

    if (c.lengthMs == 0 || NeedsWaveformRebuild(c.waveform)) {
        auto dec = CreateDecoderForFile(e.path.c_str());
        if (dec && dec->OpenFile(e.path.c_str())) {
            if (c.lengthMs == 0) c.lengthMs = dec->GetLength().ms;
            if (NeedsWaveformRebuild(c.waveform))
                c.waveform = BuildWaveformPreview(*dec);
        }
    }
    if (c.peaks.empty() && !c.waveform.empty() && c.lengthMs > 0) {
        c.peaks = BuildPeaksFromWaveformCsv(c.waveform, c.lengthMs);
    }
}

void Player::UpdateTrackCacheBpm(uint64_t trackId, float bpm) {
    if (!trackId || bpm <= 0.0f) return;
    auto it = trackCache_.find(trackId);
    if (it == trackCache_.end()) return;
    it->second.bpm = bpm;
}

std::vector<uint8_t> Player::ParseWaveformCsv(std::string_view csv) {
    std::vector<uint8_t> out;
    if (csv.size() >= 3 && csv[0] == 'v' && (csv[1] == '3' || csv[1] == '4') && csv[2] == ',')
        csv.remove_prefix(3);
    std::stringstream ss{std::string(csv)};
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (tok.empty()) continue;
        if (tok[0] == 'v' || tok[0] == 'V') continue;
        int v = 0;
        try { v = std::stoi(tok); } catch (...) { continue; }
        out.push_back(static_cast<uint8_t>(std::clamp(v, 0, 255)));
    }
    return out;
}

std::vector<std::pair<float, float>> Player::ParsePeaksCsv(std::string_view csv) {
    std::vector<std::pair<float, float>> out;
    std::stringstream ss{std::string(csv)};
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        auto pos = tok.find(':');
        if (pos == std::string::npos) continue;
        try {
            float t = std::stof(tok.substr(0, pos));
            float v = std::stof(tok.substr(pos + 1));
            out.emplace_back(t, v);
        } catch (...) {}
    }
    return out;
}

std::string Player::BuildPeaksFromWaveformCsv(std::string_view waveformCsv, uint32_t lengthMs) {
    auto bins = ParseWaveformCsv(waveformCsv);
    if (bins.empty() || lengthMs == 0) return {};

    const bool rgb = (bins.size() % 3 == 0) && (bins.size() >= 9);
    const size_t nTime = rgb ? (bins.size() / 3) : bins.size();

    // Kick-Kanal (Index 0) → lokale Maxima als Beat-Kandidaten
    std::vector<std::pair<float, float>> cands;
    cands.reserve(256);
    auto kickAt = [&](size_t i) -> float {
        if (rgb) return bins[i * 3] / 255.0f;
        return bins[i] / 255.0f;
    };

    for (size_t i = 1; i + 1 < nTime; ++i) {
        const float v = kickAt(i);
        if (v < 0.35f) continue;
        if (v >= kickAt(i - 1) && v > kickAt(i + 1)) {
            const float t = (static_cast<float>(i) / std::max<size_t>(1, nTime - 1)) *
                            static_cast<float>(lengthMs);
            cands.emplace_back(t, v);
        }
    }
    // Top 64 stärkste Kicks, zeitlich sortiert
    std::sort(cands.begin(), cands.end(), [](auto a, auto b) { return a.second > b.second; });
    constexpr size_t kMaxPeaks = 64;
    if (cands.size() > kMaxPeaks) cands.resize(kMaxPeaks);
    std::sort(cands.begin(), cands.end(), [](auto a, auto b) { return a.first < b.first; });

    std::ostringstream oss;
    for (size_t i = 0; i < cands.size(); ++i) {
        if (i) oss << ',';
        oss << cands[i].first << ':' << cands[i].second;
    }
    return oss.str();
}

// ── Open / Close ───────────────────────────────────────────────────────────────

bool Player::OpenDecoder(const wchar_t* path, StreamFormat format,
                         const void* memData, uint32_t memSize) {
    if (format == StreamFormat::Autodetect) {
        format = path ? DetectFormat(path) : DetectFormatFromMemory(memData, memSize);
    }

    std::unique_ptr<DecoderBase> dec;
    switch (format) {
#ifdef LIBZFORK_WITH_MP3
        case StreamFormat::Mp3:
            dec = std::make_unique<Mp3Decoder>();
            break;
#endif
#ifdef LIBZFORK_WITH_OGG
        case StreamFormat::Ogg:
            dec = std::make_unique<OggDecoder>();
            break;
#endif
#ifdef LIBZFORK_WITH_FLAC
        case StreamFormat::FLAC:
            dec = std::make_unique<FlacDecoder>();
            break;
        case StreamFormat::FLACOgg:
            dec = std::make_unique<FlacOggDecoder>();
            break;
#endif
#ifdef LIBZFORK_WITH_AAC
        case StreamFormat::AacADTS:
            dec = std::make_unique<AacDecoder>();
            break;
        case StreamFormat::M4a:
            dec = std::make_unique<M4aDecoder>();
            break;
#endif
        case StreamFormat::Wav:
            dec = std::make_unique<WavDecoder>();
            break;
        case StreamFormat::PCM:
            dec = std::make_unique<PcmDecoder>();
            break;
        case StreamFormat::Aiff:
            dec = std::make_unique<AiffDecoder>();
            break;
        case StreamFormat::CDAudio:
            dec = std::make_unique<CdaDecoder>();
            break;
#ifdef LIBZFORK_WITH_OPUS
        case StreamFormat::Opus:
            dec = std::make_unique<OpusDecoder>();
            break;
#endif
        default:
            SetError(std::format("Unsupported format: {}", static_cast<int>(format)));
            return false;
    }

    bool ok = false;
    if (path) {
        ok = dec->OpenFile(path);
    } else {
        ok = dec->OpenMemory(static_cast<const uint8_t*>(memData), memSize);
    }
    if (!ok) {
        SetError(dec->GetError());
        return false;
    }

    decoder_ = std::move(dec);
    soundTouch_->Configure(decoder_->SampleRate(), decoder_->Channels());
    return true;
}

bool Player::OpenFile(const wchar_t* path, StreamFormat format) {
    Close();
    SongEntry e;
    e.path   = path ? path : L"";
    e.format = format;
    e.trackId = BuildFileTrackId(path, format);
    PrimeTrackCache(e);
    songs_.push_back(std::move(e));
    RegisterTrackId(songs_.back().trackId);
    FireQueueChangedEvent();
    return OpenDecoder(path, format, nullptr, 0);
}

bool Player::OpenFileA(const char* path, StreamFormat format) {
    if (!path) { SetError("null path"); return false; }
    const auto wpath = Utf8ToWide(path);
    return OpenFile(wpath.c_str(), format);
}

bool Player::LoadPlaylistFile(const wchar_t* path, StreamFormat fallbackFormat) {
    if (!path || !path[0]) {
        SetError("LoadPlaylistFile: invalid path");
        return false;
    }
    fs::path playlistPath(path);
    std::error_code ec;
    playlistPath = fs::absolute(playlistPath, ec);
    const std::wstring normalizedPlaylistPath = NormalizePlaylistPathForOs(playlistPath.wstring());
    playlistPath = fs::path(normalizedPlaylistPath);
    std::ifstream in(playlistPath, std::ios::binary);
    if (!in.is_open()) {
        SetError("LoadPlaylistFile: failed to open playlist");
        return false;
    }

    std::vector<std::string> lines;
    std::string line;
    bool firstLine = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (firstLine && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3); // UTF-8 BOM
        }
        firstLine = false;
        lines.push_back(std::move(line));
    }

    const std::string ext = ToLowerAscii(playlistPath.extension().string());
    std::vector<std::wstring> entries = (ext == ".pls")
        ? ParsePlsPlaylist(playlistPath, lines)
        : ParseM3uPlaylist(playlistPath, lines);

    if (entries.empty()) {
        SetError("LoadPlaylistFile: playlist contains no playable entries");
        return false;
    }

    Close();
    if (!OpenFile(entries.front().c_str(), fallbackFormat)) {
        SetError("LoadPlaylistFile: failed to open first playlist entry");
        return false;
    }

    for (size_t i = 1; i < entries.size(); ++i) {
        (void)AddFile(entries[i].c_str(), fallbackFormat);
    }
    return true;
}

bool Player::LoadPlaylistFileA(const char* path, StreamFormat fallbackFormat) {
    if (!path) {
        SetError("LoadPlaylistFileA: invalid path");
        return false;
    }
    const auto wpath = Utf8ToWide(path);
    return LoadPlaylistFile(wpath.c_str(), fallbackFormat);
}

bool Player::AddFile(const wchar_t* path, StreamFormat format) {
    SongEntry e;
    e.path   = path ? path : L"";
    e.format = format;
    e.trackId = BuildFileTrackId(path, format);
    if (!allowDuplicateTracks_ && e.trackId != 0 && trackRefCount_.contains(e.trackId)) {
        SetError("Duplicate track rejected (playlist duplicates disabled)");
        if (callback_) {
            callback_(this, callbackData_, CallbackMessage::QueueDuplicateRejectedAsync,
                      static_cast<uint32_t>(songs_.size()), static_cast<uint32_t>(songs_.size()));
        }
        return false;
    }
    PrimeTrackCache(e);
    songs_.push_back(std::move(e));
    RegisterTrackId(songs_.back().trackId);
    if (callback_) {
        callback_(this, callbackData_, CallbackMessage::QueueItemAddedAsync,
                  static_cast<uint32_t>(songs_.size() - 1), static_cast<uint32_t>(songs_.size()));
    }
    FireQueueChangedEvent();
    return true;
}

bool Player::AddFileA(const char* path, StreamFormat format) {
    if (!path) { SetError("null path"); return false; }
    const auto wpath = Utf8ToWide(path);
    return AddFile(wpath.c_str(), format);
}

bool Player::OpenStream(const void* data, uint32_t size, StreamFormat format) {
    Close();
    SongEntry e;
    e.format  = format;
    e.memData.assign(static_cast<const uint8_t*>(data),
                     static_cast<const uint8_t*>(data) + size);
    e.trackId = BuildMemoryTrackId(data, size, format);
    songs_.push_back(std::move(e));
    RegisterTrackId(songs_.back().trackId);
    FireQueueChangedEvent();
    return OpenDecoder(nullptr, format, data, size);
}

bool Player::RemoveQueueItem(uint32_t index) {
    if (index >= songs_.size()) {
        SetError("RemoveQueueItem: index out of range");
        return false;
    }
    const uint32_t current = static_cast<uint32_t>(songIndex_.load(std::memory_order_relaxed));
    if (status_ == PlayerStatus::Playing && index == current) {
        SetError("RemoveQueueItem: cannot remove currently playing item");
        return false;
    }

    UnregisterTrackId(songs_[index].trackId);
    songs_.erase(songs_.begin() + static_cast<std::ptrdiff_t>(index));
    if (index < current && current > 0) {
        songIndex_.store(static_cast<int>(current - 1), std::memory_order_relaxed);
    }
    FireQueueChangedEvent();
    return true;
}

bool Player::MoveQueueItem(uint32_t fromIndex, uint32_t toIndex) {
    if (fromIndex >= songs_.size() || toIndex >= songs_.size()) {
        SetError("MoveQueueItem: index out of range");
        return false;
    }
    if (fromIndex == toIndex) return true;

    auto item = std::move(songs_[fromIndex]);
    songs_.erase(songs_.begin() + static_cast<std::ptrdiff_t>(fromIndex));
    songs_.insert(songs_.begin() + static_cast<std::ptrdiff_t>(toIndex), std::move(item));

    int cur = songIndex_.load(std::memory_order_relaxed);
    if (cur == static_cast<int>(fromIndex)) cur = static_cast<int>(toIndex);
    else if (fromIndex < static_cast<uint32_t>(cur) && toIndex >= static_cast<uint32_t>(cur)) --cur;
    else if (fromIndex > static_cast<uint32_t>(cur) && toIndex <= static_cast<uint32_t>(cur)) ++cur;
    songIndex_.store(cur, std::memory_order_relaxed);

    FireQueueChangedEvent();
    return true;
}

uint32_t Player::GetQueueCount() const {
    return static_cast<uint32_t>(songs_.size());
}

bool Player::GetQueueItemPathA(uint32_t index, char* out, uint32_t outBytes) const {
    if (!out || outBytes == 0) return false;
    out[0] = '\0';
    if (index >= songs_.size()) return false;
    const std::string utf8 = WideToUtf8(songs_[index].path);
    if (utf8.size() + 1 > outBytes) return false;
    std::memcpy(out, utf8.c_str(), utf8.size() + 1);
    return true;
}

bool Player::OpenPCMStream(PCMFormat fmt) {
    Close();
    auto dec = std::make_unique<PcmDecoder>();
    dec->SetPCMFormat(fmt);
    decoder_ = std::move(dec);
    return true;
}

void Player::Close() {
    Stop();
    {
        std::lock_guard lock(stateMtx_);
        songs_.clear();
        trackRefCount_.clear();
        decoder_.reset();
        encoder_.reset();
        songIndex_ = 0;
    }
    FireQueueChangedEvent();
}

// ── Playback ───────────────────────────────────────────────────────────────────

bool Player::Play() {
    if (!decoder_) { SetError("No stream open"); return false; }
    if (status_ == PlayerStatus::Playing) return true;

    const bool useBuiltInOutput = !(disableBuiltInOutput_ && pcmOutputCallback_);
    if (useBuiltInOutput) {
        int sr  = decoder_->SampleRate();
        int ch  = decoder_->Channels();
        if (!output_->Init(sr, ch, 16, bufferMs_)) {
            SetError("Failed to open audio output");
            return false;
        }
        output_->Start();
        output_->SetMasterVolume(masterVolLeft_, masterVolRight_);
    }

    status_ = PlayerStatus::Playing;
    paused_ = false;
    pcmQueue_.Clear();
    pcmQueue_.Reopen();

    lastPosCbMs_ = 0;
    decodeThread_ = std::jthread([this](std::stop_token st) { DecodeThread(); });
    if (broadcastMirrorPlayback_) {
        UpdateBroadcastMetadataForCurrentSong();
    }
    if (callback_)
        callback_(this, callbackData_, CallbackMessage::PlayAsync, 0, 0);
    return true;
}

bool Player::PlayLoop(uint32_t loopCount) {
    loopCount_ = loopCount;
    return Play();
}

bool Player::Pause() {
    if (status_ != PlayerStatus::Playing) return false;
    paused_ = true;
    const bool useBuiltInOutput = !(disableBuiltInOutput_ && pcmOutputCallback_);
    if (useBuiltInOutput) output_->Pause();
    if (callback_)
        callback_(this, callbackData_, CallbackMessage::PauseAsync, 0, 0);
    return true;
}

bool Player::Resume() {
    if (!paused_) return false;
    paused_ = false;
    const bool useBuiltInOutput = !(disableBuiltInOutput_ && pcmOutputCallback_);
    if (useBuiltInOutput) output_->Resume();
    if (callback_)
        callback_(this, callbackData_, CallbackMessage::ResumeAsync, 0, 0);
    return true;
}

bool Player::Stop() {
    if (status_ == PlayerStatus::Stopped) return true;

    status_ = PlayerStatus::Stopped;
    pcmQueue_.Close();
    const bool useBuiltInOutput = !(disableBuiltInOutput_ && pcmOutputCallback_);
    if (useBuiltInOutput) output_->Stop(); // signals stopped_, waveOutReset → unblocks decode thread
    StopDecodeThread();    // join the decode thread
    if (useBuiltInOutput) output_->Close(); // release the device handle
    pcmQueue_.Clear();
    pcmQueue_.Reopen();
    paused_ = false;
    return true;
}

// ── Decode thread ──────────────────────────────────────────────────────────────

void Player::DecodeThread() {
    constexpr uint32_t kChunkFrames = 4096;
    std::vector<float> floatBuf(kChunkFrames * 2); // stereo
    std::vector<int16_t> pcm16(kChunkFrames * 2);

    while (status_ == PlayerStatus::Playing) {
        if (!decoder_) break;

        uint32_t framesDecoded = decoder_->DecodeFloat(floatBuf.data(), kChunkFrames);
        if (framesDecoded == 0) {
            if (dynamic_cast<PcmDecoder*>(decoder_.get()) != nullptr) {
                if (callback_) {
                    callback_(this, callbackData_, CallbackMessage::StreamNeedMoreDataAsync, 0, 0);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
                continue;
            }
            // End of stream – handle looping or next song
            if (loopCount_ > 0) {
                --loopCount_;
                decoder_->Seek(SeekOrigin::Begin, {});
                if (callback_)
                    callback_(this, callbackData_, CallbackMessage::EnterLoopAsync, 0, 0);
                if (loopCount_ == 0 && callback_)
                    callback_(this, callbackData_, CallbackMessage::ExitLoopAsync, 0, 0);
                continue;
            }
            // Try next song
            uint32_t nextIdx = songIndex_ + 1;
            if (nextIdx < static_cast<uint32_t>(songs_.size())) {
                songIndex_ = nextIdx;
                auto& song = songs_[nextIdx];
                if (!OpenDecoder(song.path.empty() ? nullptr : song.path.c_str(),
                                 song.format,
                                 song.memData.empty() ? nullptr : song.memData.data(),
                                 static_cast<uint32_t>(song.memData.size()))) break;
                if (!song.path.empty()) {
                    UpdateBroadcastMetadataFromPath(song.path);
                }
                if (callback_)
                    callback_(this, callbackData_, CallbackMessage::NextSongAsync, nextIdx, 0);
                continue;
            }
            break; // truly done
        }

        auto pcmSpan = std::span(floatBuf.data(), framesDecoded * decoder_->Channels());
        ApplyDSP(pcmSpan, decoder_->Channels(), decoder_->SampleRate());
        if (encoder_ && !encoder_->Write(pcmSpan)) {
            SetError(encoder_->GetError());
            break;
        }
#ifdef LIBZFORK_WITH_BROADCAST
        if (broadcastMirrorPlayback_) {
            (void)EnqueueBroadcastInterleaved(pcmSpan.data(), framesDecoded,
                                              decoder_->Channels(), decoder_->SampleRate());
        }
#endif
        UpdateVU(pcmSpan, decoder_->Channels());
        FeedFFT(pcmSpan, decoder_->Channels());

        if (pcmOutputCallback_) {
            pcmOutputCallback_(this, pcmOutputUserData_, pcmSpan.data(), framesDecoded,
                               decoder_->Channels(), decoder_->SampleRate());
        }

        // Periodic position callback (~4 updates per second)
        if (callback_) {
            StreamTime pos = decoder_->GetPosition();
            if (pos.ms - lastPosCbMs_ >= 250 || pos.ms < lastPosCbMs_) {
                StreamTime len = decoder_->GetLength();
                callback_(this, callbackData_, CallbackMessage::PositionAsync,
                          pos.ms, len.ms);
                lastPosCbMs_ = pos.ms;
            }
        }

        const bool useBuiltInOutput = !(disableBuiltInOutput_ && pcmOutputCallback_);
        if (useBuiltInOutput) {
            FloatToPCM16(pcmSpan,
                         std::span(pcm16.data(), framesDecoded * decoder_->Channels()));
            // Submit decoded PCM to output backend
            // IAudioOutput::WriteBlock expects frame count, not sample count.
            if (!output_->WriteBlock(pcm16.data(), framesDecoded)) break;
        }

        if (callback_)
            callback_(this, callbackData_, CallbackMessage::StreamBufferDoneAsync, 0, 0);
    }

    const bool useBuiltInOutput = !(disableBuiltInOutput_ && pcmOutputCallback_);
    if (useBuiltInOutput) {
        // Let remaining queued blocks finish playing (natural end-of-stream).
        output_->Drain();
    }

    if (status_ == PlayerStatus::Playing) {
        status_ = PlayerStatus::Stopped;
        if (callback_)
            callback_(this, callbackData_, CallbackMessage::StopAsync, 0, 0);
    }
}

void Player::StopDecodeThread() {
    if (decodeThread_.joinable()) {
        decodeThread_.request_stop();
        pcmQueue_.Close();
        // output_->Stop() already signalled the event — just join.
        decodeThread_.join();
    }
}

// ── DSP chain ──────────────────────────────────────────────────────────────────

void Player::ApplyDSP(std::span<float> pcm, int channels, int sampleRate) {
    if (equalizerEnabled_)  equalizer_->Process(pcm, channels, sampleRate);
    if (echoEnabled_)       echo_->Process(pcm, channels, sampleRate);
    if (vocalCutEnabled_)   centerCut_->ProcessVocalCut(pcm, channels);
    if (sideCutEnabled_)    centerCut_->ProcessSideCut(pcm, channels);
    if (stereoEnhancerEnabled_) stereoEnhancer_->Process(pcm, channels);
    soundTouch_->Process(pcm, channels);

    if (monoMix_ && channels == 2) {
        for (size_t i = 0; i < pcm.size(); i += 2) {
            float m = (pcm[i] + pcm[i + 1]) * 0.5f;
            pcm[i] = pcm[i + 1] = m;
        }
    }
    // Apply player volume
    float lv = playerVolLeft_  / 100.0f;
    float rv = playerVolRight_ / 100.0f;
    if (channels == 1) {
        for (auto& s : pcm) s *= lv;
    } else {
        for (size_t i = 0; i < pcm.size(); i += 2) {
            pcm[i]   *= lv;
            pcm[i+1] *= rv;
        }
    }
    for (auto& s : pcm) {
        s = std::clamp(s, -1.0f, 1.0f);
    }
}

void Player::UpdateVU(std::span<const float> pcm, int channels) {
    float l = 0.f, r = 0.f;
    if (channels == 1) {
        for (auto s : pcm) l = std::max(l, std::abs(s));
        r = l;
    } else {
        for (size_t i = 0; i < pcm.size(); i += 2) {
            l = std::max(l, std::abs(pcm[i]));
            r = std::max(r, std::abs(pcm[i + 1]));
        }
    }
    // Exponential decay toward current peak
    vuLeft_  = vuLeft_  * 0.85f + l * 0.15f;
    vuRight_ = vuRight_ * 0.85f + r * 0.15f;
}

bool Player::FFTConsumerActive() const {
    // 1.5s grace window: covers ~typical UI poll intervals (spectrum widgets
    // poll at 11-16ms) while letting FeedFFT go idle promptly once nobody
    // reads GetFFTData/DrawFFTGraph anymore.
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    uint64_t last = fftLastAccessMs_.load(std::memory_order_relaxed);
    return last != 0 && static_cast<uint64_t>(now) - last < 1500;
}

void Player::MarkFFTAccessed() const {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    fftLastAccessMs_.store(static_cast<uint64_t>(now), std::memory_order_relaxed);
}

void Player::FeedFFT(std::span<const float> pcm, int channels) {
    if (!FFTConsumerActive()) return;
    spectrum_->Feed(pcm, channels);
}

void Player::FloatToPCM16(std::span<const float> src, std::span<int16_t> dst) {
    size_t n = std::min(src.size(), dst.size());
    for (size_t i = 0; i < n; ++i) {
        float s = std::clamp(src[i], -1.0f, 1.0f);
        dst[i]  = static_cast<int16_t>(s * 32767.0f);
    }
}

void Player::PCM16ToFloat(std::span<const int16_t> src, std::span<float> dst) {
    size_t n = std::min(src.size(), dst.size());
    for (size_t i = 0; i < n; ++i)
        dst[i] = src[i] / 32768.0f;
}

// ── Position / Info ────────────────────────────────────────────────────────────

bool Player::Seek(SeekOrigin origin, const StreamTime& time) {
    if (!decoder_) { SetError("No stream open"); return false; }
    return decoder_->Seek(origin, time);
}

bool Player::GetPosition(StreamTime& outTime) const {
    if (!decoder_) return false;
    outTime = decoder_->GetPosition();
    return true;
}

bool Player::GetStreamInfo(int songIndex, StreamInfo& outInfo) const {
    if (!decoder_) return false;
    outInfo.samplingRate  = decoder_->SampleRate();
    outInfo.channelNumber = decoder_->Channels();
    outInfo.bitrate       = decoder_->Bitrate();
    outInfo.vbr           = decoder_->IsVBR();
    outInfo.length        = decoder_->GetLength();
    return true;
}

StreamStatus Player::GetStatus() const {
    StreamStatus s{};
    s.playing          = (status_ == PlayerStatus::Playing);
    s.paused           = paused_;
    s.echoEnabled      = echoEnabled_;
    s.equalizerEnabled = equalizerEnabled_;
    s.vocalCutEnabled  = vocalCutEnabled_;
    s.slidingVolume    = sliding_;
    s.songIndex        = songIndex_;
    s.songsInQueue     = static_cast<int>(songs_.size());
    return s;
}

// ── Volume ─────────────────────────────────────────────────────────────────────

bool Player::SetMasterVolume(int left, int right) {
    masterVolLeft_  = std::clamp(left,  0, 100);
    masterVolRight_ = std::clamp(right, 0, 100);
    if (output_) output_->SetMasterVolume(masterVolLeft_, masterVolRight_);
    return true;
}

bool Player::GetMasterVolume(int& outLeft, int& outRight) const {
    outLeft  = masterVolLeft_;
    outRight = masterVolRight_;
    return true;
}

bool Player::SetPlayerVolume(int left, int right) {
    playerVolLeft_  = std::clamp(left,  0, 100);
    playerVolRight_ = std::clamp(right, 0, 100);
    return true;
}

bool Player::GetPlayerVolume(int& outLeft, int& outRight) const {
    outLeft  = playerVolLeft_;
    outRight = playerVolRight_;
    return true;
}

bool Player::SlideVolume(int fromLeft, int fromRight, int toLeft, int toRight, uint32_t durationMs) {
    sliding_ = true;
    if (callback_)
        callback_(this, callbackData_, CallbackMessage::EnterVolumeSlideAsync, 0, 0);
    slideThread_ = std::jthread([=, this](std::stop_token st) {
        const int      steps  = 50;
        const uint32_t stepMs = durationMs / steps;
        for (int i = 0; i <= steps && !st.stop_requested(); ++i) {
            float t = static_cast<float>(i) / steps;
            int l = static_cast<int>(fromLeft  + t * (toLeft  - fromLeft));
            int r = static_cast<int>(fromRight + t * (toRight - fromRight));
            SetMasterVolume(l, r);
            std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
        }
        sliding_ = false;
        if (callback_)
            callback_(this, callbackData_, CallbackMessage::ExitVolumeSlideAsync, 0, 0);
    });
    return true;
}

bool Player::GetVUData(VUData& outVU) const {
    outVU.left  = vuLeft_;
    outVU.right = vuRight_;
    return true;
}

bool Player::MixChannels(bool mono) {
    monoMix_ = mono;
    return true;
}

// ── Speed / Pitch / Tempo ──────────────────────────────────────────────────────

bool Player::SetRate(float rate)        { return soundTouch_->SetRate(rate); }
bool Player::SetPitch(float semitones)  { return soundTouch_->SetPitch(semitones); }
bool Player::SetTempo(float tempo)      { return soundTouch_->SetTempo(tempo); }

// ── Equalizer ─────────────────────────────────────────────────────────────────

bool Player::EnableEqualizer(bool enable)  { equalizerEnabled_ = enable; return true; }
bool Player::SetEqualizerBandGain(int band, float gainDb) { return equalizer_->SetBandGain(band, gainDb); }
bool Player::GetEqualizerBandGain(int band, float& gainDb) const { return equalizer_->GetBandGain(band, gainDb); }
bool Player::ResetEqualizer()              { equalizer_->Reset(); return true; }

// ── Echo ──────────────────────────────────────────────────────────────────────

bool Player::EnableEcho(bool enable) { echoEnabled_ = enable; return true; }
bool Player::SetEchoParams(const EchoParams& p) { echo_->SetParams(p); return true; }
bool Player::GetEchoParams(EchoParams& p) const { p = echo_->GetParams(); return true; }

// ── Vocal / Side cut ──────────────────────────────────────────────────────────

bool Player::EnableVocalCut(bool enable) { vocalCutEnabled_ = enable; return true; }
bool Player::EnableSideCut(bool enable)  { sideCutEnabled_  = enable; return true; }
bool Player::EnableStereoEnhancer(bool enable) { stereoEnhancerEnabled_ = enable; return true; }
bool Player::SetStereoWidth(float width) {
    stereoEnhancer_->SetWidth(width);
    return true;
}
bool Player::GetStereoWidth(float& outWidth) const {
    outWidth = stereoEnhancer_->GetWidth();
    return true;
}

// ── FFT / Spectrum ────────────────────────────────────────────────────────────

bool Player::SetFFTPoints(uint32_t points)   { spectrum_->SetFFTSize(points); return true; }
bool Player::SetFFTWindow(FFTWindow window)   { spectrum_->SetWindow(window);  return true; }
bool Player::GetFFTData(float* outLeft, float* outRight, uint32_t points) const {
    MarkFFTAccessed();
    return spectrum_->GetData(outLeft, outRight, points);
}
bool Player::DrawFFTGraph(void* hdc, int x, int y, int w, int h) const {
    MarkFFTAccessed();
#ifdef _WIN32
    return spectrum_->DrawGraph(static_cast<HDC>(hdc), x, y, w, h);
#else
    return false;
#endif
}
bool Player::SetFFTGraphType(FFTGraphType type) { spectrum_->SetGraphType(type); return true; }
bool Player::SetFFTGraphColor(int index, uint32_t color) { spectrum_->SetColor(index, color); return true; }

// ── BPM detection ─────────────────────────────────────────────────────────────

bool Player::DetectBPM(BPMAlgorithm algo, float& outBPM) {
    // Never analyse via the live decoder: ReadEnergy() seeks to begin/end and
    // races the audio thread (breaks playback / returns garbage). Prefer a
    // separate file open of the current queue item.
    std::wstring path;
    {
        const int idx = songIndex_.load(std::memory_order_relaxed);
        if (idx >= 0 && idx < static_cast<int>(songs_.size()))
            path = songs_[static_cast<size_t>(idx)].path;
    }
    if (!path.empty())
        return DetectFileBPM(path.c_str(), algo, outBPM);

    if (!decoder_) {
        SetError("No stream open");
        return false;
    }
    SetError("DetectBPM: current item has no file path (use DetectFileBPM)");
    outBPM = 0.f;
    return false;
}

bool Player::DetectFileBPM(const wchar_t* path, BPMAlgorithm algo, float& outBPM) {
    auto dec = CreateDecoderForFile(path);
    if (!dec) {
        SetError("DetectFileBPM: unsupported format");
        return false;
    }
    if (!dec->OpenFile(path)) {
        SetError(dec->GetError());
        return false;
    }
    outBPM = spectrum_->DetectBPM(algo, *dec);
    if (outBPM > 0.0f) {
        UpdateTrackCacheBpm(BuildFileTrackId(path, StreamFormat::Autodetect), outBPM);
    }
    return outBPM > 0.0f;
}

bool Player::AnalyzeFile(const wchar_t* path, uint32_t flags, void** outHandle) {
    if (!path || !outHandle) {
        SetError("AnalyzeFile: invalid argument");
        return false;
    }
    *outHandle = nullptr;
    if (flags == 0) flags = ZFORK_ANALYZE_ALL;

    const uint64_t trackId = BuildFileTrackId(path, StreamFormat::Autodetect);
    auto& c = trackCache_[trackId];
    if (c.path.empty()) c.path = path;

    if ((flags & ZFORK_ANALYZE_LENGTH) && c.lengthMs == 0) {
        auto dec = CreateDecoderForFile(path);
        if (dec && dec->OpenFile(path)) c.lengthMs = dec->GetLength().ms;
    }
    if ((flags & ZFORK_ANALYZE_WAVEFORM) && NeedsWaveformRebuild(c.waveform)) {
        auto dec = CreateDecoderForFile(path);
        if (dec && dec->OpenFile(path)) {
            c.waveform = BuildWaveformPreview(*dec);
            c.peaks.clear();
        }
    }
    if ((flags & ZFORK_ANALYZE_BPM) && c.bpm <= 0.0f) {
        float bpm = 0.0f;
        if (DetectFileBPM(path, BPMAlgorithm::AutoCorrelation, bpm)) c.bpm = bpm;
    }
    if ((flags & ZFORK_ANALYZE_PEAKS) && c.peaks.empty()) {
        if (NeedsWaveformRebuild(c.waveform)) {
            auto dec = CreateDecoderForFile(path);
            if (dec && dec->OpenFile(path)) c.waveform = BuildWaveformPreview(*dec);
        }
        if (c.lengthMs == 0) {
            auto dec = CreateDecoderForFile(path);
            if (dec && dec->OpenFile(path)) c.lengthMs = dec->GetLength().ms;
        }
        if (!c.waveform.empty() && c.lengthMs > 0) c.peaks = BuildPeaksFromWaveformCsv(c.waveform, c.lengthMs);
    }

    auto* h = new AnalysisHandleData{};
    h->trackId = trackId;
    h->flags = flags;
    h->bpm = c.bpm;
    h->lengthMs = c.lengthMs;
    h->waveformBins = ParseWaveformCsv(c.waveform);
    h->peaks = ParsePeaksCsv(c.peaks);
    *outHandle = h;
    SaveTrackCache();
    return true;
}

void Player::FreeAnalysisHandle(void* handle) {
    delete static_cast<AnalysisHandleData*>(handle);
}

bool Player::GetAnalysisSummary(void* handle, float& outBpm, uint32_t& outLengthMs, uint32_t& outFlags) const {
    auto* h = static_cast<AnalysisHandleData*>(handle);
    if (!h) return false;
    outBpm = h->bpm;
    outLengthMs = h->lengthMs;
    outFlags = h->flags;
    return true;
}

uint32_t Player::CopyAnalysisWaveform(void* handle, uint8_t* outBins, uint32_t maxBins) const {
    auto* h = static_cast<AnalysisHandleData*>(handle);
    if (!h || !outBins || maxBins == 0) return 0;
    const uint32_t n = static_cast<uint32_t>(std::min<size_t>(h->waveformBins.size(), maxBins));
    std::memcpy(outBins, h->waveformBins.data(), n);
    return n;
}

uint32_t Player::CopyAnalysisPeaks(void* handle, float* outTimesMs, float* outValues, uint32_t maxPeaks) const {
    auto* h = static_cast<AnalysisHandleData*>(handle);
    if (!h || !outTimesMs || !outValues || maxPeaks == 0) return 0;
    const uint32_t n = static_cast<uint32_t>(std::min<size_t>(h->peaks.size(), maxPeaks));
    for (uint32_t i = 0; i < n; ++i) {
        outTimesMs[i] = h->peaks[i].first;
        outValues[i] = h->peaks[i].second;
    }
    return n;
}

// ── ID3 Tags ──────────────────────────────────────────────────────────────────

bool Player::LoadID3(int songIndex, ID3Info& outInfo) const {
    if (songIndex < 0 || songIndex >= static_cast<int>(songs_.size())) return false;
    return ID3Tags::LoadBasic(songs_[songIndex].path.c_str(), outInfo);
}
bool Player::LoadID3Ex(int songIndex, ID3InfoEx& outInfo) const {
    if (songIndex < 0 || songIndex >= static_cast<int>(songs_.size())) return false;
    return ID3Tags::LoadExtended(songs_[songIndex].path.c_str(), outInfo);
}
bool Player::LoadFileID3(const wchar_t* path, ID3Info& outInfo) const {
    return ID3Tags::LoadBasic(path, outInfo);
}
bool Player::LoadFileID3Ex(const wchar_t* path, ID3InfoEx& outInfo) const {
    return ID3Tags::LoadExtended(path, outInfo);
}
bool Player::GetID3Picture(int songIndex, int picIndex, ID3Picture& outPic) const {
    if (songIndex < 0 || songIndex >= static_cast<int>(songs_.size())) return false;
    return ID3Tags::GetPicture(songs_[songIndex].path.c_str(), picIndex, outPic);
}

// ── Wave output device ─────────────────────────────────────────────────────────

int Player::EnumerateWaveOutDevices() const {
    return output_ ? output_->EnumerateDevices() : 0;
}

bool Player::GetWaveOutDeviceInfo(int deviceId, WaveDeviceInfo& outInfo) const {
    return output_ ? output_->GetDeviceInfo(deviceId, outInfo) : false;
}

bool Player::SetWaveOutDevice(int deviceId) {
    return output_ ? output_->SetDevice(deviceId) : false;
}

bool Player::SetWaveOutFile(const wchar_t* path) {
    return output_ ? output_->SetOutputFile(path) : false;
}

// ── Wave input device ──────────────────────────────────────────────────────────

int Player::EnumerateWaveInDevices() const {
#ifdef _WIN32
    return static_cast<int>(waveInGetNumDevs());
#elif defined(LIBZFORK_WITH_PORTAUDIO)
    if (Pa_Initialize() != paNoError) return 0;
    const int count = Pa_GetDeviceCount();
    Pa_Terminate();
    return (count > 0) ? count : 0;
#else
    return 0;
#endif
}

bool Player::GetWaveInDeviceInfo(int deviceId, WaveDeviceInfo& outInfo) const {
#ifdef _WIN32
    WAVEINCAPSW caps{};
    if (waveInGetDevCapsW(deviceId, &caps, sizeof(caps)) != MMSYSERR_NOERROR) return false;
    outInfo.id       = deviceId;
    outInfo.channels = caps.wChannels;
    outInfo.formats  = caps.dwFormats;
    WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, outInfo.name, 64, nullptr, nullptr);
    return true;
#elif defined(LIBZFORK_WITH_PORTAUDIO)
    if (Pa_Initialize() != paNoError) return false;
    const PaDeviceInfo* info = Pa_GetDeviceInfo(deviceId);
    if (!info || info->maxInputChannels <= 0) {
        Pa_Terminate();
        return false;
    }
    outInfo.id = static_cast<uint32_t>(deviceId);
    outInfo.channels = static_cast<uint32_t>(std::max(1, info->maxInputChannels));
    outInfo.formats = 0;
    std::snprintf(outInfo.name, sizeof(outInfo.name), "%s", info->name ? info->name : "PortAudio Input");
    Pa_Terminate();
    return true;
#else
    (void)deviceId;
    (void)outInfo;
    return false;
#endif
}

bool Player::SetWaveInDevice(int deviceId) {
#ifdef _WIN32
    std::lock_guard lock(broadcastMtx_);
    broadcastMicCaptureDevice_ = deviceId;
    if (broadcastMicCaptureRunning_) {
        return StartBroadcastMicCaptureLocked(
            deviceId,
            broadcastMicCaptureSampleRate_ > 0 ? broadcastMicCaptureSampleRate_ : broadcastConfig_.sampleRate,
            broadcastMicCaptureChannels_ > 0 ? broadcastMicCaptureChannels_ : 1);
    }
    return true;
#elif defined(LIBZFORK_WITH_PORTAUDIO)
    std::lock_guard lock(broadcastMtx_);
    broadcastMicCaptureDevice_ = deviceId;
    if (broadcastMicCaptureRunning_) {
        return StartBroadcastMicCapturePortAudioLocked(
            deviceId,
            broadcastMicCaptureSampleRate_ > 0 ? broadcastMicCaptureSampleRate_ : broadcastConfig_.sampleRate,
            broadcastMicCaptureChannels_ > 0 ? broadcastMicCaptureChannels_ : 1);
    }
    return true;
#else
    (void)deviceId;
    return false;
#endif
}

// ── Encoding ──────────────────────────────────────────────────────────────────

bool Player::StartEncoding(StreamFormat format, const wchar_t* outputPath,
                           EncoderWriteCallback writeCallback, void* userData) {
    StopEncoding();
    encWriteCb_  = writeCallback;
    encUserData_ = userData;

    std::unique_ptr<EncoderBase> enc;
    switch (format) {
#ifdef LIBZFORK_WITH_MP3ENC
        case StreamFormat::Mp3:  enc = std::make_unique<Mp3Encoder>(); break;
#endif
#ifdef LIBZFORK_WITH_OGG
        case StreamFormat::Ogg:  enc = std::make_unique<OggEncoder>(); break;
#endif
#ifdef LIBZFORK_WITH_FLAC
        case StreamFormat::FLAC: enc = std::make_unique<FlacEncoder>(); break;
#endif
        case StreamFormat::Wav:  enc = std::make_unique<WavEncoder>(); break;
        default:
            SetError("Unsupported encoding format");
            return false;
    }
    if (!enc->Open(outputPath, decoder_ ? decoder_->SampleRate() : 44100,
                   decoder_ ? decoder_->Channels() : 2)) {
        SetError(enc->GetError());
        return false;
    }
    encoder_ = std::move(enc);
    return true;
}

bool Player::StopEncoding() {
    if (encoder_) encoder_->Close();
    encoder_.reset();
    return true;
}

// ── PCM push ──────────────────────────────────────────────────────────────────

bool Player::PushPCMData(const void* data, uint32_t byteCount) {
    if (!decoder_) { SetError("No PCM stream open"); return false; }
    auto* pcmDec = dynamic_cast<PcmDecoder*>(decoder_.get());
    if (!pcmDec) { SetError("Not a PCM stream"); return false; }
    return pcmDec->Push(data, byteCount);
}

// ── Decode-only (for push pipelines) ─────────────────────────────────────────

bool Player::DecodeNextChunk(float* outBuf, uint32_t maxFrames, uint32_t& decodedFrames) {
    decodedFrames = 0;
    if (!decoder_) { SetError("No stream open"); return false; }

    const uint32_t ch   = static_cast<uint32_t>(std::max(1, decoder_->Channels()));
    const uint32_t need = maxFrames * ch;
    const uint32_t got  = decoder_->DecodeFloat(outBuf, need);
    if (got == 0) { return false; }
    decodedFrames = got / ch;
    return true;
}

bool Player::StartBroadcast(const BroadcastConfig& config) {
#ifndef LIBZFORK_WITH_BROADCAST
    (void)config;
    broadcastError_ = "Broadcast support disabled at build time";
    return false;
#else
    bool wasRunning = false;
    {
        std::lock_guard lock(broadcastMtx_);
        wasRunning = broadcastRunning_;
    }
    if (wasRunning) StopBroadcast();

    std::lock_guard lock(broadcastMtx_);
    if (!IsValidBroadcastConfig(config)) {
        broadcastError_ = "StartBroadcast: invalid config (bitrate: 32..320 typical, channels: 1/2)";
        return false;
    }

    broadcastConfig_ = config;
    broadcastStats_ = {};
    broadcastError_.clear();
    broadcastQueue_.clear();
    broadcastQueuedFrames_ = 0;
    broadcastMicQueue_.clear();
    broadcastMicQueuedFrames_ = 0;
    broadcastMicFrontOffsetSamples_ = 0;
    broadcastDuckGain_ = 1.0f;
    broadcastMicActive_ = false;
    broadcastStopRequested_ = false;
    broadcastMetaTitle_.clear();
    broadcastMetaArtist_.clear();
    broadcastMetaAlbum_.clear();

    broadcastTransport_ = CreateBroadcastTransport();
    if (!broadcastTransport_ || !broadcastTransport_->Connect(broadcastConfig_)) {
        broadcastError_ = broadcastTransport_ ? broadcastTransport_->GetLastError() : "Broadcast transport init failed";
        return false;
    }
    broadcastEncoder_ = std::make_unique<Mp3BroadcastEncoder>();
    if (!broadcastEncoder_->Open(
            broadcastConfig_.sampleRate,
            broadcastConfig_.channels,
            broadcastConfig_.bitrateKbps,
            static_cast<int>(broadcastConfig_.stereoMode))) {
        broadcastError_ = broadcastEncoder_->GetError();
        broadcastTransport_->Disconnect();
        broadcastTransport_.reset();
        broadcastEncoder_.reset();
        return false;
    }
    if (broadcastConfig_.streamName[0]) {
        broadcastMetaTitle_ = broadcastConfig_.streamName;
        (void)ApplyBroadcastMetadataLocked();
    }

    broadcastRunning_ = true;
    broadcastStats_.connected = true;
    broadcastThread_ = std::jthread([this](std::stop_token) { BroadcastWorker(); });
    if (callback_) {
        callback_(this, callbackData_, CallbackMessage::BroadcastConnectedAsync,
                  IsLikelyShoutcast(broadcastConfig_) ? 1u : 0u, 0);
    }
    return true;
#endif
}

bool Player::StopBroadcast() {
#ifndef LIBZFORK_WITH_BROADCAST
    return true;
#else
    {
        std::lock_guard lock(broadcastMtx_);
        (void)StopBroadcastMicCaptureLocked();
    }
    StopBroadcastFileThread();

    {
        std::lock_guard lock(broadcastMtx_);
        broadcastStopRequested_ = true;
        broadcastMirrorPlayback_ = false;
    }
    broadcastCv_.notify_all();
    if (broadcastThread_.joinable()) {
        broadcastThread_.request_stop();
        broadcastThread_.join();
    }

    std::lock_guard lock(broadcastMtx_);
    if (broadcastEncoder_ && broadcastTransport_) {
        (void)broadcastEncoder_->Flush([this](std::span<const uint8_t> payload) {
            return broadcastTransport_->Send(payload);
        });
    }
    if (broadcastTransport_) broadcastTransport_->Disconnect();
    broadcastEncoder_.reset();
    broadcastTransport_.reset();
    broadcastQueue_.clear();
    broadcastQueuedFrames_ = 0;
    broadcastMicQueue_.clear();
    broadcastMicQueuedFrames_ = 0;
    broadcastMicFrontOffsetSamples_ = 0;
    broadcastDuckGain_ = 1.0f;
    broadcastMicActive_ = false;
    broadcastRunning_ = false;
    broadcastStopRequested_ = false;
    broadcastStats_.connected = false;
    return true;
#endif
}

void Player::StopBroadcastFileThread() {
#ifdef LIBZFORK_WITH_BROADCAST
    if (broadcastFileThread_.joinable()) {
        broadcastFileThread_.request_stop();
        broadcastFileThread_.join();
    }
#endif
}

bool Player::ApplyBroadcastMetadataLocked() {
#ifdef LIBZFORK_WITH_BROADCAST
    if (!broadcastTransport_ || !broadcastTransport_->IsConnected()) return false;
    if (!broadcastTransport_->SetMetadata(
            broadcastMetaTitle_.c_str(),
            broadcastMetaArtist_.c_str(),
            broadcastMetaAlbum_.c_str())) {
        broadcastError_ = broadcastTransport_->GetLastError();
        return false;
    }
    return true;
#else
    return false;
#endif
}

void Player::UpdateBroadcastMetadataFromPath(const std::wstring& path) {
#ifdef LIBZFORK_WITH_BROADCAST
    if (path.empty()) return;
    ID3InfoEx id3{};
    std::string title;
    std::string artist;
    std::string album;
    if (ID3Tags::LoadExtended(path.c_str(), id3)) {
        title = id3.title;
        artist = id3.artist;
        album = id3.album;
    }
    if (title.empty()) {
        title = fs::path(path).stem().string();
    }
    (void)SetBroadcastMetadata(title.c_str(), artist.c_str(), album.c_str());
#else
    (void)path;
#endif
}

void Player::UpdateBroadcastMetadataForCurrentSong() {
#ifdef LIBZFORK_WITH_BROADCAST
    std::wstring path;
    {
        std::lock_guard lock(stateMtx_);
        const int idx = songIndex_.load(std::memory_order_relaxed);
        if (idx < 0 || idx >= static_cast<int>(songs_.size())) return;
        path = songs_[static_cast<size_t>(idx)].path;
    }
    if (!path.empty()) UpdateBroadcastMetadataFromPath(path);
#endif
}

bool Player::StopBroadcastMicCaptureLocked() {
#ifndef LIBZFORK_WITH_BROADCAST
    return true;
#else
    broadcastMicCaptureRunning_ = false;
#ifdef LIBZFORK_WITH_PORTAUDIO
    StopBroadcastMicCapturePortAudioLocked();
#endif
#ifdef _WIN32
    if (waveInPollThread_.joinable()) {
        waveInPollThread_.request_stop();
        waveInPollThread_.join();
    }
    if (!waveInHandle_) return true;
    waveInStop(waveInHandle_);
    waveInReset(waveInHandle_);
    for (size_t i = 0; i < kWaveInBufferCount; ++i) {
        WAVEHDR& hdr = waveInHeaders_[i];
        if (hdr.dwFlags & WHDR_PREPARED) {
            waveInUnprepareHeader(waveInHandle_, &hdr, sizeof(WAVEHDR));
        }
        hdr = {};
        waveInBuffers_[i].clear();
    }
    waveInClose(waveInHandle_);
    waveInHandle_ = nullptr;
#endif
    return true;
#endif
}

#ifdef _WIN32
bool Player::StartBroadcastMicCaptureLocked(int deviceId, int sampleRate, int channels) {
#ifndef LIBZFORK_WITH_BROADCAST
    (void)deviceId; (void)sampleRate; (void)channels;
    return false;
#else
    if (!broadcastRunning_) {
        broadcastError_ = "StartBroadcastMicCapture: broadcast not running";
        return false;
    }
    if (sampleRate <= 0 || (channels != 1 && channels != 2)) {
        broadcastError_ = "StartBroadcastMicCapture: invalid format";
        return false;
    }
    if (sampleRate != broadcastConfig_.sampleRate) {
        broadcastError_ = "StartBroadcastMicCapture: sample rate must match broadcast";
        return false;
    }
    if (!StopBroadcastMicCaptureLocked()) return false;

    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = static_cast<WORD>(channels);
    fmt.nSamplesPerSec = static_cast<DWORD>(sampleRate);
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = static_cast<WORD>((fmt.nChannels * fmt.wBitsPerSample) / 8);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    fmt.cbSize = 0;

    const UINT dev = (deviceId < 0) ? WAVE_MAPPER : static_cast<UINT>(deviceId);
    MMRESULT mmr = waveInOpen(&waveInHandle_, dev, &fmt, 0, 0, CALLBACK_NULL);
    if (mmr != MMSYSERR_NOERROR || !waveInHandle_) {
        broadcastError_ = "StartBroadcastMicCapture: waveInOpen failed";
        waveInHandle_ = nullptr;
        return false;
    }

    const size_t samplesPerBuffer = kWaveInFramesPerBuffer * static_cast<size_t>(channels);
    for (size_t i = 0; i < kWaveInBufferCount; ++i) {
        waveInBuffers_[i].assign(samplesPerBuffer, 0);
        WAVEHDR& hdr = waveInHeaders_[i];
        std::memset(&hdr, 0, sizeof(WAVEHDR));
        hdr.lpData = reinterpret_cast<LPSTR>(waveInBuffers_[i].data());
        hdr.dwBufferLength = static_cast<DWORD>(waveInBuffers_[i].size() * sizeof(int16_t));
        mmr = waveInPrepareHeader(waveInHandle_, &hdr, sizeof(WAVEHDR));
        if (mmr != MMSYSERR_NOERROR) {
            broadcastError_ = "StartBroadcastMicCapture: waveInPrepareHeader failed";
            StopBroadcastMicCaptureLocked();
            return false;
        }
        mmr = waveInAddBuffer(waveInHandle_, &hdr, sizeof(WAVEHDR));
        if (mmr != MMSYSERR_NOERROR) {
            broadcastError_ = "StartBroadcastMicCapture: waveInAddBuffer failed";
            StopBroadcastMicCaptureLocked();
            return false;
        }
    }
    mmr = waveInStart(waveInHandle_);
    if (mmr != MMSYSERR_NOERROR) {
        broadcastError_ = "StartBroadcastMicCapture: waveInStart failed";
        StopBroadcastMicCaptureLocked();
        return false;
    }

    broadcastMicCaptureDevice_ = deviceId;
    broadcastMicCaptureSampleRate_ = sampleRate;
    broadcastMicCaptureChannels_ = channels;
    broadcastMicCaptureRunning_ = true;
    waveInPollThread_ = std::jthread([this](std::stop_token st) {
        while (!st.stop_requested()) {
            {
                std::unique_lock lock(broadcastMtx_, std::try_to_lock);
                if (!lock.owns_lock()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(3));
                    continue;
                }
                if (!waveInHandle_ || !broadcastMicCaptureRunning_) break;
                for (size_t i = 0; i < kWaveInBufferCount; ++i) {
                    WAVEHDR& hdr = waveInHeaders_[i];
                    if ((hdr.dwFlags & WHDR_DONE) && hdr.dwBytesRecorded > 0) {
                        OnWaveInBufferDone(i, hdr.dwBytesRecorded);
                        hdr.dwBytesRecorded = 0;
                        hdr.dwFlags &= ~WHDR_DONE;
                        (void)waveInAddBuffer(waveInHandle_, &hdr, sizeof(WAVEHDR));
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    return true;
#endif
}

void Player::OnWaveInBufferDone(size_t bufferIndex, uint32_t bytesRecorded) {
#ifndef LIBZFORK_WITH_BROADCAST
    (void)bufferIndex; (void)bytesRecorded;
#else
    if (bufferIndex >= kWaveInBufferCount || bytesRecorded == 0) return;
    if (!broadcastMicActive_) return;
    const int channels = std::max(1, broadcastMicCaptureChannels_);
    const size_t samples = std::min<size_t>(waveInBuffers_[bufferIndex].size(),
                                           static_cast<size_t>(bytesRecorded / sizeof(int16_t)));
    const uint32_t frames = static_cast<uint32_t>(samples / static_cast<size_t>(channels));
    if (frames == 0) return;
    std::vector<float> mic(samples);
    for (size_t i = 0; i < samples; ++i) {
        mic[i] = static_cast<float>(waveInBuffers_[bufferIndex][i]) / 32768.0f;
    }
    (void)EnqueueBroadcastMicInterleavedLocked(
        mic.data(),
        frames,
        channels,
        broadcastMicCaptureSampleRate_);
#endif
}
#endif

#ifdef LIBZFORK_WITH_PORTAUDIO
bool Player::StartBroadcastMicCapturePortAudioLocked(int deviceId, int sampleRate, int channels) {
#ifndef LIBZFORK_WITH_BROADCAST
    (void)deviceId; (void)sampleRate; (void)channels;
    return false;
#else
    if (!broadcastRunning_) {
        broadcastError_ = "StartBroadcastMicCapture: broadcast not running";
        return false;
    }
    if (sampleRate <= 0 || (channels != 1 && channels != 2)) {
        broadcastError_ = "StartBroadcastMicCapture: invalid format";
        return false;
    }
    if (sampleRate != broadcastConfig_.sampleRate) {
        broadcastError_ = "StartBroadcastMicCapture: sample rate must match broadcast";
        return false;
    }
    if (paMicStream_ || paMicCaptureThread_.joinable() || broadcastMicCaptureRunning_) {
        StopBroadcastMicCapturePortAudioLocked();
    }

    if (!paMicInitialized_) {
        if (Pa_Initialize() != paNoError) {
            broadcastError_ = "StartBroadcastMicCapture: Pa_Initialize failed";
            return false;
        }
        paMicInitialized_ = true;
    }

    const int hostDevice = (deviceId >= 0) ? deviceId : static_cast<int>(Pa_GetDefaultInputDevice());
    if (hostDevice < 0 || hostDevice >= Pa_GetDeviceCount()) {
        broadcastError_ = "StartBroadcastMicCapture: invalid PortAudio input device";
        return false;
    }

    PaStreamParameters in{};
    in.device = hostDevice;
    const PaDeviceInfo* dev = Pa_GetDeviceInfo(in.device);
    if (!dev) {
        broadcastError_ = "StartBroadcastMicCapture: Pa_GetDeviceInfo failed";
        return false;
    }
    in.channelCount = channels;
    in.sampleFormat = paFloat32;
    in.suggestedLatency = dev->defaultLowInputLatency;
    in.hostApiSpecificStreamInfo = nullptr;

    PaStream* stream = nullptr;
    if (Pa_OpenStream(&stream, &in, nullptr, static_cast<double>(sampleRate),
                      512, paNoFlag, nullptr, nullptr) != paNoError || !stream) {
        broadcastError_ = "StartBroadcastMicCapture: Pa_OpenStream failed";
        paMicStream_ = nullptr;
        return false;
    }
    paMicStream_ = stream;
    if (Pa_StartStream(reinterpret_cast<PaStream*>(paMicStream_)) != paNoError) {
        broadcastError_ = "StartBroadcastMicCapture: Pa_StartStream failed";
        Pa_CloseStream(reinterpret_cast<PaStream*>(paMicStream_));
        paMicStream_ = nullptr;
        return false;
    }

    broadcastMicCaptureDevice_ = deviceId;
    broadcastMicCaptureSampleRate_ = sampleRate;
    broadcastMicCaptureChannels_ = channels;
    broadcastMicCaptureRunning_ = true;
    paMicCaptureThread_ = std::jthread([this, channels](std::stop_token st) {
        std::vector<float> chunk(static_cast<size_t>(512 * channels));
        while (!st.stop_requested()) {
            if (!paMicStream_ || !broadcastMicCaptureRunning_) break;
            const PaError er = Pa_ReadStream(reinterpret_cast<PaStream*>(paMicStream_), chunk.data(), 512);
            if (er != paNoError) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            std::unique_lock lock(broadcastMtx_, std::try_to_lock);
            if (!lock.owns_lock()) continue;
            if (!broadcastMicActive_) continue;
            (void)EnqueueBroadcastMicInterleavedLocked(
                chunk.data(),
                512,
                channels,
                broadcastMicCaptureSampleRate_);
        }
    });
    return true;
#endif
}

void Player::StopBroadcastMicCapturePortAudioLocked() {
    if (paMicCaptureThread_.joinable()) {
        paMicCaptureThread_.request_stop();
        paMicCaptureThread_.join();
    }
    if (paMicStream_) {
        auto* stream = reinterpret_cast<PaStream*>(paMicStream_);
        Pa_StopStream(stream);
        Pa_CloseStream(stream);
        paMicStream_ = nullptr;
    }
    if (paMicInitialized_) {
        Pa_Terminate();
        paMicInitialized_ = false;
    }
}
#endif

bool Player::EnqueueBroadcastMicInterleavedLocked(const float* interleaved, uint32_t frames, int channels, int sampleRate) {
#ifndef LIBZFORK_WITH_BROADCAST
    (void)interleaved; (void)frames; (void)channels; (void)sampleRate;
    return false;
#else
    if (!interleaved || frames == 0) return true;
    if (!broadcastRunning_) return false;
    if (sampleRate != broadcastConfig_.sampleRate) {
        broadcastError_ = "Broadcast mic sample rate mismatch";
        return false;
    }
    if (!(channels == 1 || channels == 2)) {
        broadcastError_ = "Broadcast mic channel count unsupported";
        return false;
    }

    const int outChannels = std::max(1, broadcastConfig_.channels);
    std::vector<float> converted(static_cast<size_t>(frames) * static_cast<size_t>(outChannels));
    if (channels == outChannels) {
        std::memcpy(converted.data(), interleaved, converted.size() * sizeof(float));
    } else if (channels == 1 && outChannels == 2) {
        for (size_t i = 0; i < frames; ++i) {
            const float s = interleaved[i];
            converted[i * 2] = s;
            converted[i * 2 + 1] = s;
        }
    } else if (channels == 2 && outChannels == 1) {
        for (size_t i = 0; i < frames; ++i) {
            converted[i] = 0.5f * (interleaved[i * 2] + interleaved[i * 2 + 1]);
        }
    } else {
        broadcastError_ = "Broadcast mic format mismatch";
        return false;
    }

    constexpr size_t kMaxMicQueuedFrames = 44100u * 6u;
    if (broadcastMicQueuedFrames_ > kMaxMicQueuedFrames) {
        broadcastStats_.droppedFrames += frames;
        return false;
    }
    broadcastMicQueue_.push_back(std::move(converted));
    broadcastMicQueuedFrames_ += frames;
    return true;
#endif
}

bool Player::EnqueueBroadcastMicInterleaved(const float* interleaved, uint32_t frames, int channels, int sampleRate) {
#ifndef LIBZFORK_WITH_BROADCAST
    (void)interleaved; (void)frames; (void)channels; (void)sampleRate;
    return false;
#else
    std::lock_guard lock(broadcastMtx_);
    return EnqueueBroadcastMicInterleavedLocked(interleaved, frames, channels, sampleRate);
#endif
}

void Player::StepBroadcastDuckGainLocked(bool micActive, uint32_t frames) {
#ifdef LIBZFORK_WITH_BROADCAST
    const float target = (broadcastDucking_.enabled && micActive) ? DbToLinear(broadcastDucking_.duckDb) : 1.0f;
    const uint32_t timeMs = (target < broadcastDuckGain_) ? broadcastDucking_.attackMs : broadcastDucking_.releaseMs;
    const float frameRate = static_cast<float>(std::max(1, broadcastConfig_.sampleRate));
    const float delta = static_cast<float>(frames) / std::max(1.0f, frameRate * (static_cast<float>(timeMs) / 1000.0f));
    const float t = std::clamp(delta, 0.0f, 1.0f);
    broadcastDuckGain_ += (target - broadcastDuckGain_) * t;
#else
    (void)micActive; (void)frames;
#endif
}

void Player::MixBroadcastMicLocked(std::vector<float>& program, uint32_t frames, int channels) {
#ifdef LIBZFORK_WITH_BROADCAST
    if (program.empty()) return;
    const size_t sampleCount = program.size();
    std::vector<float> mic(sampleCount, 0.0f);
    size_t writeOffset = 0;
    while (writeOffset < sampleCount && !broadcastMicQueue_.empty()) {
        auto& front = broadcastMicQueue_.front();
        const size_t avail = front.size() - broadcastMicFrontOffsetSamples_;
        const size_t need = sampleCount - writeOffset;
        const size_t take = std::min(avail, need);
        std::memcpy(mic.data() + writeOffset, front.data() + broadcastMicFrontOffsetSamples_, take * sizeof(float));
        writeOffset += take;
        broadcastMicFrontOffsetSamples_ += take;
        if (broadcastMicFrontOffsetSamples_ >= front.size()) {
            const size_t consumedFrames = front.size() / static_cast<size_t>(std::max(1, channels));
            broadcastMicQueuedFrames_ = (broadcastMicQueuedFrames_ > consumedFrames) ? (broadcastMicQueuedFrames_ - consumedFrames) : 0;
            broadcastMicFrontOffsetSamples_ = 0;
            broadcastMicQueue_.pop_front();
        }
    }

    float micPeak = 0.0f;
    for (float s : mic) micPeak = std::max(micPeak, std::abs(s));
    const bool micSignal = broadcastMicActive_ && micPeak >= broadcastDucking_.threshold;
    StepBroadcastDuckGainLocked(micSignal, frames);

    for (size_t i = 0; i < sampleCount; ++i) {
        float mixed = program[i] * broadcastDuckGain_ + mic[i];
        program[i] = std::clamp(mixed, -1.0f, 1.0f);
    }
#else
    (void)program; (void)frames; (void)channels;
#endif
}

bool Player::EnqueueBroadcastInterleaved(const float* interleaved, uint32_t frames, int channels, int sampleRate) {
#ifndef LIBZFORK_WITH_BROADCAST
    (void)interleaved; (void)frames; (void)channels; (void)sampleRate;
    return false;
#else
    if (!interleaved || frames == 0) return true;

    std::lock_guard lock(broadcastMtx_);
    if (!broadcastRunning_ || !broadcastEncoder_ || !broadcastTransport_) return false;
    if (channels != broadcastConfig_.channels || sampleRate != broadcastConfig_.sampleRate) {
        broadcastError_ = "Broadcast PCM format mismatch";
        return false;
    }

    constexpr size_t kMaxQueuedFrames = 44100u * 12u;
    if (broadcastQueuedFrames_ > kMaxQueuedFrames) {
        broadcastStats_.droppedFrames += frames;
        if (callback_) {
            callback_(this, callbackData_, CallbackMessage::BroadcastUnderrunAsync,
                      static_cast<uint32_t>(broadcastStats_.droppedFrames & 0xFFFFFFFFu), 0);
        }
        return false;
    }

    std::vector<float> copy(static_cast<size_t>(frames) * static_cast<size_t>(channels));
    std::memcpy(copy.data(), interleaved, copy.size() * sizeof(float));
    MixBroadcastMicLocked(copy, frames, channels);
    broadcastQueue_.push_back(std::move(copy));
    broadcastQueuedFrames_ += frames;
    broadcastCv_.notify_one();
    return true;
#endif
}

void Player::BroadcastWorker() {
#ifdef LIBZFORK_WITH_BROADCAST
    while (true) {
        std::vector<float> chunk;
        {
            std::unique_lock lock(broadcastMtx_);
            broadcastCv_.wait(lock, [this] {
                return broadcastStopRequested_ || !broadcastQueue_.empty();
            });
            if (broadcastStopRequested_ && broadcastQueue_.empty()) break;
            if (broadcastQueue_.empty()) continue;
            chunk = std::move(broadcastQueue_.front());
            broadcastQueue_.pop_front();
            const size_t frames = chunk.size() / static_cast<size_t>(std::max(1, broadcastConfig_.channels));
            broadcastQueuedFrames_ = (broadcastQueuedFrames_ > frames) ? (broadcastQueuedFrames_ - frames) : 0;
        }

        auto sendPayload = [this](std::span<const uint8_t> payload) -> bool {
            std::lock_guard lock(broadcastMtx_);
            if (!broadcastTransport_ || !broadcastTransport_->IsConnected()) return false;
            if (!broadcastTransport_->Send(payload)) {
                broadcastError_ = broadcastTransport_->GetLastError();
                return false;
            }
            broadcastStats_.bytesSent += payload.size();
            return true;
        };

        bool ok = broadcastEncoder_->Encode(chunk, sendPayload);
        if (ok) {
            const uint64_t f = chunk.size() / static_cast<size_t>(std::max(1, broadcastConfig_.channels));
            broadcastStats_.framesSent += f;
            continue;
        }

        // Reconnect path
        uint32_t retry = 0;
        while (!broadcastStopRequested_ && broadcastConfig_.autoReconnect) {
            ++retry;
            ++broadcastStats_.reconnectCount;
            if (callback_) {
                callback_(this, callbackData_, CallbackMessage::BroadcastReconnectingAsync,
                          retry, broadcastConfig_.reconnectDelayMs);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(broadcastConfig_.reconnectDelayMs));
            {
                std::lock_guard lock(broadcastMtx_);
                if (broadcastTransport_ && broadcastTransport_->Connect(broadcastConfig_)) {
                    broadcastStats_.connected = true;
                    (void)ApplyBroadcastMetadataLocked();
                    if (callback_) {
                        callback_(this, callbackData_, CallbackMessage::BroadcastConnectedAsync,
                                  IsLikelyShoutcast(broadcastConfig_) ? 1u : 0u, 0);
                    }
                    break;
                }
                broadcastError_ = broadcastTransport_ ? broadcastTransport_->GetLastError() : "Broadcast transport missing";
            }
            if (callback_ && (broadcastError_.find("auth") != std::string::npos ||
                              broadcastError_.find("password") != std::string::npos)) {
                callback_(this, callbackData_, CallbackMessage::BroadcastAuthFailedAsync, 0, 0);
            }
        }
        if (!broadcastConfig_.autoReconnect) {
            if (callback_) callback_(this, callbackData_, CallbackMessage::BroadcastDisconnectedAsync, 0, 0);
            break;
        }
    }
#endif
}

bool Player::SetBroadcastMetadata(const char* title, const char* artist, const char* album) {
#ifndef LIBZFORK_WITH_BROADCAST
    (void)title; (void)artist; (void)album;
    broadcastError_ = "Broadcast support disabled at build time";
    return false;
#else
    std::lock_guard lock(broadcastMtx_);
    if (!broadcastRunning_) {
        broadcastError_ = "SetBroadcastMetadata: broadcast not running";
        return false;
    }
    broadcastMetaTitle_ = title ? title : "";
    broadcastMetaArtist_ = artist ? artist : "";
    broadcastMetaAlbum_ = album ? album : "";
    if (!broadcastTransport_ || !broadcastTransport_->IsConnected()) {
        return true; // keep metadata and apply after reconnect.
    }
    return ApplyBroadcastMetadataLocked();
#endif
}

bool Player::PushBroadcastPCM(const float* interleaved, uint32_t frames, int channels, int sampleRate) {
    return EnqueueBroadcastInterleaved(interleaved, frames, channels, sampleRate);
}

bool Player::PushBroadcastMicPCM(const float* interleaved, uint32_t frames, int channels, int sampleRate) {
    return EnqueueBroadcastMicInterleaved(interleaved, frames, channels, sampleRate);
}

bool Player::SetBroadcastMicActive(bool active) {
#ifndef LIBZFORK_WITH_BROADCAST
    (void)active;
    broadcastError_ = "Broadcast support disabled at build time";
    return false;
#else
    broadcastMicActive_ = active;
    return true;
#endif
}

bool Player::SetBroadcastDucking(const BroadcastDuckingConfig& config) {
#ifndef LIBZFORK_WITH_BROADCAST
    (void)config;
    broadcastError_ = "Broadcast support disabled at build time";
    return false;
#else
    if (!IsValidDuckingConfig(config)) {
        std::lock_guard lock(broadcastMtx_);
        broadcastError_ = "SetBroadcastDucking: invalid config";
        return false;
    }
    std::lock_guard lock(broadcastMtx_);
    broadcastDucking_ = config;
    return true;
#endif
}

bool Player::GetBroadcastDucking(BroadcastDuckingConfig& outConfig) const {
#ifndef LIBZFORK_WITH_BROADCAST
    (void)outConfig;
    return false;
#else
    std::lock_guard lock(broadcastMtx_);
    outConfig = broadcastDucking_;
    return true;
#endif
}

bool Player::StartBroadcastMicCapture(int deviceId, int sampleRate, int channels) {
#ifndef LIBZFORK_WITH_BROADCAST
    (void)deviceId; (void)sampleRate; (void)channels;
    broadcastError_ = "Broadcast support disabled at build time";
    return false;
#else
#ifdef _WIN32
    std::lock_guard lock(broadcastMtx_);
    return StartBroadcastMicCaptureLocked(deviceId, sampleRate, channels);
#elif defined(LIBZFORK_WITH_PORTAUDIO)
    std::lock_guard lock(broadcastMtx_);
    return StartBroadcastMicCapturePortAudioLocked(deviceId, sampleRate, channels);
#else
    (void)deviceId; (void)sampleRate; (void)channels;
    std::lock_guard lock(broadcastMtx_);
    broadcastError_ = "StartBroadcastMicCapture: no input backend compiled (enable PortAudio)";
    return false;
#endif
#endif
}

bool Player::StopBroadcastMicCapture() {
#ifndef LIBZFORK_WITH_BROADCAST
    return true;
#else
    std::lock_guard lock(broadcastMtx_);
    return StopBroadcastMicCaptureLocked();
#endif
}

bool Player::StartBroadcastFile(const wchar_t* path, StreamFormat inputFormat) {
#ifndef LIBZFORK_WITH_BROADCAST
    (void)path; (void)inputFormat;
    broadcastError_ = "Broadcast support disabled at build time";
    return false;
#else
    if (!path || !path[0]) {
        broadcastError_ = "StartBroadcastFile: invalid path";
        return false;
    }
    {
        std::lock_guard lock(broadcastMtx_);
        if (!broadcastRunning_) {
            broadcastError_ = "StartBroadcastFile: broadcast not running";
            return false;
        }
    }

    StopBroadcastFileThread();
    std::wstring filePath(path);
    broadcastFileThread_ = std::jthread([this, filePath = std::move(filePath), inputFormat](std::stop_token st) {
        std::unique_ptr<DecoderBase> dec;
        if (inputFormat == StreamFormat::Autodetect) {
            dec = CreateDecoderForFile(filePath.c_str());
        } else {
            // For now, non-autodetect falls back to extension-based factory.
            dec = CreateDecoderForFile(filePath.c_str());
        }
        if (!dec || !dec->OpenFile(filePath.c_str())) {
            std::lock_guard lock(broadcastMtx_);
            broadcastError_ = dec ? dec->GetError() : "StartBroadcastFile: unsupported format";
            return;
        }
        UpdateBroadcastMetadataFromPath(filePath);

        const int channels = std::max(1, dec->Channels());
        const int sampleRate = dec->SampleRate();
        std::vector<float> chunk(static_cast<size_t>(4096u * channels));
        while (!st.stop_requested()) {
            const uint32_t gotFrames = dec->DecodeFloat(chunk.data(), 4096);
            if (gotFrames == 0) break;
            (void)EnqueueBroadcastInterleaved(chunk.data(), gotFrames, channels, sampleRate);
        }
    });
    return true;
#endif
}

bool Player::AttachBroadcastToCurrentPlayback(bool enable) {
    {
        std::lock_guard lock(broadcastMtx_);
        if (enable && !broadcastRunning_) {
            broadcastError_ = "AttachBroadcastToCurrentPlayback: broadcast not running";
            return false;
        }
        broadcastMirrorPlayback_ = enable;
    }
    if (enable) UpdateBroadcastMetadataForCurrentSong();
    return true;
}

bool Player::GetBroadcastStats(BroadcastStats& outStats) const {
    std::lock_guard lock(broadcastMtx_);
    outStats = broadcastStats_;
    return true;
}

const char* Player::GetBroadcastError() const {
    std::lock_guard lock(broadcastMtx_);
    return broadcastError_.c_str();
}

// ── Settings ──────────────────────────────────────────────────────────────────

bool Player::SetBufferSize(uint32_t ms) { bufferMs_ = ms; return true; }
bool Player::SetAccurateLength(bool a)  { accurateLength_ = a; return true; }
bool Player::SetAccurateSeek(bool a)    { accurateSeek_   = a; return true; }
bool Player::SetPlaylistAllowDuplicates(bool allow) { allowDuplicateTracks_ = allow; return true; }
bool Player::GetPlaylistAllowDuplicates() const { return allowDuplicateTracks_; }
bool Player::SetCallback(PlayerCallback cb, void* userData) {
    callback_     = cb;
    callbackData_ = userData;
    return true;
}

bool Player::SetPCMOutputCallback(PCMOutputCallback callback, void* userData,
                                  bool disableBuiltInOutput) {
    pcmOutputCallback_ = callback;
    pcmOutputUserData_ = userData;
    disableBuiltInOutput_ = (disableBuiltInOutput && callback != nullptr);
    return true;
}

} // namespace libZFork
