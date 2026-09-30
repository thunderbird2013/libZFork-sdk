/**
 * libZFork — FFT Spektrum Demo (Console-Visualisierung)
 *
 * Öffnet eine Audio-Datei und zeigt während der Wiedergabe
 * ein Echtzeit-Spektrum-Balkendiagramm in der Konsole.
 *
 * Usage:  fft_demo <audiodatei> [--bins N] [--window hamming|hann|blackman|flat]
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
#include <vector>
#include <string>
#include <string_view>

// ── Console helpers ────────────────────────────────────────────────────────────

static HANDLE g_hCon = GetStdHandle(STD_OUTPUT_HANDLE);

static void GotoXY(int x, int y) {
    COORD c{ static_cast<SHORT>(x), static_cast<SHORT>(y) };
    SetConsoleCursorPosition(g_hCon, c);
}

static void SetColor(WORD attr) { SetConsoleTextAttribute(g_hCon, attr); }

static void HideCursor() {
    CONSOLE_CURSOR_INFO ci{ 1, FALSE };
    SetConsoleCursorInfo(g_hCon, &ci);
}

static void ShowCursor() {
    CONSOLE_CURSOR_INFO ci{ 10, TRUE };
    SetConsoleCursorInfo(g_hCon, &ci);
}

// ── Bar characters (Unicode block elements) ───────────────────────────────────

// Vertical bar segments from empty to full
static const wchar_t* kBarSegs[] = { L" ", L"▁", L"▂", L"▃", L"▄", L"▅", L"▆", L"▇", L"█" };
static constexpr int  kBarSegsN  = 8;

static void DrawBar(int x, int topY, int rows, float value, WORD color) {
    // value 0..1 → fill from bottom
    float fillRows = value * static_cast<float>(rows);
    for (int r = 0; r < rows; ++r) {
        GotoXY(x, topY + rows - 1 - r);
        float rowBottom = static_cast<float>(r);
        float rowFill   = std::clamp(fillRows - rowBottom, 0.f, 1.f);
        int   seg       = static_cast<int>(rowFill * kBarSegsN);
        SetColor(seg > 0 ? color : 0x08);
        DWORD w;
        WriteConsoleW(g_hCon, seg > 0 ? kBarSegs[std::min(seg, kBarSegsN)] : L"│", 1, &w, nullptr);
    }
}

// ── Peak hold ─────────────────────────────────────────────────────────────────

struct PeakHold {
    float  peak   = 0.f;
    int    hold   = 0;       // frames to hold
    float  decay  = 0.f;     // per-frame decay
};

static void UpdatePeak(PeakHold& p, float val, int holdFrames = 20, float decayRate = 0.02f) {
    if (val >= p.peak) {
        p.peak  = val;
        p.hold  = holdFrames;
    } else {
        if (p.hold > 0) --p.hold;
        else            p.peak = std::max(0.f, p.peak - decayRate);
    }
}

// ── Draw spectrum ──────────────────────────────────────────────────────────────

static void DrawSpectrum(const float* fftL, const float* fftR, int fftBins,
                         int numBars, int barRows, int startY,
                         std::vector<PeakHold>& peaksL,
                         std::vector<PeakHold>& peaksR) {
    // Group FFT bins into numBars display bars (logarithmic spacing)
    int halfBars = numBars / 2;

    for (int b = 0; b < halfBars; ++b) {
        // Logarithmic bin mapping: low bars cover fewer bins, high bars more
        float logStart = std::pow(static_cast<float>(b)     / halfBars, 2.f) * fftBins / 2;
        float logEnd   = std::pow(static_cast<float>(b + 1) / halfBars, 2.f) * fftBins / 2;
        int   binStart = static_cast<int>(logStart);
        int   binEnd   = std::max(binStart + 1, static_cast<int>(logEnd));
        binEnd = std::min(binEnd, fftBins / 2);

        // Average magnitude in this bin range
        float sumL = 0.f, sumR = 0.f;
        for (int k = binStart; k < binEnd; ++k) {
            sumL += fftL[k];
            sumR += fftR[k];
        }
        float invN = 1.f / std::max(1, binEnd - binStart);
        float valL = std::clamp(sumL * invN * 4.f, 0.f, 1.f);
        float valR = std::clamp(sumR * invN * 4.f, 0.f, 1.f);

        UpdatePeak(peaksL[b], valL);
        UpdatePeak(peaksR[b], valR);

        int x = b * 2;

        // Left channel (top half, cyan → green based on level)
        WORD colorL = valL > 0.7f ? 0x0C : (valL > 0.4f ? 0x0E : 0x0A);
        DrawBar(x, startY, barRows, valL, colorL);

        // Peak dot left
        int peakRowL = static_cast<int>((1.f - peaksL[b].peak) * barRows);
        GotoXY(x, startY + std::clamp(peakRowL, 0, barRows - 1));
        SetColor(0x0F);
        DWORD w; WriteConsoleW(g_hCon, L"─", 1, &w, nullptr);

        // Right channel (bottom half, blue → magenta)
        WORD colorR = valR > 0.7f ? 0x0D : (valR > 0.4f ? 0x09 : 0x01);
        DrawBar(x, startY + barRows + 1, barRows, valR, colorR);

        int peakRowR = static_cast<int>((1.f - peaksR[b].peak) * barRows);
        GotoXY(x, startY + barRows + 1 + std::clamp(peakRowR, 0, barRows - 1));
        SetColor(0x0F);
        WriteConsoleW(g_hCon, L"─", 1, &w, nullptr);
    }
}

// ── Argument parsing ──────────────────────────────────────────────────────────

static bool HasFlag(int argc, char* argv[], const char* flag) {
    for (int i = 1; i < argc; ++i) if (!std::strcmp(argv[i], flag)) return true;
    return false;
}
static const char* GetArg(int argc, char* argv[], const char* key) {
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], key)) return argv[i + 1];
    return nullptr;
}

static libZFork::FFTWindow ParseWindow(const char* s) {
    if (!s)                          return libZFork::FFTWindow::Hann;
    if (!std::strcmp(s,"hamming"))   return libZFork::FFTWindow::Hamming;
    if (!std::strcmp(s,"hann"))      return libZFork::FFTWindow::Hann;
    if (!std::strcmp(s,"blackman"))  return libZFork::FFTWindow::Blackman;
    if (!std::strcmp(s,"flat"))      return libZFork::FFTWindow::FlatTop;
    if (!std::strcmp(s,"bartlett"))  return libZFork::FFTWindow::Bartlett;
    if (!std::strcmp(s,"nuttall"))   return libZFork::FFTWindow::Nuttall;
    return libZFork::FFTWindow::Hann;
}

// ── Main ───────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::printf("Usage: fft_demo <audiodatei> [--bins 2048] [--window hann|hamming|blackman|flat]\n");
        std::printf("       Tasten waehrend Wiedergabe: Q=Quit  Space=Pause\n");
        return 1;
    }

    int  fftBins = 2048;
    if (const char* b = GetArg(argc, argv, "--bins")) fftBins = std::atoi(b);
    auto window  = ParseWindow(GetArg(argc, argv, "--window"));

    // Widen path
    int wlen = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
    std::vector<wchar_t> wpath(wlen);
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, wpath.data(), wlen);

    // ── Setup player ──────────────────────────────────────────────────────────
    auto* player = libZFork::CreateZFork();
    if (!player) { std::printf("CreateZFork() fehlgeschlagen\n"); return 1; }

    if (!player->OpenFile(wpath.data(), libZFork::StreamFormat::Autodetect)) {
        std::printf("OpenFile fehlgeschlagen: %s\n", player->GetError());
        libZFork::DestroyZFork(player);
        return 1;
    }

    player->SetFFTPoints(static_cast<uint32_t>(fftBins));
    player->SetFFTWindow(window);
    player->SetMasterVolume(80, 80);

    // ID3 info
    libZFork::ID3InfoEx tag{};
    player->LoadID3Ex(0, tag);
    libZFork::StreamInfo info{};
    player->GetStreamInfo(0, info);

    // ── Console setup ─────────────────────────────────────────────────────────
    SetConsoleOutputCP(CP_UTF8);
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(g_hCon, &csbi);
    int consoleW = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    int consoleH = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

    int numBars   = std::min(60, (consoleW / 2) & ~1);  // even number
    int barRows   = std::max(4, (consoleH - 10) / 2);
    int specStartY= 5;

    HideCursor();
    system("cls");

    // ── Draw static labels ────────────────────────────────────────────────────
    GotoXY(0, 0); SetColor(0x0F);
    std::printf("libZFork FFT-Demo  |  %s",
                tag.title[0] ? tag.title : argv[1]);
    GotoXY(0, 1); SetColor(0x07);
    std::printf("Interpret: %-30s  %d Hz  %d ch  %d kbps",
                tag.artist[0] ? tag.artist : "?",
                info.samplingRate, info.channelNumber, info.bitrate);
    GotoXY(0, 2); SetColor(0x08);
    std::printf("Fenster: ");
    SetColor(0x0B);
    std::printf("%-12s", GetArg(argc, argv, "--window") ? GetArg(argc, argv, "--window") : "hann");
    SetColor(0x08);
    std::printf("  FFT-Bins: ");
    SetColor(0x0B);
    std::printf("%d", fftBins);
    SetColor(0x08);
    GotoXY(0, 3); std::printf("Q=Beenden  Space=Pause  +/-=Lautstaerke");

    // Axis labels
    GotoXY(0, specStartY - 1); SetColor(0x0B);
    std::printf("L ▲");
    GotoXY(0, specStartY + barRows); SetColor(0x09);
    std::printf("R ▼");

    // Peak labels
    std::vector<PeakHold> peaksL(numBars / 2), peaksR(numBars / 2);
    std::vector<float> fftL(fftBins / 2), fftR(fftBins / 2);

    if (!player->Play()) {
        std::printf("\nPlay() fehlgeschlagen: %s\n", player->GetError());
        libZFork::DestroyZFork(player);
        ShowCursor();
        return 1;
    }

    int  volume   = 80;
    bool running  = true;

    while (running) {
        // ── Input ─────────────────────────────────────────────────────────────
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 'q' || ch == 'Q' || ch == 27) { running = false; break; }
            if (ch == ' ') {
                auto st = player->GetStatus();
                if (st.paused) player->Resume(); else player->Pause();
            }
            if (ch == '+' || ch == '=') {
                volume = std::min(100, volume + 5);
                player->SetMasterVolume(volume, volume);
            }
            if (ch == '-') {
                volume = std::max(0, volume - 5);
                player->SetMasterVolume(volume, volume);
            }
        }

        // ── Status line ───────────────────────────────────────────────────────
        libZFork::StreamTime pos{};
        player->GetPosition(pos);
        libZFork::VUData vu{};
        player->GetVUData(vu);
        auto st = player->GetStatus();

        GotoXY(0, specStartY + barRows * 2 + 2);
        SetColor(0x07);
        std::printf("  %s  %02u:%02u  Vol:%3d%%  VU: L=%.2f R=%.2f   ",
                    st.paused ? "[PAUSE]" : "[PLAY] ",
                    pos.hms.minute, pos.hms.second,
                    volume, vu.left, vu.right);

        // ── FFT ───────────────────────────────────────────────────────────────
        player->GetFFTData(fftL.data(), fftR.data(), fftBins / 2);
        DrawSpectrum(fftL.data(), fftR.data(), fftBins,
                     numBars, barRows, specStartY, peaksL, peaksR);

        if (!st.playing && !st.paused) break;
        Sleep(40); // ~25 fps
    }

    player->Stop();
    libZFork::DestroyZFork(player);
    ShowCursor();

    GotoXY(0, specStartY + barRows * 2 + 4);
    SetColor(0x07);
    std::printf("\nWiedergabe beendet.\n");
    return 0;
}
