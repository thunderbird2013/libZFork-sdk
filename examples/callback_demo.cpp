/**
 * libZFork — Callback Demo
 *
 * Demonstrates the complete PlayerCallback system.
 *
 * Every CallbackMessage value is handled and printed to the console so you can
 * see exactly when each event fires during normal playback.
 *
 * Events shown:
 *   PlayAsync              — fired when Play() starts
 *   PauseAsync             — fired when Pause() is called
 *   ResumeAsync            — fired when Resume() is called
 *   StopAsync              — fired when the track ends or Stop() is called
 *   NextSongAsync          — fired when the playlist advances
 *   EnterLoopAsync         — fired at the start of each loop iteration
 *   ExitLoopAsync          — fired after the last loop iteration
 *   EnterVolumeSlideAsync  — fired when SlideVolume() starts
 *   ExitVolumeSlideAsync   — fired when SlideVolume() finishes
 *   StreamBufferDoneAsync  — fired each time a waveOut buffer is submitted
 *   PositionAsync          — fired ~4 times/second with current+total ms
 *                            (param1 = current ms, param2 = total ms)
 *
 * Usage:
 *   callback_demo <audio_file> [--loop N] [--slide] [--quiet]
 *
 *   --loop N   Play N times (demonstrates loop events)
 *   --slide    Trigger a volume slide during playback (demonstrates slide events)
 *   --quiet    Suppress StreamBufferDoneAsync spam (fires every ~20 ms)
 */

#define NOMINMAX
#include <libzfork/libzfork.h>
#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <string>
#include <atomic>
#include <algorithm>

using namespace libZFork;

// ── Console helpers ────────────────────────────────────────────────────────────

static HANDLE           g_hCon     = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_conLock;   // serializes all console I/O across threads
static SHORT            g_progressRow = 0;  // fixed row for the progress bar

static void SetColor(WORD a) {
    if (g_hCon != INVALID_HANDLE_VALUE) SetConsoleTextAttribute(g_hCon, a);
}
static void ResetColor() { SetColor(0x07); }

// Print a line in the scrolling event area (above the progress bar).
// Must be called with g_conLock held.
static void PrintEventLine(const char* fmt, ...) {
    // Move cursor to the progress row and scroll one line up by
    // positioning just above it, then let \n push everything up.
    CONSOLE_SCREEN_BUFFER_INFO cbi{};
    GetConsoleScreenBufferInfo(g_hCon, &cbi);
    // Place cursor on the line just above the progress row so the
    // upcoming \n keeps event lines in the scrolling area.
    COORD c{ 0, static_cast<SHORT>(g_progressRow - 1) };
    SetConsoleCursorPosition(g_hCon, c);
    std::printf("\n");   // scroll event area by one line

    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);

    // Restore cursor to the progress row
    c.Y = g_progressRow;
    SetConsoleCursorPosition(g_hCon, c);
}

// ── Shared state between callback and main thread ─────────────────────────────

static std::atomic<uint32_t> g_posMs      { 0 };
static std::atomic<uint32_t> g_totalMs    { 0 };
static std::atomic<int>      g_eventCount { 0 };
static bool g_quietBuffers = true;   // StreamBufferDoneAsync suppressed by default
                                     // use --verbose to show them

// ── Callback message name ──────────────────────────────────────────────────────

static const char* MsgName(CallbackMessage m) {
    switch (m) {
        case CallbackMessage::PlayAsync:              return "PlayAsync";
        case CallbackMessage::PauseAsync:             return "PauseAsync";
        case CallbackMessage::ResumeAsync:            return "ResumeAsync";
        case CallbackMessage::StopAsync:              return "StopAsync";
        case CallbackMessage::NextSongAsync:          return "NextSongAsync";
        case CallbackMessage::EnterLoopAsync:         return "EnterLoopAsync";
        case CallbackMessage::ExitLoopAsync:          return "ExitLoopAsync";
        case CallbackMessage::EnterVolumeSlideAsync:  return "EnterVolumeSlideAsync";
        case CallbackMessage::ExitVolumeSlideAsync:   return "ExitVolumeSlideAsync";
        case CallbackMessage::StreamBufferDoneAsync:  return "StreamBufferDoneAsync";
        case CallbackMessage::StreamNeedMoreDataAsync:return "StreamNeedMoreDataAsync";
        case CallbackMessage::PositionAsync:          return "PositionAsync";
        default:                                      return "Unknown";
    }
}

// Color per message category
static WORD MsgColor(CallbackMessage m) {
    switch (m) {
        case CallbackMessage::PlayAsync:             return 0x0A; // green
        case CallbackMessage::StopAsync:             return 0x0C; // red
        case CallbackMessage::PauseAsync:            return 0x0E; // yellow
        case CallbackMessage::ResumeAsync:           return 0x0A; // green
        case CallbackMessage::PositionAsync:         return 0x08; // dark grey
        case CallbackMessage::StreamBufferDoneAsync: return 0x08; // dark grey
        case CallbackMessage::EnterLoopAsync:
        case CallbackMessage::ExitLoopAsync:         return 0x0B; // cyan
        case CallbackMessage::EnterVolumeSlideAsync:
        case CallbackMessage::ExitVolumeSlideAsync:  return 0x0D; // magenta
        case CallbackMessage::NextSongAsync:         return 0x0F; // white
        default:                                     return 0x07;
    }
}

// ── The callback function ──────────────────────────────────────────────────────
// NOTE: This is called from the player's decode thread (Async variants) or
//       the slide thread. Keep it fast and avoid blocking operations.

static int ZFORK_CALL OnPlayerEvent(void*           /*instance*/,
                                    void*           /*userData*/,
                                    CallbackMessage msg,
                                    uint32_t        param1,
                                    uint32_t        param2)
{
    ++g_eventCount;

    // Update position state (used by the main thread for the progress bar)
    if (msg == CallbackMessage::PositionAsync) {
        g_posMs.store(param1);
        g_totalMs.store(param2);
        return 0;
    }

    // Suppress buffer events unless --verbose was passed
    if (g_quietBuffers && msg == CallbackMessage::StreamBufferDoneAsync)
        return 0;

    // ── Print event — serialized via g_conLock ────────────────────────────────
    EnterCriticalSection(&g_conLock);

    SetColor(MsgColor(msg));
    char detail[80] = {};
    switch (msg) {
        case CallbackMessage::NextSongAsync:
            std::snprintf(detail, sizeof(detail), "  song index=%u", param1);
            break;
        case CallbackMessage::EnterLoopAsync:
        case CallbackMessage::ExitLoopAsync:
            std::snprintf(detail, sizeof(detail), "  remaining loops=%u", param1);
            break;
        case CallbackMessage::EnterVolumeSlideAsync:
        case CallbackMessage::ExitVolumeSlideAsync:
            std::snprintf(detail, sizeof(detail), "  vol L=%u  R=%u", param1, param2);
            break;
        case CallbackMessage::StreamBufferDoneAsync:
            std::snprintf(detail, sizeof(detail), "  (buffer → waveOut)");
            break;
        default:
            if (param1 || param2)
                std::snprintf(detail, sizeof(detail), "  p1=%u  p2=%u", param1, param2);
            break;
    }
    PrintEventLine("  [EVENT #%4d]  %-28s%s",
                   g_eventCount.load(), MsgName(msg), detail);
    ResetColor();

    LeaveCriticalSection(&g_conLock);
    return 0;
}

// ── Draw progress bar on the fixed bottom row ─────────────────────────────────

static void DrawProgress(int vol) {
    uint32_t pos    = g_posMs.load();
    uint32_t total  = g_totalMs.load();
    double   ratio  = (total > 0) ? std::min(1.0, (double)pos / total) : 0.0;
    int      filled = static_cast<int>(ratio * 36);

    EnterCriticalSection(&g_conLock);

    // Overwrite the dedicated progress row in-place
    COORD c{ 0, g_progressRow };
    SetConsoleCursorPosition(g_hCon, c);

    std::printf("  [");
    SetColor(0x0A);
    for (int i = 0; i < 36; ++i) std::printf(i < filled ? "\xDB" : "\xB0");
    ResetColor();
    std::printf("]  %02u:%02u / %02u:%02u  Vol:%3d%%  ",
                (pos / 1000) / 60, (pos / 1000) % 60,
                (total / 1000) / 60, (total / 1000) % 60,
                vol);
    std::fflush(stdout);

    LeaveCriticalSection(&g_conLock);
}

// ── Entry point ────────────────────────────────────────────────────────────────

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    g_hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    InitializeCriticalSection(&g_conLock);

    // ── Parse arguments ────────────────────────────────────────────────────────
    const wchar_t* filePath = nullptr;
    int  loopCount = 0;
    bool doSlide   = false;

    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--loop") && i + 1 < argc) {
            loopCount = _wtoi(argv[++i]);
        } else if (!wcscmp(argv[i], L"--slide")) {
            doSlide = true;
        } else if (!wcscmp(argv[i], L"--quiet")) {
            g_quietBuffers = true;   // already default, kept for compatibility
        } else if (!wcscmp(argv[i], L"--verbose")) {
            g_quietBuffers = false;  // show StreamBufferDoneAsync
        } else if (argv[i][0] != L'-') {
            filePath = argv[i];
        }
    }

    if (!filePath) {
        std::printf("Usage: callback_demo <audio_file> [--loop N] [--slide] [--verbose]\n\n");
        std::printf("  --loop N    play N times (shows EnterLoopAsync / ExitLoopAsync)\n");
        std::printf("  --slide     trigger a volume slide (shows slide events)\n");
        std::printf("  --verbose   show StreamBufferDoneAsync (fires every ~20 ms)\n\n");
        std::printf("Keys during playback:\n");
        std::printf("  P   Pause / Resume\n");
        std::printf("  S   Trigger volume slide (100→30→100 over 2 s)\n");
        std::printf("  Q   Stop and quit\n");
        return 1;
    }

    // ── Create player ──────────────────────────────────────────────────────────
    ZFork* player = CreateZFork();
    if (!player) { std::printf("CreateZFork() failed\n"); return 1; }

    // Register callback BEFORE opening the file
    player->SetCallback(OnPlayerEvent, nullptr);

    if (!player->OpenFile(filePath, StreamFormat::Autodetect)) {
        std::printf("OpenFile failed: %s\n", player->GetError());
        DestroyZFork(player);
        return 1;
    }

    StreamInfo info{};
    player->GetStreamInfo(0, info);
    g_totalMs.store(info.length.ms);

    // ── Print header ───────────────────────────────────────────────────────────
    SetColor(0x0F);
    std::printf("\n╔══════════════════════════════════════════════╗\n");
    std::printf("║        libZFork  Callback Demo               ║\n");
    std::printf("╚══════════════════════════════════════════════╝\n");
    ResetColor();
    std::printf("  File   : ");
    std::fputws(filePath, stdout);
    std::printf("\n");
    std::printf("  Length : %02u:%02u:%02u\n",
                info.length.hms.hour, info.length.hms.minute, info.length.hms.second);
    std::printf("  Rate   : %d Hz  Ch: %d  Bitrate: %d kbps\n",
                info.samplingRate, info.channelNumber, info.bitrate);
    std::printf("  Loops  : %d\n", loopCount);
    std::printf("  Verbose: %s  (StreamBufferDoneAsync %s)\n\n",
                g_quietBuffers ? "no" : "yes",
                g_quietBuffers ? "suppressed" : "shown");

    std::printf("  Callback events will appear here as they fire:\n");
    std::printf("  ───────────────────────────────────────────────\n");

    // Reserve two blank lines so the progress bar has room below the header.
    // g_progressRow marks the fixed row the progress bar always overwrites.
    std::printf("\n\n");
    {
        CONSOLE_SCREEN_BUFFER_INFO cbi{};
        GetConsoleScreenBufferInfo(g_hCon, &cbi);
        // Progress bar sits on the current row (after the two blank lines above)
        g_progressRow = cbi.dwCursorPosition.Y;
    }

    // ── Start playback ─────────────────────────────────────────────────────────
    int vol = 80;
    player->SetMasterVolume(vol, vol);

    bool started = (loopCount > 0)
        ? player->PlayLoop(static_cast<uint32_t>(loopCount))
        : player->Play();

    if (!started) {
        std::printf("Play() failed: %s\n", player->GetError());
        DestroyZFork(player);
        return 1;
    }

    // Trigger a slide shortly after start if requested
    if (doSlide) {
        Sleep(1500);
        player->SlideVolume(vol, vol, 30, 30, 1000);
        Sleep(1200);
        player->SlideVolume(30, 30, vol, vol, 800);
    }

    // ── Main loop ──────────────────────────────────────────────────────────────
    while (true) {
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 'q' || ch == 'Q') break;
            if (ch == 'p' || ch == 'P') {
                auto st = player->GetStatus();
                if (st.paused) player->Resume();
                else           player->Pause();
            }
            if (ch == 's' || ch == 'S') {
                // Manual slide trigger from keyboard
                player->SlideVolume(vol, vol, 20, 20, 1500);
                Sleep(1700);
                player->SlideVolume(20, 20, vol, vol, 1000);
            }
        }

        DrawProgress(vol);

        auto st = player->GetStatus();
        if (!st.playing && !st.paused) break;

        Sleep(80);
    }

    player->Stop();

    // ── Summary ────────────────────────────────────────────────────────────────
    // Move cursor past the progress row so summary prints below it
    {
        CONSOLE_SCREEN_BUFFER_INFO cbi{};
        GetConsoleScreenBufferInfo(g_hCon, &cbi);
        SHORT below = static_cast<SHORT>(
            std::max((int)g_progressRow + 1, (int)cbi.dwCursorPosition.Y + 1));
        COORD c{ 0, below };
        SetConsoleCursorPosition(g_hCon, c);
    }
    std::printf("\n");
    SetColor(0x0F);
    std::printf("  ───────────────────────────────────────────────\n");
    std::printf("  Total events received : %d\n", g_eventCount.load());
    ResetColor();
    std::printf("  Final position        : %u ms\n", g_posMs.load());
    std::printf("\n");

    DestroyZFork(player);
    DeleteCriticalSection(&g_conLock);
    return 0;
}
