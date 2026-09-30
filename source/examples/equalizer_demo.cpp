/**
 * libZFork — Equalizer Demo
 *
 * Portiert von libZPlay/example/equalizer.cpp
 *
 * Interaktive 31-Band-EQ-Steuerung während der Wiedergabe.
 * Zeigt außerdem live FFT-Balken im Konsolenfenster.
 *
 * Usage:  equalizer_demo <audiodatei>
 *
 * Tasten:
 *   0-9, a-v   Band 0-31 auswählen
 *   +/-        Gewinn des gewählten Bandes ±3 dB
 *   R          Reset (alle Bänder auf 0 dB)
 *   P          Preset "Smiley" (Bass+Treble boost)
 *   S          Preset "Cut 5k-10k" (wie libZPlay-Original)
 *   E          Equalizer ein/aus
 *   Space      Pause / Weiter
 *   Q / Esc    Beenden
 */

#define NOMINMAX
#include <libzfork/libzfork.h>
#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <array>
#include <string>

// ── Konsolen-Helfer ────────────────────────────────────────────────────────────

static HANDLE g_con = GetStdHandle(STD_OUTPUT_HANDLE);

static void GotoXY(int x, int y) {
    COORD c{ static_cast<SHORT>(x), static_cast<SHORT>(y) };
    SetConsoleCursorPosition(g_con, c);
}
static void Color(WORD w) { SetConsoleTextAttribute(g_con, w); }
static void HideCursor() { CONSOLE_CURSOR_INFO ci{1,FALSE}; SetConsoleCursorInfo(g_con,&ci); }
static void ShowCursor() { CONSOLE_CURSOR_INFO ci{10,TRUE};  SetConsoleCursorInfo(g_con,&ci); }

// ── Band-Mittenfequenzen (ISO 31-Band, 1/3 Oktave) ────────────────────────────

static constexpr int kBands = 31;
static constexpr int kBandFreq[kBands] = {
     20,  25,  31,  40,  50,  63,  80, 100, 125, 160,
    200, 250, 315, 400, 500, 630, 800,1000,1250,1600,
   2000,2500,3150,4000,5000,6300,8000,10000,12500,16000,20000
};

// ── EQ-Anzeige ────────────────────────────────────────────────────────────────

static void DrawEQ(libZFork::ZFork* p, const float gains[kBands],
                   int selected, bool enabled) {
    GotoXY(0, 0); Color(0x0F);
    std::printf("══ libZFork Equalizer Demo ══════════════════════════════════════\n");

    GotoXY(0, 1); Color(enabled ? 0x0A : 0x08);
    std::printf("  EQ: %s    ", enabled ? "EIN " : "AUS");
    Color(0x07);
    std::printf("Band %2d: %6.0f Hz    Gain: %+.1f dB   ", selected, static_cast<float>(kBandFreq[selected]), gains[selected]);
    Color(0x08);
    std::printf("E=Toggle P=Smiley S=Cut5k R=Reset Q=Quit\n");

    // Balken: 12 Zeilen Höhe, Bereich -12 .. +12 dB
    const int rows = 12;
    const int topY = 3;
    const float maxDB = 12.f;

    GotoXY(0, 2);
    Color(0x08);
    std::printf(" +12 dB |");
    for (int b = 0; b < kBands; ++b) std::printf("─");
    std::printf("|\n");

    for (int row = 0; row < rows; ++row) {
        GotoXY(0, topY + row);
        float rowDB = maxDB - (row * 2.f * maxDB / (rows - 1));
        if (row == rows / 2) { Color(0x08); std::printf("   0 dB |"); }
        else                 { Color(0x08); std::printf("        |"); }

        for (int b = 0; b < kBands; ++b) {
            float g = gains[b];
            bool active = (g >= 0) ? (rowDB >= 0 && rowDB <= g)
                                   : (rowDB <= 0 && rowDB >= g);
            if (b == selected) Color(active ? 0x0E : 0x06);
            else               Color(active ? (g > 0 ? 0x0A : 0x0C) : 0x08);
            std::printf(active ? "█" : "·");
        }
        Color(0x08);
        std::printf("|\n");
    }

    GotoXY(0, topY + rows); Color(0x08);
    std::printf(" -12 dB |");
    for (int b = 0; b < kBands; ++b) std::printf("─");
    std::printf("|\n");

    // Frequenz-Labels
    GotoXY(9, topY + rows + 1); Color(0x08);
    std::printf("20      100     500    1k     5k    20kHz\n");
}

// ── Presets ────────────────────────────────────────────────────────────────────

static void PresetSmiley(libZFork::ZFork* p, float gains[kBands]) {
    const float s[] = { 6, 5, 4, 3, 2, 1, 0,-1,-2,-2,-2,-2,-2,-1, 0, 0,
                         0, 1, 2, 3, 4, 5, 5, 5, 5, 4, 3, 2, 1, 0,-1 };
    for (int b = 0; b < kBands; ++b) {
        gains[b] = s[b];
        p->SetEqualizerBandGain(b, s[b]);
    }
}

static void PresetCut5k(libZFork::ZFork* p, float gains[kBands]) {
    // Entspricht dem libZPlay-Original: Band 24+25 (5k-10k) stark absenken
    for (int b = 0; b < kBands; ++b) {
        float g = (b >= 24 && b <= 25) ? -12.f : 0.f;
        gains[b] = g;
        p->SetEqualizerBandGain(b, g);
    }
}

static void PresetReset(libZFork::ZFork* p, float gains[kBands]) {
    for (int b = 0; b < kBands; ++b) {
        gains[b] = 0.f;
        p->SetEqualizerBandGain(b, 0.f);
    }
}

// ── Hauptprogramm ─────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::printf("Usage: equalizer_demo <audiodatei>\n");
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

    // EQ aktivieren, Standard-Reset
    float gains[kBands]{};
    player->EnableEqualizer(true);
    PresetReset(player, gains);

    player->SetMasterVolume(80, 80);
    player->SetFFTPoints(1024);

    SetConsoleOutputCP(CP_UTF8);
    HideCursor();
    system("cls");

    if (!player->Play()) {
        std::printf("Play() fehlgeschlagen: %s\n", player->GetError());
        libZFork::DestroyZFork(player);
        ShowCursor();
        return 1;
    }

    bool eqOn    = true;
    int  selBand = 0;
    bool running = true;

    // Band-Auswahl-Taste → Index
    auto keyToBand = [](int ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'v') return 10 + (ch - 'a');
        if (ch >= 'A' && ch <= 'V') return 10 + (ch - 'A');
        return -1;
    };

    while (running) {
        if (_kbhit()) {
            int ch = _getch();
            int band = keyToBand(ch);
            if (band >= 0 && band < kBands) {
                selBand = band;
            } else switch (ch) {
                case '+': case '=':
                    gains[selBand] = std::min(12.f, gains[selBand] + 3.f);
                    player->SetEqualizerBandGain(selBand, gains[selBand]);
                    break;
                case '-':
                    gains[selBand] = std::max(-12.f, gains[selBand] - 3.f);
                    player->SetEqualizerBandGain(selBand, gains[selBand]);
                    break;
                case 'e': case 'E':
                    eqOn = !eqOn;
                    player->EnableEqualizer(eqOn);
                    break;
                case 'p': case 'P': PresetSmiley(player, gains); break;
                case 's': case 'S': PresetCut5k(player, gains);  break;
                case 'r': case 'R': PresetReset(player, gains);  break;
                case ' ': {
                    auto st = player->GetStatus();
                    if (st.paused) player->Resume(); else player->Pause();
                    break;
                }
                case 'q': case 'Q': case 27: running = false; break;
            }
        }

        DrawEQ(player, gains, selBand, eqOn);

        // Status-Zeile
        libZFork::StreamTime pos{};
        player->GetPosition(pos);
        auto st = player->GetStatus();

        GotoXY(0, 17); Color(0x07);
        std::printf("  %s  %02u:%02u:%02u / %02u:%02u:%02u   "
                    "Space=Pause  +/-=Band-Gain  0-9/a-v=Band",
                    st.paused ? "[PAUSE]" : "[PLAY] ",
                    pos.hms.hour, pos.hms.minute, pos.hms.second,
                    info.length.hms.hour, info.length.hms.minute, info.length.hms.second);

        if (!st.playing && !st.paused) break;
        Sleep(80);
    }

    player->Stop();
    libZFork::DestroyZFork(player);
    ShowCursor(); Color(0x07);
    GotoXY(0, 19); std::printf("\nWiedergabe beendet.\n");
    return 0;
}
