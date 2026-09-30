#include "engine/soundbank_v1.h"
#include "engine/audio_context.h"
#include "engine/effect_chain.h"
#include "engine/engine_defs.h"
#include "engine/sound_cache.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace libZFork::engine {

namespace fs = std::filesystem;

namespace {
constexpr char     kBinaryMagic[4] = { 'Z', 'F', 'B', 'K' };
// v1: base format. v2: + trailing fx section. v3: + per-entry embedded-blob
// byte, blob bytes written inline right after each entry. v4: embedded blobs
// moved out of the TOC into one DATA block at the end of the file, each TOC
// entry now storing (blobOffset, blobSize) into it instead of inline bytes —
// so the TOC stays compact/quick to scan regardless of how much audio is
// embedded. Older files still load (missing fields treated as their default:
// fxCount=0, embedded=0; v3's inline layout is still read, just not written).
constexpr uint32_t kBinaryVersion   = 4;
constexpr uint32_t kMaxBankEntries  = 100000;
constexpr uint32_t kMaxEmbeddedBlob = 64u * 1024u * 1024u; // 64 MiB per clip

// A parsed "@echo"/"@reverb" bank-file directive — see soundbank_v1.h for
// the text/binary formats. Shared by text parsing, binary read and the
// text->binary converter so all three agree on field meaning.
struct FxDirective {
    std::string group;
    uint8_t     kind = 0; // 0 = echo, 1 = reverb
    // echo
    int32_t leftDelayMs = 0, leftSrcVol = 100, leftEchoVol = 45;
    int32_t rightDelayMs = 0, rightSrcVol = 100, rightEchoVol = 45;
    // reverb
    float roomSize = 0.5f, damping = 0.5f, wet = 0.3f, dry = 0.7f;
};

template <typename T>
void WritePod(std::ostream& out, const T& v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T>
bool ReadPod(std::istream& in, T& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<bool>(in);
}

void WriteLenString(std::ostream& out, const std::string& s) {
    uint16_t len = static_cast<uint16_t>(std::min<std::size_t>(s.size(), 0xFFFFu));
    WritePod(out, len);
    out.write(s.data(), len);
}

bool ReadLenString(std::istream& in, std::string& s) {
    uint16_t len = 0;
    if (!ReadPod(in, len)) return false;
    s.resize(len);
    if (len == 0) return true;
    in.read(s.data(), len);
    return static_cast<bool>(in);
}
} // namespace

static inline std::string Trim(std::string s) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
        [&](char c) { return !isSpace(static_cast<unsigned char>(c)); }));
    s.erase(std::find_if(s.rbegin(), s.rend(),
        [&](char c) { return !isSpace(static_cast<unsigned char>(c)); }).base(), s.end());
    return s;
}

// Convert a UTF-8 encoded std::string to a std::wstring.
// On Windows we use the Win32 API (CP_UTF8) to guarantee correct conversion.
// On other platforms the string is assumed to be valid UTF-8 and we convert
// byte-by-byte via the standard library path constructor.
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        w.data(), n);
    return w;
#else
    // POSIX: treat the narrow string as a native path and let the filesystem
    // library do the right thing.
    return fs::path(s).wstring();
#endif
}

static inline std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static std::vector<std::string> SplitSemicolon(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string part;
    while (std::getline(ss, part, ';')) {
        out.push_back(Trim(part));
    }
    return out;
}

static float ParseFloat(const std::string& s, float fallback) {
    try { return std::stof(s); } catch (...) { return fallback; }
}

static int32_t ParseInt(const std::string& s, int32_t fallback) {
    try { return std::stoi(s); } catch (...) { return fallback; }
}

// Parses the part after '@' from a directive line (e.g. "echo;ui;180;100;45;180;100;45").
// Returns false for an unrecognised/malformed directive (caller should skip the line).
static bool ParseFxDirective(const std::string& afterAt, FxDirective& out) {
    auto fields = SplitSemicolon(afterAt);
    if (fields.size() < 2 || fields[1].empty()) return false;

    std::string kind = ToLower(fields[0]);
    out.group = fields[1];

    if (kind == "echo") {
        if (fields.size() < 8) return false;
        out.kind          = 0;
        out.leftDelayMs   = ParseInt(fields[2], 0);
        out.leftSrcVol    = ParseInt(fields[3], 100);
        out.leftEchoVol   = ParseInt(fields[4], 45);
        out.rightDelayMs  = ParseInt(fields[5], 0);
        out.rightSrcVol   = ParseInt(fields[6], 100);
        out.rightEchoVol  = ParseInt(fields[7], 45);
        return true;
    }
    if (kind == "reverb") {
        if (fields.size() < 6) return false;
        out.kind     = 1;
        out.roomSize = ParseFloat(fields[2], 0.5f);
        out.damping  = ParseFloat(fields[3], 0.5f);
        out.wet      = ParseFloat(fields[4], 0.3f);
        out.dry      = ParseFloat(fields[5], 0.7f);
        return true;
    }
    return false;
}

// Installs a parsed directive onto its target group. Silently does nothing
// if the group name doesn't resolve — same tolerance as a sound entry that
// references a missing group.
static void ApplyFxDirective(AudioContext& ctx, const FxDirective& fx) {
    uint32_t gid = ctx.FindGroup(fx.group);
    if (gid == kInvalidGroup) return;

    if (fx.kind == 0) {
        auto* wrapper = new EchoEffectWrapper();
        libZFork::EchoParams p{};
        p.leftDelay      = fx.leftDelayMs;
        p.leftSourceVol  = fx.leftSrcVol;
        p.leftEchoVol    = fx.leftEchoVol;
        p.rightDelay     = fx.rightDelayMs;
        p.rightSourceVol = fx.rightSrcVol;
        p.rightEchoVol   = fx.rightEchoVol;
        wrapper->SetParams(p);
        ctx.SetGroupEffect(gid, kEchoEffectSlot, wrapper);
    } else {
        auto* wrapper = new ReverbEffectWrapper();
        libZFork::ReverbParams p{};
        p.roomSize = fx.roomSize;
        p.damping  = fx.damping;
        p.wet      = fx.wet;
        p.dry      = fx.dry;
        wrapper->SetParams(p);
        ctx.SetGroupEffect(gid, kReverbEffectSlot, wrapper);
    }
}

void SoundBankV1::AddEntry(AudioContext& ctx, const fs::path& baseDir,
                            const std::string& eventId, const std::string& pathUtf8,
                            const std::string& groupName, uint8_t typeByte,
                            float volume, float pan, int32_t priority, int32_t loopCount,
                            const std::vector<uint8_t>* embeddedData) {
    if (eventId.empty()) return;

    EventEntry e{};
    e.eventId = eventId;

    uint32_t groupId = ctx.FindGroup(groupName);
    e.groupId = (groupId == kInvalidGroup) ? 0u : groupId;

    SoundType st = (typeByte == 1) ? SoundType::Streaming : SoundType::PreDecoded;

    e.volume    = std::clamp(volume, 0.0f, 4.0f);
    e.pan       = std::clamp(pan, -1.0f, 1.0f);
    e.priority  = priority;
    e.loopCount = loopCount;

    if (embeddedData) {
        // pathUtf8 is just the original filename here — used only to derive
        // a decoder extension hint, no filesystem access involved. Each
        // embedded blob is loaded fresh (no SoundCache: the bytes already
        // live in RAM as part of the loaded bank, there's no disk I/O to
        // cache against, and different banks are unlikely to share an
        // identical blob).
        auto dot = pathUtf8.rfind('.');
        std::wstring ext = (dot == std::string::npos) ? L"" : Utf8ToWide(pathUtf8.substr(dot + 1));
        e.sound = Sound::LoadFromMemory(embeddedData->data(), embeddedData->size(), ext.c_str(), st);
    } else {
        // Convert the UTF-8 path field to a wide-character filesystem path.
        // Using Utf8ToWide avoids the MSVC pitfall where fs::path(std::string)
        // applies the system ANSI codepage instead of UTF-8, which silently
        // corrupts paths that contain non-ASCII characters.
        fs::path soundPath = fs::path(Utf8ToWide(pathUtf8));
        if (soundPath.is_relative()) soundPath = baseDir / soundPath;
        e.sound = SoundCache::Instance().GetOrLoad(soundPath.wstring(), st);
    }

    if (!e.sound) {
        ++skippedCount_;
        return;
    }

    entries_.push_back(std::move(e));
}

std::unique_ptr<SoundBankV1> SoundBankV1::LoadTextFile(AudioContext& ctx, const std::wstring& path) {
    fs::path bankPath(path);
    std::ifstream in(bankPath);
    if (!in.is_open()) return nullptr;

    auto bank = std::make_unique<SoundBankV1>();
    const fs::path baseDir = bankPath.parent_path();

    std::string line;
    while (std::getline(in, line)) {
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#') continue;

        if (t[0] == '@') {
            FxDirective fx;
            if (ParseFxDirective(t.substr(1), fx)) ApplyFxDirective(ctx, fx);
            continue;
        }

        auto fields = SplitSemicolon(t);
        if (fields.size() < 4) continue;
        if (fields[0].empty()) continue;

        float   volume    = 1.0f;
        float   pan       = 0.0f;
        int32_t priority  = 0;
        int32_t loopCount = 0;
        if (fields.size() > 4) volume    = ParseFloat(fields[4], 1.0f);
        if (fields.size() > 5) pan       = ParseFloat(fields[5], 0.0f);
        if (fields.size() > 6) priority  = ParseInt(fields[6], 0);
        if (fields.size() > 7) loopCount = ParseInt(fields[7], 0);

        std::string type = ToLower(fields[3]);
        uint8_t typeByte = (type == "stream" || type == "streaming") ? 1 : 0;

        bank->AddEntry(ctx, baseDir, fields[0], fields[1], fields[2], typeByte,
                       volume, pan, priority, loopCount);
    }

    // Return the bank even if all entries were skipped so the caller can
    // inspect SkippedCount() / EntryCount() for diagnostics.  Only return
    // nullptr when the file itself could not be opened.
    return bank;
}

std::unique_ptr<SoundBankV1> SoundBankV1::LoadBinaryFile(AudioContext& ctx, const std::wstring& path) {
    fs::path bankPath(path);
    std::ifstream in(bankPath, std::ios::binary);
    if (!in.is_open()) return nullptr;

    in.seekg(0, std::ios::end);
    const auto fileSizeTell = in.tellg();
    if (fileSizeTell < 0) return nullptr;
    const uint64_t fileSize = static_cast<uint64_t>(fileSizeTell);
    in.seekg(0, std::ios::beg);

    char magic[4] = {};
    in.read(magic, 4);
    if (!in || std::memcmp(magic, kBinaryMagic, 4) != 0) return nullptr;

    uint32_t version = 0;
    uint32_t count   = 0;
    if (!ReadPod(in, version) || !ReadPod(in, count)) return nullptr;
    if (version < 1 || version > kBinaryVersion) return nullptr; // unknown/future version
    if (count > kMaxBankEntries) return nullptr; // corrupt / hostile TOC

    auto bank = std::make_unique<SoundBankV1>();
    const fs::path baseDir = bankPath.parent_path();

    // Phase 1: read the TOC only. v4 embedded entries just remember where
    // their audio bytes live (blobOffset/blobSize into the DATA block at
    // the end); v3's older inline layout still has the bytes read here
    // immediately, since that's where the format put them.
    struct ParsedEntry {
        std::string eventId, pathUtf8, groupName;
        uint8_t typeByte = 0;
        float   volume = 1.0f, pan = 0.0f;
        int32_t priority = 0, loopCount = 0;
        bool    embedded = false;
        std::vector<uint8_t> inlineBlob; // v3 only
        uint64_t blobOffset = 0;         // v4 only
        uint32_t blobSize   = 0;         // v4 only
    };
    std::vector<ParsedEntry> parsed;
    parsed.reserve(count);

    bool truncated = false;
    for (uint32_t i = 0; i < count && !truncated; ++i) {
        ParsedEntry e;
        if (!ReadLenString(in, e.eventId))   { truncated = true; break; }
        if (!ReadLenString(in, e.pathUtf8))  { truncated = true; break; }
        if (!ReadLenString(in, e.groupName)) { truncated = true; break; }
        if (!ReadPod(in, e.typeByte))        { truncated = true; break; }
        if (!ReadPod(in, e.volume))          { truncated = true; break; }
        if (!ReadPod(in, e.pan))             { truncated = true; break; }
        if (!ReadPod(in, e.priority))        { truncated = true; break; }
        if (!ReadPod(in, e.loopCount))       { truncated = true; break; }

        if (version >= 3) {
            uint8_t embeddedByte = 0;
            if (!ReadPod(in, embeddedByte)) { truncated = true; break; }
            e.embedded = embeddedByte != 0;
            if (e.embedded) {
                if (version >= 4) {
                    if (!ReadPod(in, e.blobOffset)) { truncated = true; break; }
                    if (!ReadPod(in, e.blobSize))   { truncated = true; break; }
                    if (e.blobSize > kMaxEmbeddedBlob
                        || e.blobOffset > fileSize
                        || static_cast<uint64_t>(e.blobSize) > fileSize - e.blobOffset) {
                        truncated = true;
                        break;
                    }
                } else {
                    uint32_t blobSize = 0;
                    if (!ReadPod(in, blobSize)) { truncated = true; break; }
                    if (blobSize > kMaxEmbeddedBlob) { truncated = true; break; }
                    e.inlineBlob.resize(blobSize);
                    if (blobSize > 0) {
                        in.read(reinterpret_cast<char*>(e.inlineBlob.data()), blobSize);
                        if (!in) { truncated = true; break; }
                    }
                }
            }
        }
        parsed.push_back(std::move(e));
    }

    // v1 files end here — no fx section.
    if (!truncated && version >= 2) {
        uint32_t fxCount = 0;
        if (ReadPod(in, fxCount)) {
            for (uint32_t i = 0; i < fxCount; ++i) {
                FxDirective fx;
                uint8_t     kind = 0;
                if (!ReadLenString(in, fx.group)) break;
                if (!ReadPod(in, kind))           break;
                fx.kind = kind;
                if (kind == 0) {
                    if (!ReadPod(in, fx.leftDelayMs))  break;
                    if (!ReadPod(in, fx.leftSrcVol))   break;
                    if (!ReadPod(in, fx.leftEchoVol))  break;
                    if (!ReadPod(in, fx.rightDelayMs)) break;
                    if (!ReadPod(in, fx.rightSrcVol))  break;
                    if (!ReadPod(in, fx.rightEchoVol)) break;
                } else {
                    if (!ReadPod(in, fx.roomSize)) break;
                    if (!ReadPod(in, fx.damping))  break;
                    if (!ReadPod(in, fx.wet))      break;
                    if (!ReadPod(in, fx.dry))      break;
                }
                ApplyFxDirective(ctx, fx);
            }
        }
    }

    // Phase 2: build each entry, fetching v4 embedded blobs on demand via a
    // seek into the DATA block (one seek + read per embedded entry — no
    // need to have read through anything else first).
    std::vector<uint8_t> blobBuf;
    for (auto& e : parsed) {
        const std::vector<uint8_t>* blobPtr = nullptr;
        if (e.embedded) {
            if (version < 4) {
                blobPtr = &e.inlineBlob; // v3: already read in phase 1
            } else {
                // Re-validate against file size (TOC may have been truncated mid-read).
                if (e.blobSize > kMaxEmbeddedBlob
                    || e.blobOffset > fileSize
                    || static_cast<uint64_t>(e.blobSize) > fileSize - e.blobOffset) {
                    ++bank->skippedCount_;
                    continue;
                }
                blobBuf.assign(e.blobSize, 0);
                if (e.blobSize > 0) {
                    in.seekg(static_cast<std::streamoff>(e.blobOffset), std::ios::beg);
                    in.read(reinterpret_cast<char*>(blobBuf.data()), e.blobSize);
                    if (!in || static_cast<uint32_t>(in.gcount()) != e.blobSize) {
                        ++bank->skippedCount_;
                        continue;
                    }
                }
                blobPtr = &blobBuf;
            }
        }
        bank->AddEntry(ctx, baseDir, e.eventId, e.pathUtf8, e.groupName, e.typeByte,
                       e.volume, e.pan, e.priority, e.loopCount, blobPtr);
    }

    return bank;
}

std::unique_ptr<SoundBankV1> SoundBankV1::LoadFromFile(AudioContext& ctx, const wchar_t* path) {
    if (!path) return nullptr;

    // Peek at the first 4 bytes to auto-detect the binary format; anything
    // else (including a file too short to hold a magic) is treated as text.
    {
        std::ifstream probe(fs::path(path), std::ios::binary);
        if (!probe.is_open()) return nullptr;
        char magic[4] = {};
        probe.read(magic, 4);
        if (probe.gcount() == 4 && std::memcmp(magic, kBinaryMagic, 4) == 0) {
            return LoadBinaryFile(ctx, path);
        }
    }
    return LoadTextFile(ctx, path);
}

bool SoundBankV1::ConvertTextToBinary(const wchar_t* textPath, const wchar_t* binaryPath,
                                      bool embedAudioFiles) {
    if (!textPath || !binaryPath) return false;

    fs::path textFsPath(textPath);
    std::ifstream in{textFsPath};
    if (!in.is_open()) return false;

    std::ofstream out(fs::path(binaryPath), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;

    const fs::path baseDir = textFsPath.parent_path();

    struct RawEntry {
        std::string eventId, path, group;
        uint8_t     typeByte = 0;
        float       volume = 1.0f, pan = 0.0f;
        int32_t     priority = 0, loopCount = 0;
        std::vector<uint8_t> blob; // non-empty iff embedded
    };
    std::vector<RawEntry>    raw;
    std::vector<FxDirective> fx;

    std::string line;
    while (std::getline(in, line)) {
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#') continue;

        if (t[0] == '@') {
            FxDirective d;
            if (ParseFxDirective(t.substr(1), d)) fx.push_back(std::move(d));
            continue;
        }

        auto fields = SplitSemicolon(t);
        if (fields.size() < 4 || fields[0].empty()) continue;

        RawEntry r;
        r.eventId = fields[0];
        r.path    = fields[1];
        r.group   = fields[2];
        std::string type = ToLower(fields[3]);
        r.typeByte = (type == "stream" || type == "streaming") ? 1 : 0;
        if (fields.size() > 4) r.volume    = ParseFloat(fields[4], 1.0f);
        if (fields.size() > 5) r.pan       = ParseFloat(fields[5], 0.0f);
        if (fields.size() > 6) r.priority  = ParseInt(fields[6], 0);
        if (fields.size() > 7) r.loopCount = ParseInt(fields[7], 0);

        if (embedAudioFiles) {
            fs::path soundPath = fs::path(Utf8ToWide(r.path));
            if (soundPath.is_relative()) soundPath = baseDir / soundPath;
            std::ifstream audioIn(soundPath, std::ios::binary);
            if (audioIn) {
                audioIn.seekg(0, std::ios::end);
                auto size = audioIn.tellg();
                audioIn.seekg(0, std::ios::beg);
                if (size > 0) {
                    r.blob.resize(static_cast<size_t>(size));
                    audioIn.read(reinterpret_cast<char*>(r.blob.data()), size);
                    if (!audioIn) r.blob.clear(); // read failed — fall back to external reference
                }
            }
            // audioIn failing to open (or empty file) just leaves r.blob
            // empty, i.e. this entry stays an external path reference below.
        }

        raw.push_back(std::move(r));
    }

    // Build the TOC (+ fx section) into an in-memory buffer first, so we can
    // learn its exact byte size before deciding where the DATA block (the
    // embedded blobs, appended after) starts — each embedded entry's offset
    // is patched into the buffer once that's known, then the buffer and the
    // blobs are written to `out` in one straight pass. No fixed-size hackery
    // needed: the buffer's own bytes are the source of truth for its size.
    std::ostringstream toc(std::ios::binary);
    struct PendingBlob {
        size_t offsetFieldPos; // byte position within `toc`'s buffer to patch
        const std::vector<uint8_t>* blob;
    };
    std::vector<PendingBlob> pending;
    pending.reserve(raw.size());

    for (const auto& r : raw) {
        WriteLenString(toc, r.eventId);
        WriteLenString(toc, r.path);
        WriteLenString(toc, r.group);
        WritePod(toc, r.typeByte);
        WritePod(toc, r.volume);
        WritePod(toc, r.pan);
        WritePod(toc, r.priority);
        WritePod(toc, r.loopCount);

        const uint8_t embeddedByte = r.blob.empty() ? 0 : 1;
        WritePod(toc, embeddedByte);
        if (embeddedByte) {
            const size_t offsetFieldPos = static_cast<size_t>(toc.tellp());
            WritePod(toc, static_cast<uint64_t>(0)); // blobOffset placeholder, patched below
            WritePod(toc, static_cast<uint32_t>(r.blob.size()));
            pending.push_back({offsetFieldPos, &r.blob});
        }
    }

    WritePod(toc, static_cast<uint32_t>(fx.size()));
    for (const auto& d : fx) {
        WriteLenString(toc, d.group);
        WritePod(toc, d.kind);
        if (d.kind == 0) {
            WritePod(toc, d.leftDelayMs);
            WritePod(toc, d.leftSrcVol);
            WritePod(toc, d.leftEchoVol);
            WritePod(toc, d.rightDelayMs);
            WritePod(toc, d.rightSrcVol);
            WritePod(toc, d.rightEchoVol);
        } else {
            WritePod(toc, d.roomSize);
            WritePod(toc, d.damping);
            WritePod(toc, d.wet);
            WritePod(toc, d.dry);
        }
    }

    std::string tocBytes = toc.str();

    // Fixed header written directly to `out`, ahead of `tocBytes`: magic(4) +
    // version(4) + entryCount(4).
    constexpr size_t kFixedHeaderSize = 4 + 4 + 4;
    const uint64_t dataBlockStart = kFixedHeaderSize + tocBytes.size();

    uint64_t runningOffset = 0;
    for (const auto& pb : pending) {
        const uint64_t absOffset = dataBlockStart + runningOffset;
        std::memcpy(&tocBytes[pb.offsetFieldPos], &absOffset, sizeof(absOffset));
        runningOffset += pb.blob->size();
    }

    out.write(kBinaryMagic, 4);
    WritePod(out, kBinaryVersion);
    WritePod(out, static_cast<uint32_t>(raw.size()));
    out.write(tocBytes.data(), static_cast<std::streamsize>(tocBytes.size()));

    for (const auto& pb : pending) {
        out.write(reinterpret_cast<const char*>(pb.blob->data()),
                  static_cast<std::streamsize>(pb.blob->size()));
    }

    return static_cast<bool>(out);
}

const SoundBankV1::EventEntry* SoundBankV1::Find(std::string_view eventId) const {
    for (const auto& e : entries_) {
        if (e.eventId == eventId) return &e;
    }
    return nullptr;
}

} // namespace libZFork::engine
