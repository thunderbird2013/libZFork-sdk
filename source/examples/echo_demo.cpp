/**
 * libZFork — Echo Demo
 *
 * Portiert von libZPlay/example/echo.cpp
 *
 * Demonstriert den Echo-Effekt mit interaktiver Parameteranpassung.
 * L/R-Kanal haben unterschiedliche Verzögerungen für einen räumlichen Effekt.
 *
 * Usage:  echo_demo <audiodatei>
 *
 * Tasten:
 *   E          Echo ein/aus
 *   L/l        Linke Verzögerung +/- 50 ms
 *   R/r        Rechte Verzögerung +/- 50 ms
 *   +/-        Echo-Lautstärke +/- 5 %
 *   1          Preset: kurzes Echo  (180/200 ms)
 *   2          Preset: mittleres Echo (500/550 ms)
 *   3          Preset: langes Echo (1800/2000 ms) — wie libZPlay-Original
 *   Space      Pause / Weiter
 *   Q / Esc    Beenden
 */

#define NOMINMAX
#include <libzfork/libzfork.h>
#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>

// ── Helfer ─────────────────────────────────────────────────────────────────────

static HANDLE g_con = GetStdHandle(STD_OUTPUT_HANDLE);
static void GotoXY(int x, int y) {
    COORD c{ static_cast<SHORT>(x), static_cast<SHORT>(y) };
    SetConsoleCursorPosition(g_con, c);
}
static void Color(WORD w) { SetConsoleTextAttribute(g_con, w); }

// ── Echo-Parameter zeichnen ────────────────────────────────────────────────────

static void DrawParams(const libZFork::EchoParams& ep, bool on) {
    GotoXY(0, 0); Color(0x0F);
    std::printf("══ libZFork Echo Demo ═══════════════════════════════\n\n");

    GotoXY(0, 2); Color(on ? 0x0A : 0x08);
    std::printf("  Echo: %-4s", on ? "EIN" : "AUS");
    Color(0x07);
    std::printf("   E=Toggle   1/2/3=Preset   Space=Pause   Q=Quit\n\n");

    GotoXY(0, 4); Color(0x0B);
    std::printf("  ┌─────────────────── L-Kanal ───────────┐  ┌─────────────────── R-Kanal ───────────┐\n");
    GotoXY(0, 5); Color(0x07);
    std::printf("  │  Verzögerung : %4d ms  [L/l = +/-50]  │  │  Verzögerung : %4d ms  [R/r = +/-50]  │\n",
                ep.leftDelay, ep.rightDelay);
    GotoXY(0, 6); Color(0x07);
    std::printf("  │  Quelle-Vol  : %3d %%                   │  │  Quelle-Vol  : %3d %%                   │\n",
                ep.leftSourceVol, ep.rightSourceVol);
    GotoXY(0, 7); Color(0x07);
    std::printf("  │  Echo-Vol    : %3d %%   [+/- anpassen]  │  │  Echo-Vol    : %3d %%   [+/- anpassen]  │\n",
                ep.leftEchoVol, ep.rightEchoVol);
    GotoXY(0, 8); Color(0x0B);
    std::printf("  └───────────────────────────────────────┘  └───────────────────────────────────────┘\n");
}

// ── Hauptprogramm ─────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::printf("Usage: echo_demo <audiodatei>\n");
        return 1;
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, wpath.data(), wlen);
    if (!wpath.empty() && wpath.back() == L'\0') wpath.pop_back();

    auto* player = libZFork::CreateZFork();
    if (!player) { std::printf("CreateZFork() fehlgeschlagen\n"); return 1; }

    if (!player->OpenFile(wpath.c_str(), libZFork::StreamFormat::Autodetect)) {
        std::printf("OpenFile fehlgeschlagen: %s\n", player->GetError());
        libZFork::DestroyZFork(player);
        return 1;
    }

    libZFork::StreamInfo info{};
    player->GetStreamInfo(0, info);

    // Preset 3: langes Echo — wie libZPlay-Original
    libZFork::EchoParams ep{};
    ep.leftDelay      = 1800;
    ep.leftSourceVol  = 80;
    ep.leftEchoVol    = 20;
    ep.rightDelay     = 2000;
    ep.rightSourceVol = 80;
    ep.rightEchoVol   = 20;

    player->EnableEcho(true);
    player->SetEchoParams(ep);
    player->SetMasterVolume(80, 80);

    // Echo-Params lesen und anzeigen (Verifikation)
    libZFork::EchoParams readBack{};
    if (player->GetEchoParams(readBack)) {
        std::printf("Echo aktiv: L=%dms vol=%d%%  R=%dms vol=%d%%\n",
                    readBack.leftDelay, readBack.leftEchoVol,
                    readBack.rightDelay, readBack.rightEchoVol);
    }

    SetConsoleOutputCP(CP_UTF8);
    system("cls");
    CONSOLE_CURSOR_INFO ci{1,FALSE}; SetConsoleCursorInfo(g_con,&ci);

    if (!player->Play()) {
        std::printf("Play() fehlgeschlagen: %s\n", player->GetError());
        libZFork::DestroyZFork(player);
        return 1;
    }

    bool echoOn = true;
    bool running = true;

    while (running) {
        if (_kbhit()) {
            int ch = _getch();
            switch (ch) {
                case 'e': case 'E':
                    echoOn = !echoOn;
                    player->EnableEcho(echoOn);
                    break;
                case 'L':   // L-Delay +50 ms
                    ep.leftDelay = std::min(5000, ep.leftDelay + 50);
                    player->SetEchoParams(ep);
                    break;
                case 'l':   // L-Delay -50 ms
                    ep.leftDelay = std::max(10, ep.leftDelay - 50);
                    player->SetEchoParams(ep);
                    break;
                case 'R':   // R-Delay +50 ms
                    ep.rightDelay = std::min(5000, ep.rightDelay + 50);
                    player->SetEchoParams(ep);
                    break;
                case 'r':   // R-Delay -50 ms
                    ep.rightDelay = std::max(10, ep.rightDelay - 50);
                    player->SetEchoParams(ep);
                    break;
                case '+': case '=':
                    ep.leftEchoVol  = std::min(100, ep.leftEchoVol  + 5);
                    ep.rightEchoVol = std::min(100, ep.rightEchoVol + 5);
                    player->SetEchoParams(ep);
                    break;
                case '-':
                    ep.leftEchoVol  = std::max(0, ep.leftEchoVol  - 5);
                    ep.rightEchoVol = std::max(0, ep.rightEchoVol - 5);
                    player->SetEchoParams(ep);
                    break;
                case '1':   // kurzes Echo
                    ep = { 180, 80, 20, 200, 80, 20 };
                    player->SetEchoParams(ep);
                    break;
                case '2':   // mittleres Echo
                    ep = { 500, 80, 20, 550, 80, 20 };
                    player->SetEchoParams(ep);
                    break;
                case '3':   // langes Echo (libZPlay-Original)
                    ep = { 1800, 80, 20, 2000, 80, 20 };
                    player->SetEchoParams(ep);
                    break;
                case ' ': {
                    auto st = player->GetStatus();
                    if (st.paused) player->Resume(); else player->Pause();
                    break;
                }
                case 'q': case 'Q': case 27: running = false; break;
            }
        }

        DrawParams(ep, echoOn);

        libZFork::StreamTime pos{};
        player->GetPosition(pos);
        libZFork::VUData vu{};
        player->GetVUData(vu);
        auto st = player->GetStatus();

        int barL = static_cast<int>(vu.left  * 30);
        int barR = static_cast<int>(vu.right * 30);

        GotoXY(0, 10); Color(0x07);
        std::printf("  %s  %02u:%02u:%02u / %02u:%02u:%02u\n",
                    st.paused ? "[PAUSE]" : "[PLAY] ",
                    pos.hms.hour, pos.hms.minute, pos.hms.second,
                    info.length.hms.hour, info.length.hms.minute, info.length.hms.second);
        GotoXY(0, 11); Color(0x08);
        std::printf("  L[");
        for (int i = 0; i < 30; ++i) {
            Color(i < 20 ? 0x0A : (i < 27 ? 0x0E : 0x0C));
            std::printf(i < barL ? "█" : " ");
        }
        Color(0x08); std::printf("] R[");
        for (int i = 0; i < 30; ++i) {
            Color(i < 20 ? 0x0A : (i < 27 ? 0x0E : 0x0C));
            std::printf(i < barR ? "█" : " ");
        }
        Color(0x08); std::printf("]  ");

        if (!st.playing && !st.paused) break;
        Sleep(80);
    }

    player->Stop();
    libZFork::DestroyZFork(player);
    ci = {10,TRUE}; SetConsoleCursorInfo(g_con,&ci);
    Color(0x07); GotoXY(0,13);
    std::printf("\nWiedergabe beendet.\n");
    return 0;
}
