/**
 * libZFork — Format Demo
 *
 * Demonstrates the three new decoder formats added to libZFork:
 *   AIFF    — Apple Interchange File Format (big-endian PCM, .aif/.aiff)
 *   FLAC-OGG — FLAC audio encapsulated in an OGG container (.oga)
 *   Opus    — Xiph Opus codec in OGG container (.opus), always 48 kHz
 *
 * For each file the demo:
 *   1. Opens the file with Autodetect
 *   2. Prints stream info (sample rate, channels, bitrate, duration)
 *   3. Prints ID3 / Vorbis comment tags if available
 *   4. Plays the first 8 seconds (or full track if shorter)
 *   5. Shows a live progress bar driven by the PositionAsync callback
 *
 * Usage:
 *   format_demo <file1> [<file2> ...] [--no-play]
 *
 * Supported extensions: .aif .aiff .oga .opus
 * Any other format supported by libZFork also works (--no-play just shows info).
 */

#define NOMINMAX
#include <libzfork/libzfork.h>
#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>

using namespace libZFork;

// ── Console color helpers ──────────────────────────────────────────────────────

static HANDLE g_hCon = INVALID_HANDLE_VALUE;

static void SetColor(WORD attr) {
    if (g_hCon != INVALID_HANDLE_VALUE)
        SetConsoleTextAttribute(g_hCon, attr);
}
static void ResetColor() { SetColor(0x07); }

// ── Format name lookup ─────────────────────────────────────────────────────────

static const char* FormatName(StreamFormat f) {
    switch (f) {
        case StreamFormat::Mp3:     return "MP3";
        case StreamFormat::Ogg:     return "OGG Vorbis";
        case StreamFormat::Wav:     return "WAV/PCM";
        case StreamFormat::PCM:     return "Raw PCM";
        case StreamFormat::FLAC:    return "FLAC";
        case StreamFormat::FLACOgg: return "FLAC-in-OGG";
        case StreamFormat::AacADTS: return "AAC/ADTS";
        case StreamFormat::Aiff:    return "AIFF";
        case StreamFormat::Opus:    return "Opus";
        default:                    return "Unknown";
    }
}

// Detect format name from file extension (for display only).
static const char* FormatNameFromExt(const wchar_t* path) {
    const wchar_t* dot = wcsrchr(path, L'.');
    if (!dot) return "Unknown";
    if (!_wcsicmp(dot, L".aif")  || !_wcsicmp(dot, L".aiff")) return "AIFF";
    if (!_wcsicmp(dot, L".oga"))                                return "FLAC-in-OGG";
    if (!_wcsicmp(dot, L".opus"))                               return "Opus";
    if (!_wcsicmp(dot, L".mp3"))                                return "MP3";
    if (!_wcsicmp(dot, L".ogg"))                                return "OGG Vorbis";
    if (!_wcsicmp(dot, L".flac"))                               return "FLAC";
    if (!_wcsicmp(dot, L".wav")  || !_wcsicmp(dot, L".wave"))  return "WAV/PCM";
    if (!_wcsicmp(dot, L".aac"))                                return "AAC/ADTS";
    return "Unknown";
}

// ── Progress bar ───────────────────────────────────────────────────────────────

static std::atomic<uint32_t> g_posMs   { 0 };
static std::atomic<uint32_t> g_totalMs { 0 };

static void DrawProgress() {
    uint32_t pos   = g_posMs.load();
    uint32_t total = g_totalMs.load();
    double   ratio = (total > 0) ? std::min(1.0, (double)pos / total) : 0.0;
    int      filled = static_cast<int>(ratio * 40);

    uint32_t posS   = pos   / 1000;
    uint32_t totS   = total / 1000;

    std::printf("\r  [");
    SetColor(0x0A);
    for (int i = 0; i < 40; ++i) std::printf(i < filled ? "\xDB" : "\xB0");
    ResetColor();
    std::printf("]  %02u:%02u / %02u:%02u  %5.1f%%",
                posS / 60, posS % 60, totS / 60, totS % 60, ratio * 100.0);
    std::fflush(stdout);
}

// ── Callback handler ───────────────────────────────────────────────────────────
// Receives all player events. PositionAsync updates the global position atoms
// so the main thread can redraw the progress bar without any locking.

static int ZFORK_CALL PlayerCB(void*, void*, CallbackMessage msg,
                                uint32_t p1, uint32_t p2) {
    switch (msg) {
        case CallbackMessage::PositionAsync:
            g_posMs.store(p1);
            g_totalMs.store(p2);
            break;
        case CallbackMessage::StopAsync:
        case CallbackMessage::PlayAsync:
        case CallbackMessage::PauseAsync:
        case CallbackMessage::ResumeAsync:
            break;
        default:
            break;
    }
    return 0;
}

// ── Print stream info ──────────────────────────────────────────────────────────

static void PrintInfo(ZFork* p, const wchar_t* path) {
    StreamInfo info{};
    p->GetStreamInfo(0, info);

    SetColor(0x0E);
    std::printf("  Format      : %s\n", FormatNameFromExt(path));
    ResetColor();
    std::printf("  Sample rate : %d Hz\n",   info.samplingRate);
    std::printf("  Channels    : %d  (%s)\n", info.channelNumber,
                info.channelNumber == 1 ? "Mono" : info.channelNumber == 2 ? "Stereo" : "Multi");
    if (info.bitrate > 0)
        std::printf("  Bitrate     : %d kbps%s\n", info.bitrate, info.vbr ? " (VBR)" : "");
    std::printf("  Duration    : %02u:%02u:%02u.%03u\n",
                info.length.hms.hour, info.length.hms.minute,
                info.length.hms.second, info.length.hms.millisecond);
    std::printf("  Samples     : %llu\n", (unsigned long long)info.length.samples);

    // Tags (best effort — not all formats carry ID3 tags)
    ID3InfoEx tag{};
    if (p->LoadID3Ex(0, tag)) {
        bool anyTag = tag.title[0] || tag.artist[0] || tag.album[0];
        if (anyTag) {
            std::printf("\n  Tags:\n");
            if (tag.title[0])  std::printf("    Title  : %s\n", tag.title);
            if (tag.artist[0]) std::printf("    Artist : %s\n", tag.artist);
            if (tag.album[0])  std::printf("    Album  : %s\n", tag.album);
            if (tag.year[0])   std::printf("    Year   : %s\n", tag.year);
            if (tag.bpm > 0)   std::printf("    BPM    : %d\n", tag.bpm);
        }
    }
}

// ── Play one file ──────────────────────────────────────────────────────────────

static void PlayFile(const wchar_t* path, bool doPlay) {
    SetColor(0x0F);
    std::printf("\n══════════════════════════════════════════════\n");
    std::printf("  File: ");
    SetColor(0x0B);
    std::fputws(path, stdout);
    std::printf("\n");
    ResetColor();
    std::printf("══════════════════════════════════════════════\n");

    ZFork* p = CreateZFork();
    if (!p) { std::printf("  [ERROR] CreateZFork() failed\n"); return; }

    p->SetCallback(PlayerCB, nullptr);

    if (!p->OpenFile(path, StreamFormat::Autodetect)) {
        SetColor(0x0C);
        std::printf("  [ERROR] Open failed: %s\n", p->GetError());
        ResetColor();
        DestroyZFork(p);
        return;
    }

    PrintInfo(p, path);

    if (!doPlay) {
        DestroyZFork(p);
        return;
    }

    // Play up to 8 seconds, or full track if shorter
    StreamInfo info{};
    p->GetStreamInfo(0, info);
    uint32_t playMs = std::min(8000u, info.length.ms > 0 ? info.length.ms : 8000u);

    g_posMs.store(0);
    g_totalMs.store(info.length.ms);

    p->SetMasterVolume(85, 85);
    if (!p->Play()) {
        SetColor(0x0C);
        std::printf("  [ERROR] Play() failed: %s\n", p->GetError());
        ResetColor();
        DestroyZFork(p);
        return;
    }

    std::printf("\n  Playing (press Q to skip)...\n");

    DWORD t0    = GetTickCount();
    bool  aborted = false;

    while (true) {
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 'q' || ch == 'Q') { aborted = true; break; }
        }

        DrawProgress();

        auto st = p->GetStatus();
        if (!st.playing && !st.paused) break;

        if ((GetTickCount() - t0) >= playMs) break;

        Sleep(80);
    }

    p->Stop();
    std::printf("\n  %s\n", aborted ? "Skipped." : "Done.");

    DestroyZFork(p);
}

// ── Entry point ────────────────────────────────────────────────────────────────

int wmain(int argc, wchar_t* argv[]) {
    // UTF-8 output for tags
    SetConsoleOutputCP(CP_UTF8);
    g_hCon = GetStdHandle(STD_OUTPUT_HANDLE);

    // Collect files and flags
    std::vector<const wchar_t*> files;
    bool noPlay = false;

    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--no-play")) { noPlay = true; continue; }
        files.push_back(argv[i]);
    }

    if (files.empty()) {
        SetColor(0x0F);
        std::printf("libZFork — Format Demo\n\n");
        ResetColor();
        std::printf("Demonstrates AIFF, FLAC-OGG and Opus decoding.\n\n");
        std::printf("Usage: format_demo <file1> [<file2> ...] [--no-play]\n\n");
        std::printf("Supported extensions:\n");
        std::printf("  .aif / .aiff  AIFF  (Apple, big-endian PCM)\n");
        std::printf("  .oga          FLAC encapsulated in OGG container\n");
        std::printf("  .opus         Opus  (always 48 kHz)\n");
        std::printf("  .mp3 .ogg .flac .wav .aac  (all other formats also work)\n\n");
        std::printf("--no-play  Show info only, do not play audio\n");
        return 0;
    }

    SetColor(0x0F);
    std::printf("\n╔══════════════════════════════════════════════╗\n");
    std::printf("║         libZFork  Format Demo                ║\n");
    std::printf("╚══════════════════════════════════════════════╝\n");
    ResetColor();
    std::printf("  Files   : %zu\n", files.size());
    std::printf("  Playback: %s\n", noPlay ? "disabled (--no-play)" : "8 s preview per file");
    std::printf("  Keys    : Q = skip current file\n");

    for (auto* path : files)
        PlayFile(path, !noPlay);

    SetColor(0x0F);
    std::printf("\n══════════════════════════════════════════════\n");
    std::printf("  All files processed.\n");
    std::printf("══════════════════════════════════════════════\n\n");
    ResetColor();
    return 0;
}
