#pragma once
#include "engine/sound.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace libZFork::engine {

class AudioContext;

// Soundbank (API V1). Supports two on-disk formats, auto-detected from the
// file's first 4 bytes:
//
// 1) Text format (semicolon-separated, one entry per line):
//      eventId;path;group;type;volume;pan;priority;loop
//    Example:
//      ui_click;sounds/button1.ogg;ui;predecoded;1.0;0.0;5;0
//      music_loop;dungeon_ambient_1.ogg;music;streaming;0.6;0.0;0;-1
//    Lines starting with '#' (after trimming) are comments.
//
//    Lines starting with '@' are group-effect directives instead of sound
//    entries — install an echo or reverb on a group's bus, applied once when
//    the bank loads (every voice routed through that group is processed by
//    it automatically; this mirrors zfork2_SetGroupEcho/SetGroupReverb).
//    Effects are per-GROUP, not per-sound: a bus effect is shared by every
//    voice on that group and processed once per mix block, which is why
//    it's cheap — a per-voice version would mean one echo/reverb instance
//    *per playing sound*, defeating that. Route sounds that want the same
//    treatment onto the same group (as ui_click/button_on/... already do
//    via the "ui" group) and put the directive on that group.
//      @echo;group;leftDelayMs;leftSrcVol;leftEchoVol;rightDelayMs;rightSrcVol;rightEchoVol
//      @reverb;group;roomSize;damping;wet;dry
//    Example:
//      @echo;ui;180;100;45;180;100;45
//      @reverb;music;0.65;0.5;0.35;0.7
//    An unknown group name is silently skipped (same tolerance as an entry
//    referencing a missing group).
//
// 2) Binary format ("ZFBK"), a compact, comment-free encoding of the same
//    data — faster to load and immune to locale/encoding issues. Layout is
//    TOC-then-data (like a small archive/pak file): every entry's metadata
//    sits together up front, actual embedded audio bytes are appended as one
//    block at the very end — so listing/inspecting a bank (e.g. a tool
//    showing what's inside) never has to read past the TOC, and reading one
//    embedded sound is a single seek + read instead of scanning through
//    other sounds' data to find it.
//      offset 0: char[4]   magic "ZFBK"
//      offset 4: uint32_t  version (currently 4)
//      offset 8: uint32_t  entryCount
//      then, repeated entryCount times (TOC — sound entries, no audio bytes):
//        uint16_t len; char eventId[len];   // UTF-8, not null-terminated
//        uint16_t len; char path[len];      // UTF-8: relative path (external,
//                                            // the default), or just the
//                                            // original filename (embedded —
//                                            // kept only as a decoder
//                                            // extension hint, e.g. "click.ogg")
//        uint16_t len; char group[len];     // UTF-8 group name
//        uint8_t  type;                     // 0 = predecoded, 1 = streaming
//        float    volume;
//        float    pan;
//        int32_t  priority;
//        int32_t  loopCount;
//        uint8_t  embedded;                 // v3+: 0 = external path (above),
//                                            // 1 = this entry's audio lives in
//                                            // the DATA block (see below).
//                                            // Absent in v1/v2 — treated as 0.
//        if embedded == 1 (v4+): uint64_t blobOffset; uint32_t blobSize;
//          — blobOffset is an ABSOLUTE byte offset into this same file (into
//          the DATA block described below), not a length-prefixed inline
//          blob. (v3 used an inline uint32_t blobSize + immediate raw bytes
//          right here instead — still readable, just no longer written.)
//      then: uint32_t fxCount, then repeated fxCount times (group effects —
//      still no audio bytes, small enough to keep in the TOC region):
//        uint16_t len; char group[len];     // UTF-8 group name
//        uint8_t  kind;                     // 0 = echo, 1 = reverb
//        if kind == 0: int32_t leftDelayMs, leftSrcVol, leftEchoVol,
//                              rightDelayMs, rightSrcVol, rightEchoVol;
//        if kind == 1: float roomSize, damping, wet, dry;
//      then (v4+ only): DATA block — every embedded entry's raw file bytes,
//      concatenated in entry order, each referenced by its TOC entry's
//      (blobOffset, blobSize) above. Nothing else reads or writes past this
//      point sequentially; it exists purely to be seeked into on demand.
//    All multi-byte integers are little-endian (native x86/x64 layout).
//    Use ConvertTextToBinary() to compile a text bank into this format;
//    pass embedAudioFiles=true to produce a single self-contained .bank with
//    every sound file's bytes packed into the DATA block (no external
//    sounds/ folder to ship). Older files still load: v1 has no fx section,
//    v2 has no embedded byte, v3 has inline (not TOC-referenced) blobs.
//    Embedding is compile-time only — the .txt source always references
//    external paths; ConvertTextToBinary() is what reads and inlines them.
class SoundBankV1 final {
public:
    struct EventEntry {
        std::string            eventId;
        std::shared_ptr<Sound> sound;
        uint32_t               groupId   = 0;
        float                  volume    = 1.0f;
        float                  pan       = 0.0f;
        int32_t                priority  = 0;
        int32_t                loopCount = 0;
    };

    // Loads the bank, auto-detecting text vs. binary format. Returns nullptr
    // if the file cannot be opened or no entries could be loaded. Check
    // EntryCount() to verify partial loads.
    static std::unique_ptr<SoundBankV1> LoadFromFile(AudioContext& ctx, const wchar_t* path);

    // Compiles a text-format bank file into the binary format described
    // above. Does not require an AudioContext. Returns false if textPath
    // cannot be opened or binaryPath cannot be written.
    //
    // embedAudioFiles=false (default): only re-serializes the parsed fields
    // (paths stay external references) — no sound files are read.
    // embedAudioFiles=true: also reads each referenced sound file (resolved
    // relative to textPath's directory, same as loading would) and embeds
    // its raw bytes into the .bank, producing a single self-contained file.
    // A file that can't be read is left as an external reference instead of
    // failing the whole compile (mirrors LoadFromFile's per-entry tolerance).
    static bool ConvertTextToBinary(const wchar_t* textPath, const wchar_t* binaryPath,
                                    bool embedAudioFiles = false);

    // Looks up an event by id. Thread-safe after construction (read-only).
    const EventEntry* Find(std::string_view eventId) const;

    // Enumeration — for tooling/UI that wants to show what's actually in a
    // loaded bank instead of assuming fixed, hand-known event ids (e.g. a
    // dynamically generated bank from zfork2_bank_builder). Returns nullptr
    // for an out-of-range index.
    const EventEntry* EntryAt(std::size_t index) const noexcept {
        return index < entries_.size() ? &entries_[index] : nullptr;
    }

    // Number of successfully loaded entries.
    [[nodiscard]] std::size_t EntryCount()   const noexcept { return entries_.size(); }

    // Number of entries that were parsed but whose sound files could not be
    // loaded (missing file, unsupported format, ...).
    [[nodiscard]] uint32_t SkippedCount() const noexcept { return skippedCount_; }

private:
    static std::unique_ptr<SoundBankV1> LoadTextFile(AudioContext& ctx, const std::wstring& path);
    static std::unique_ptr<SoundBankV1> LoadBinaryFile(AudioContext& ctx, const std::wstring& path);

    // `embeddedData` non-null means `pathUtf8` is just a decoder extension
    // hint (e.g. "click.ogg"), not a path to resolve against baseDir — the
    // sound is loaded from those bytes via Sound::LoadFromMemory() instead
    // of going through SoundCache/disk I/O.
    void AddEntry(AudioContext& ctx, const std::filesystem::path& baseDir,
                  const std::string& eventId, const std::string& pathUtf8,
                  const std::string& groupName, uint8_t typeByte,
                  float volume, float pan, int32_t priority, int32_t loopCount,
                  const std::vector<uint8_t>* embeddedData = nullptr);

    std::vector<EventEntry> entries_;
    uint32_t                skippedCount_ = 0;
};

} // namespace libZFork::engine
