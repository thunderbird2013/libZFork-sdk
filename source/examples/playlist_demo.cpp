/**
 * libZFork — Playlist Demo
 *
 * Demonstrates sequential playback of multiple audio files with:
 *  - Gapless / crossfade-ready transitions
 *  - Per-track metadata display
 *  - VU meter in the console
 *  - Keyboard control (next/prev/pause/quit)
 *
 * Usage:
 *   playlist_demo <file1> <file2> ... [--volume N] [--repeat]
 *   playlist_demo *.mp3 --volume 75
 */

#define NOMINMAX
#include <libzfork/libzfork.h>
#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

// ── Console helper ─────────────────────────────────────────────────────────────

static HANDLE g_hCon = INVALID_HANDLE_VALUE;

static void ConGotoXY(int x, int y) {
    if (g_hCon == INVALID_HANDLE_VALUE) return;
    COORD c{ static_cast<SHORT>(x), static_cast<SHORT>(y) };
    SetConsoleCursorPosition(g_hCon, c);
}

static void ConColor(WORD attr) {
    if (g_hCon != INVALID_HANDLE_VALUE)
        SetConsoleTextAttribute(g_hCon, attr);
}

static void ConClearLine(int y) {
    ConGotoXY(0, y);
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(g_hCon, &csbi);
    int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    for (int i = 0; i < w; ++i) std::printf(" ");
    ConGotoXY(0, y);
}

// ── Argument helpers ────────────────────────────────────────────────────────────

static bool HasFlag(int argc, char* argv[], const char* flag) {
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], flag)) return true;
    return false;
}

static int GetInt(int argc, char* argv[], const char* key, int def) {
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], key)) return std::atoi(argv[i + 1]);
    return def;
}

static std::wstring Utf8ToWide(const char* s) {
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), n);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

// ── VU bar drawing ──────────────────────────────────────────────────────────────

static void DrawVU(float left, float right, int y) {
    ConGotoXY(0, y);
    ConColor(0x08);
    std::printf("L[");
    int barL = static_cast<int>(std::clamp(left,  0.f, 1.f) * 30);
    int barR = static_cast<int>(std::clamp(right, 0.f, 1.f) * 30);
    for (int i = 0; i < 30; ++i) {
        WORD c = i < 20 ? 0x0A : (i < 27 ? 0x0E : 0x0C);
        ConColor(c);
        std::printf(i < barL ? "█" : " ");
    }
    ConColor(0x08);
    std::printf("] R[");
    for (int i = 0; i < 30; ++i) {
        WORD c = i < 20 ? 0x0A : (i < 27 ? 0x0E : 0x0C);
        ConColor(c);
        std::printf(i < barR ? "█" : " ");
    }
    ConColor(0x08);
    std::printf("]   ");
    ConColor(0x07);
}

// ── Track display ───────────────────────────────────────────────────────────────

static void ShowTrackInfo(libZFork::ZFork* player, const char* path,
                          int index, int total, int startY) {
    libZFork::ID3InfoEx tag{};
    libZFork::StreamInfo info{};
    player->LoadID3Ex(0, tag);
    player->GetStreamInfo(0, info);

    system("cls");

    ConGotoXY(0, 0); ConColor(0x0F);
    std::printf("  libZFork Playlist Demo");

    ConGotoXY(0, 1); ConColor(0x08);
    for (int i = 0; i < 60; ++i) std::printf("─");

    ConGotoXY(0, 2); ConColor(0x0B);
    std::printf("  Track %d / %d", index + 1, total);
    ConGotoXY(20, 2); ConColor(0x07);
    if (tag.title[0])
        std::printf("  %s", tag.title);
    else
        std::printf("  %s", path);

    ConGotoXY(0, 3); ConColor(0x08);
    std::printf("  Artist  : "); ConColor(0x07);
    std::printf("%-30s", tag.artist[0] ? tag.artist : "(unknown)");
    ConGotoXY(0, 4); ConColor(0x08);
    std::printf("  Album   : "); ConColor(0x07);
    std::printf("%-30s", tag.album[0] ? tag.album : "(unknown)");

    ConGotoXY(0, 5); ConColor(0x08);
    std::printf("  Format  : "); ConColor(0x07);
    std::printf("%d Hz  %d ch  %d kbps%s",
                info.samplingRate, info.channelNumber, info.bitrate,
                info.vbr ? " VBR" : "");

    ConGotoXY(0, 6); ConColor(0x08);
    std::printf("  Duration: "); ConColor(0x07);
    std::printf("%02u:%02u:%02u",
                info.length.hms.hour, info.length.hms.minute, info.length.hms.second);

    ConGotoXY(0, startY - 1); ConColor(0x08);
    for (int i = 0; i < 60; ++i) std::printf("─");
    ConGotoXY(0, startY + 3); ConColor(0x08);
    for (int i = 0; i < 60; ++i) std::printf("─");

    ConGotoXY(0, startY + 4); ConColor(0x08);
    std::printf("  Q=Quit  Space=Pause  N=Next  P=Prev  +/-=Volume  R=Repeat");
}

// ── Main ───────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::printf("Usage: playlist_demo <file1> [file2 ...] [--volume N] [--repeat]\n");
        return 1;
    }

    g_hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleOutputCP(CP_UTF8);

    // Collect track paths (skip flags)
    std::vector<std::string> tracks;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') { ++i; continue; }  // skip flag and its value
        if (!std::strcmp(argv[i], "--repeat") || !std::strcmp(argv[i], "--list")) continue;
        tracks.push_back(argv[i]);
    }

    if (tracks.empty()) {
        std::printf("No audio files given.\n");
        return 1;
    }

    int  volume = GetInt(argc, argv, "--volume", 80);
    bool repeat = HasFlag(argc, argv, "--repeat");

    // Hide cursor
    CONSOLE_CURSOR_INFO ci{ 1, FALSE };
    SetConsoleCursorInfo(g_hCon, &ci);

    auto* player = libZFork::CreateZFork();
    if (!player) { std::printf("CreateZFork() failed\n"); return 1; }

    constexpr int kVURow      = 8;
    constexpr int kStatusRow  = 9;
    constexpr int kProgressRow= 10;

    int  current  = 0;
    bool running  = true;
    bool doNext   = false;
    bool doPrev   = false;

    auto loadTrack = [&](int idx) -> bool {
        player->Stop();
        auto w = Utf8ToWide(tracks[idx].c_str());
        if (!player->OpenFile(w.c_str(), libZFork::StreamFormat::Autodetect)) {
            std::printf("Cannot open: %s — %s\n", tracks[idx].c_str(), player->GetError());
            return false;
        }
        player->SetMasterVolume(volume, volume);
        ShowTrackInfo(player, tracks[idx].c_str(), idx,
                      static_cast<int>(tracks.size()), kVURow);
        return player->Play();
    };

    if (!loadTrack(current)) {
        libZFork::DestroyZFork(player);
        return 1;
    }

    while (running) {
        // ── Key input ─────────────────────────────────────────────────────────
        if (_kbhit()) {
            int ch = _getch();
            switch (ch) {
                case 'q': case 'Q': case 27: running = false; break;
                case ' ': {
                    auto st = player->GetStatus();
                    if (st.paused) player->Resume(); else player->Pause();
                    break;
                }
                case 'n': case 'N': doNext = true;  break;
                case 'p': case 'P': doPrev = true;  break;
                case '+': case '=':
                    volume = std::min(100, volume + 5);
                    player->SetMasterVolume(volume, volume);
                    break;
                case '-':
                    volume = std::max(0, volume - 5);
                    player->SetMasterVolume(volume, volume);
                    break;
                case 'r': case 'R': repeat = !repeat; break;
            }
        }

        // ── Track navigation ──────────────────────────────────────────────────
        auto st = player->GetStatus();
        bool trackEnded = !st.playing && !st.paused;

        if (doNext || trackEnded) {
            doNext = false;
            current = (current + 1) % static_cast<int>(tracks.size());
            if (current == 0 && !repeat && trackEnded) { running = false; break; }
            if (!loadTrack(current)) { running = false; break; }
            continue;
        }
        if (doPrev) {
            doPrev = false;
            current = (current - 1 + static_cast<int>(tracks.size()))
                      % static_cast<int>(tracks.size());
            if (!loadTrack(current)) { running = false; break; }
            continue;
        }

        // ── VU meter ──────────────────────────────────────────────────────────
        libZFork::VUData vu{};
        player->GetVUData(vu);
        DrawVU(vu.left, vu.right, kVURow);

        // ── Status / position ─────────────────────────────────────────────────
        libZFork::StreamTime pos{};
        player->GetPosition(pos);

        libZFork::StreamInfo info{};
        player->GetStreamInfo(0, info);

        ConGotoXY(0, kStatusRow); ConColor(0x07);
        std::printf("  %s  %02u:%02u:%02u  Vol:%3d%%  %s   ",
                    st.paused ? "[PAUSE]" : "[PLAY] ",
                    pos.hms.hour, pos.hms.minute, pos.hms.second,
                    volume,
                    repeat ? "[REPEAT]" : "        ");

        // ── Progress bar ──────────────────────────────────────────────────────
        ConGotoXY(0, kProgressRow); ConColor(0x08);
        std::printf("  [");
        uint64_t totSec = info.length.hms.hour * 3600u +
                          info.length.hms.minute * 60u +
                          info.length.hms.second;
        uint64_t posSec = pos.hms.hour * 3600u +
                          pos.hms.minute * 60u +
                          pos.hms.second;
        int filled = (totSec > 0)
            ? static_cast<int>(40.0 * posSec / totSec)
            : 0;
        for (int i = 0; i < 40; ++i) {
            ConColor(i < filled ? 0x0B : 0x08);
            std::printf(i < filled ? "─" : "·");
        }
        ConColor(0x08);
        std::printf("]  ");
        ConColor(0x07);
        std::printf("%02u:%02u:%02u  ",
                    info.length.hms.hour, info.length.hms.minute, info.length.hms.second);

        Sleep(40);
    }

    player->Stop();
    libZFork::DestroyZFork(player);

    ci = { 10, TRUE };
    SetConsoleCursorInfo(g_hCon, &ci);
    ConColor(0x07);
    std::printf("\n\nPlayback ended.\n");
    return 0;
}
