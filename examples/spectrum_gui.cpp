/**
 * libZFork — Spectrum Analyzer (Win32 GUI)
 *
 * Visualization modes (click buttons or keys 1-4):
 *   Bars   — classic bars, L top / R bottom, peak hold
 *   Wave   — smooth filled curve, L/R mirrored
 *   Sparks — particle sparks emitted from bars
 *   Radial — circular bars, L top half / R bottom half
 *
 * Menu bar: File | Playback | Help (dark-themed, custom drawn)
 * Seekbar : click or drag the progress bar to seek
 * Keys    : Space=Pause  +/-=Vol  1-4=Mode  Ctrl+O=Open  Q=Quit
 * Drag & Drop to open files
 */

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <libzfork/libzfork.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comdlg32.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

using namespace libZFork;

// ── Menu command IDs ───────────────────────────────────────────────────────────
#define IDM_FILE_OPEN    1001
#define IDM_FILE_EXIT    1002
#define IDM_PB_PLAYPAUSE 1003
#define IDM_PB_STOP      1004
#define IDM_HELP_ABOUT   1005

// ── Layout constants ───────────────────────────────────────────────────────────
static constexpr int   kFFTBins   = 2048;
static constexpr int   kMaxBars   = 128;
static constexpr int   kUpdateMs  = 16;       // ~60 fps
static constexpr int   kPeakHold  = 60;
static constexpr float kPeakDecay = 0.008f;
static constexpr float kSmooth    = 0.28f;
static constexpr int   kWinW      = 980;
static constexpr int   kWinH      = 600;
static constexpr int   kMenuBarH  = 22;       // custom app menu bar
static constexpr int   kModeBarH  = 30;       // vis-mode selector
static constexpr int   kInfoH     = 74;       // title / info / seekbar
static constexpr int   kFreqLblH  = 20;       // freq labels + branding row
static constexpr int   kMarginX   = 14;
static constexpr int   kMaxSparks = 1400;
static constexpr int   kBarsMin   = 16;
static constexpr int   kBarsMax   = 120;
static constexpr int   kBarsStep  = 4;

// ── Visualization modes ────────────────────────────────────────────────────────
enum class VisMode { Bars, Wave, Sparks, Radial };
static VisMode      g_visMode          = VisMode::Bars;
static const char*  kModeNames[]       = { "Bars", "Wave", "Sparks", "Radial" };
static const char*  kModeKeys[]        = { "1", "2", "3", "4" };
static constexpr int kNumModes         = 4;
static int          g_modeBtnX[kNumModes] = {};
static int          g_modeBtnW[kNumModes] = {};

// ── Palette ────────────────────────────────────────────────────────────────────
static constexpr COLORREF kBg         = RGB( 10,  10,  16);
static constexpr COLORREF kBgMenu     = RGB( 16,  16,  26);
static constexpr COLORREF kBgMenuHov  = RGB( 28,  32,  52);
static constexpr COLORREF kGrid       = RGB( 20,  20,  32);
static constexpr COLORREF kSep        = RGB( 38,  38,  58);
static constexpr COLORREF kBarLow     = RGB(  0, 210,  80);
static constexpr COLORREF kBarMid     = RGB(230, 185,   0);
static constexpr COLORREF kBarHigh    = RGB(225,  38,  22);
static constexpr COLORREF kPeakDot    = RGB(255, 255, 255);
static constexpr COLORREF kTextMain   = RGB(215, 215, 232);
static constexpr COLORREF kTextDim    = RGB( 88,  88, 114);
static constexpr COLORREF kTextActive = RGB(255, 255, 255);
static constexpr COLORREF kAccent     = RGB( 75, 135, 255);
static constexpr COLORREF kProgress   = RGB( 55, 115, 255);
static constexpr COLORREF kProgressBg = RGB( 28,  28,  46);
static constexpr COLORREF kWaveL      = RGB(  0, 200,  75);
static constexpr COLORREF kWaveR      = RGB( 30, 120, 255);
static constexpr COLORREF kSeekThumb  = RGB(200, 220, 255);

// ── Global player state ────────────────────────────────────────────────────────
static ZFork*    g_player      = nullptr;
static bool      g_hasFile     = false;
static int       g_volume      = 80;
static char      g_title [256] = {};
static char      g_artist[256] = {};
static int       g_bitrate     = 0;
static int       g_sampleRate  = 44100;
static int       g_channels    = 2;
static uint32_t  g_totalMs     = 0;
static int       g_numBars     = 80;

static float g_fftL   [kFFTBins / 2] = {};
static float g_fftR   [kFFTBins / 2] = {};
static float g_smoothL[kMaxBars]     = {};
static float g_smoothR[kMaxBars]     = {};
static float g_peakL  [kMaxBars]     = {};
static float g_peakR  [kMaxBars]     = {};
static int   g_holdL  [kMaxBars]     = {};
static int   g_holdR  [kMaxBars]     = {};

// ── Spinner rects (set each frame during DrawModeBar) ──────────────────────────
static RECT g_spinMinus = {};
static RECT g_spinPlus  = {};
static RECT g_spinArea  = {};

// ── Seekbar ────────────────────────────────────────────────────────────────────
static RECT g_seekRect = {};          // padded hit area (client coords)
static bool g_seeking  = false;       // LMB held on seekbar

// ── Custom menu bar ────────────────────────────────────────────────────────────
static HMENU g_hmenuFile = nullptr;   // "File" popup
static HMENU g_hmenuPB   = nullptr;   // "Playback" popup
static HMENU g_hmenuHelp = nullptr;   // "Help" popup
static RECT  g_menuBtns[3] = {};      // client rects of File / Playback / Help

// ── About window ──────────────────────────────────────────────────────────────
static HWND g_aboutHwnd = nullptr;

// ── Spark particles ────────────────────────────────────────────────────────────
struct Spark { float x, y, vy, life; COLORREF color; };
static Spark g_sparks   [kMaxSparks] = {};
static int   g_sparkCount            = 0;

// ══════════════════════════════════════════════════════════════════════════════
//  GDI helpers
// ══════════════════════════════════════════════════════════════════════════════

static COLORREF LerpColor(COLORREF a, COLORREF b, float t) {
    return RGB(
        GetRValue(a) + (int)((GetRValue(b) - GetRValue(a)) * t),
        GetGValue(a) + (int)((GetGValue(b) - GetGValue(a)) * t),
        GetBValue(a) + (int)((GetBValue(b) - GetBValue(a)) * t));
}

static COLORREF BarColor(float v) {
    return v < 0.55f ? LerpColor(kBarLow, kBarMid, v / 0.55f)
                     : LerpColor(kBarMid, kBarHigh, (v - 0.55f) / 0.45f);
}

// Fast fill — no CreateSolidBrush, uses DC_BRUSH stock object
static void FR(HDC dc, int x, int y, int w, int h, COLORREF c) {
    SetDCBrushColor(dc, c);
    RECT rc = { x, y, x + w, y + h };
    FillRect(dc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
}

static void Line(HDC dc, int x0, int y0, int x1, int y1, COLORREF c, int w = 1) {
    HPEN p  = CreatePen(PS_SOLID, w, c);
    HPEN op = (HPEN)SelectObject(dc, p);
    MoveToEx(dc, x0, y0, nullptr);
    LineTo(dc, x1, y1);
    SelectObject(dc, op);
    DeleteObject(p);
}

static HFONT MkFont(int sz, int wt, const char* face) {
    return CreateFontA(sz, 0, 0, 0, wt, 0, 0, 0, ANSI_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, face);
}

// ══════════════════════════════════════════════════════════════════════════════
//  Seek helper
// ══════════════════════════════════════════════════════════════════════════════

static void SeekToX(int mx) {
    if (!g_player || !g_hasFile || g_totalMs == 0) return;
    int pbW = g_seekRect.right - g_seekRect.left;
    if (pbW <= 0) return;
    float ratio = std::clamp((float)(mx - g_seekRect.left) / pbW, 0.f, 1.f);
    uint32_t ms = (uint32_t)(ratio * g_totalMs);
    StreamTime t{};
    t.ms  = ms;
    t.sec = ms / 1000;
    g_player->Seek(SeekOrigin::Begin, t);
}

// ══════════════════════════════════════════════════════════════════════════════
//  Bar processing
// ══════════════════════════════════════════════════════════════════════════════

static void UpdateBars(bool active) {
    if (active && g_player)
        g_player->GetFFTData(g_fftL, g_fftR, kFFTBins / 2);

    for (int b = 0; b < g_numBars; ++b) {
        float t0 = (float)b       / g_numBars;
        float t1 = (float)(b + 1) / g_numBars;
        int k0 = (int)(t0 * t0 * (kFFTBins / 2));
        int k1 = std::max(k0 + 1, (int)(t1 * t1 * (kFFTBins / 2)));
        k1 = std::min(k1, kFFTBins / 2);
        float sL = 0, sR = 0;
        for (int k = k0; k < k1; ++k) { sL += g_fftL[k]; sR += g_fftR[k]; }
        float inv = 1.f / std::max(1, k1 - k0);
        float rL  = std::pow(std::clamp(sL * inv * 14.f, 0.f, 1.f), 0.55f);
        float rR  = std::pow(std::clamp(sR * inv * 14.f, 0.f, 1.f), 0.55f);
        g_smoothL[b] = g_smoothL[b] * (1.f - kSmooth) + rL * kSmooth;
        g_smoothR[b] = g_smoothR[b] * (1.f - kSmooth) + rR * kSmooth;
        auto upd = [](float v, float& pk, int& hold) {
            if (v >= pk) { pk = v; hold = kPeakHold; }
            else if (hold > 0) --hold;
            else pk = std::max(0.f, pk - kPeakDecay);
        };
        upd(g_smoothL[b], g_peakL[b], g_holdL[b]);
        upd(g_smoothR[b], g_peakR[b], g_holdR[b]);
    }
}

// ══════════════════════════════════════════════════════════════════════════════
//  Spark physics
// ══════════════════════════════════════════════════════════════════════════════

static void UpdateSparks(int halfH, int midY, int startX, int barW) {
    for (int b = 0; b < g_numBars && g_sparkCount < kMaxSparks - 4; ++b) {
        auto emit = [&](float val, float sy, float dir) {
            if (val < 0.07f) return;
            int n = std::min(2, (int)(val * 3));
            for (int i = 0; i < n && g_sparkCount < kMaxSparks; ++i) {
                auto& s = g_sparks[g_sparkCount++];
                s.x    = (float)(startX + b * barW + rand() % barW);
                s.y    = sy;
                s.vy   = dir * (1.2f + val * 5.f + (rand() % 80) * 0.025f);
                s.life = 0.75f + (rand() % 25) * 0.01f;
                s.color = BarColor(val);
            }
        };
        emit(g_smoothL[b], (float)(midY - (int)(g_smoothL[b] * halfH)), -1.f);
        emit(g_smoothR[b], (float)(midY + 4 + (int)(g_smoothR[b] * halfH)), +1.f);
    }
    int alive = 0;
    for (int i = 0; i < g_sparkCount; ++i) {
        auto& s = g_sparks[i];
        s.y += s.vy; s.vy += 0.22f; s.life -= 0.020f;
        if (s.life > 0.f) g_sparks[alive++] = s;
    }
    g_sparkCount = alive;
}

// ══════════════════════════════════════════════════════════════════════════════
//  Dark mode (DWM + UxTheme)
// ══════════════════════════════════════════════════════════════════════════════

static void ApplyDarkMode(HWND hwnd) {
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                                     &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
    HMODULE hUx = GetModuleHandleA("uxtheme.dll");
    if (!hUx) hUx = LoadLibraryA("uxtheme.dll");
    if (hUx) {
        using FnSPAM = int(WINAPI*)(int);
        if (auto fn = reinterpret_cast<FnSPAM>(GetProcAddress(hUx, MAKEINTRESOURCEA(135))))
            fn(2); // ForceDark
        using FnADMW = BOOL(WINAPI*)(HWND, BOOL);
        if (auto fn = reinterpret_cast<FnADMW>(GetProcAddress(hUx, MAKEINTRESOURCEA(133))))
            fn(hwnd, TRUE);
        using FnRICP = void(WINAPI*)();
        if (auto fn = reinterpret_cast<FnRICP>(GetProcAddress(hUx, MAKEINTRESOURCEA(104))))
            fn();
        using FnFMT = void(WINAPI*)();
        if (auto fn = reinterpret_cast<FnFMT>(GetProcAddress(hUx, MAKEINTRESOURCEA(136))))
            fn();
    }
}

// ══════════════════════════════════════════════════════════════════════════════
//  Open audio file
// ══════════════════════════════════════════════════════════════════════════════

static void OpenAudioFile(const wchar_t* path, HWND hwnd) {
    if (!g_player) return;
    g_player->Stop();
    if (!g_player->OpenFile(path, StreamFormat::Autodetect)) {
        MessageBoxA(hwnd, g_player->GetError(), "libZFork", MB_ICONERROR);
        g_hasFile = false;
        return;
    }
    ID3InfoEx  tag{}; g_player->LoadID3Ex(0, tag);
    StreamInfo inf{}; g_player->GetStreamInfo(0, inf);
    strncpy_s(g_title,  tag.title[0]  ? tag.title  : "(no title)", 255);
    strncpy_s(g_artist, tag.artist[0] ? tag.artist : "",            255);
    g_bitrate    = inf.bitrate;
    g_sampleRate = inf.samplingRate > 0 ? inf.samplingRate : 44100;
    g_channels   = inf.channelNumber;
    g_totalMs    = inf.length.ms;
    memset(g_fftL,    0, sizeof g_fftL);    memset(g_fftR,    0, sizeof g_fftR);
    memset(g_smoothL, 0, sizeof g_smoothL); memset(g_smoothR, 0, sizeof g_smoothR);
    memset(g_peakL,   0, sizeof g_peakL);   memset(g_peakR,   0, sizeof g_peakR);
    memset(g_holdL,   0, sizeof g_holdL);   memset(g_holdR,   0, sizeof g_holdR);
    g_sparkCount = 0;
    g_player->SetFFTPoints(kFFTBins);
    g_player->SetFFTWindow(FFTWindow::Hann);
    g_player->SetMasterVolume(g_volume, g_volume);
    g_hasFile = true;
    g_player->Play();
    wchar_t wt[512];
    swprintf_s(wt, L"libZFork Spectrum  -  %hs", g_title);
    SetWindowTextW(hwnd, wt);
}

// ── Open-file dialog ───────────────────────────────────────────────────────────
static void OpenFileDialog(HWND hwnd) {
    OPENFILENAMEW ofn{};
    wchar_t path[MAX_PATH] = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter =
        L"Audio Files\0*.mp3;*.ogg;*.wav;*.flac;*.aac;*.opus;*.aiff\0"
        L"All Files\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = L"Open Audio File";
    if (GetOpenFileNameW(&ofn))
        OpenAudioFile(path, hwnd);
}

// ══════════════════════════════════════════════════════════════════════════════
//  About window
// ══════════════════════════════════════════════════════════════════════════════

static LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        ApplyDarkMode(hwnd);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        const int W = rc.right, H = rc.bottom;

        // Background
        FR(dc, 0, 0, W, H, RGB(14, 14, 22));
        // Top accent stripe
        FR(dc, 0, 0, W, 3, kAccent);
        // Border
        {
            HPEN bp  = CreatePen(PS_SOLID, 1, kSep);
            HPEN obp = (HPEN)SelectObject(dc, bp);
            SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, 0, 0, W, H);
            SelectObject(dc, obp); DeleteObject(bp);
        }

        SetBkMode(dc, TRANSPARENT);

        // Title
        HFONT fBig = MkFont(22, FW_BOLD,   "Segoe UI");
        HFONT fMed = MkFont(13, FW_NORMAL, "Segoe UI");
        HFONT fSml = MkFont(11, FW_NORMAL, "Segoe UI");

        SelectObject(dc, fBig);
        SetTextColor(dc, kAccent);
        {
            const char* t = "libZFork Spectrum Analyzer";
            SIZE sz{}; GetTextExtentPoint32A(dc, t, (int)strlen(t), &sz);
            TextOutA(dc, (W - sz.cx) / 2, 18, t, (int)strlen(t));
        }

        // Version
        SelectObject(dc, fMed);
        SetTextColor(dc, kTextMain);
        {
            const char* t = "Version " LIBZFORK_VERSION_STRING;
            SIZE sz{}; GetTextExtentPoint32A(dc, t, (int)strlen(t), &sz);
            TextOutA(dc, (W - sz.cx) / 2, 56, t, (int)strlen(t));
        }

        // Separator
        Line(dc, 28, 84, W - 28, 84, kSep);

        // Info lines
        SelectObject(dc, fSml);
        struct InfoLine { const char* text; COLORREF color; };
        static const InfoLine lines[] = {
            { "Built on libZFork - modern C++20 audio engine",    0 },
            { "Based on libZPlay 2.02 by Zoran Cindori (GPLv2)", 0 },
            { "",                                                  0 },
            { "https://github.com/thunderbird2013/libZFork",      0 },
            { "",                                                  0 },
            { "(c) 2025  |  All rights reserved",                 0 },
            { "",                                                  0 },
            { "Click or press any key to close",                  0 },
        };
        int ty = 96;
        for (auto& li : lines) {
            if (li.text[0]) {
                bool isUrl = (li.text[0] == 'h');
                SetTextColor(dc, isUrl ? kAccent : kTextDim);
                SIZE sz{}; GetTextExtentPoint32A(dc, li.text, (int)strlen(li.text), &sz);
                TextOutA(dc, (W - sz.cx) / 2, ty, li.text, (int)strlen(li.text));
                if (isUrl) {
                    // underline
                    Line(dc, (W - sz.cx) / 2, ty + sz.cy,
                             (W + sz.cx) / 2, ty + sz.cy, kAccent);
                }
            }
            ty += 18;
        }

        DeleteObject(fBig); DeleteObject(fMed); DeleteObject(fSml);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_KEYDOWN:
    case WM_LBUTTONDOWN:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        g_aboutHwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ShowAbout(HWND parent) {
    if (g_aboutHwnd) { SetForegroundWindow(g_aboutHwnd); return; }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = AboutWndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"libZForkAbout";
        RegisterClassExW(&wc);
        registered = true;
    }
    // Center over parent
    RECT pr; GetWindowRect(parent, &pr);
    const int W = 440, H = 280;
    int x = pr.left + (pr.right  - pr.left - W) / 2;
    int y = pr.top  + (pr.bottom - pr.top  - H) / 2;
    g_aboutHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        L"libZForkAbout", L"About libZFork",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, W, H, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    ShowWindow(g_aboutHwnd, SW_SHOW);
    SetForegroundWindow(g_aboutHwnd);
}

// ══════════════════════════════════════════════════════════════════════════════
//  Draw: custom app menu bar  (File | Playback | Help)
// ══════════════════════════════════════════════════════════════════════════════

static void DrawAppMenuBar(HDC dc, int W) {
    FR(dc, 0, 0, W, kMenuBarH, kBgMenu);
    Line(dc, 0, kMenuBarH - 1, W, kMenuBarH - 1, kSep);

    static const char* kItems[] = { "File", "Playback", "Help" };
    HFONT f = MkFont(13, FW_NORMAL, "Segoe UI");
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);

    int x = 4;
    for (int i = 0; i < 3; ++i) {
        SIZE sz{};
        GetTextExtentPoint32A(dc, kItems[i], (int)strlen(kItems[i]), &sz);
        int bw = sz.cx + 18;
        g_menuBtns[i] = { x, 0, x + bw, kMenuBarH };
        SetTextColor(dc, kTextDim);
        TextOutA(dc, x + 9, (kMenuBarH - sz.cy) / 2, kItems[i], (int)strlen(kItems[i]));
        x += bw + 2;
    }
    DeleteObject(f);
}

// ══════════════════════════════════════════════════════════════════════════════
//  Draw: vis-mode selector bar  (y offset = kMenuBarH)
// ══════════════════════════════════════════════════════════════════════════════

static void DrawModeBar(HDC dc, int W) {
    const int y0 = kMenuBarH;
    FR(dc, 0, y0, W, kModeBarH, kBgMenu);
    Line(dc, 0, y0 + kModeBarH - 1, W, y0 + kModeBarH - 1, kSep);

    HFONT f = MkFont(13, FW_NORMAL, "Segoe UI");
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);

    int x = kMarginX;
    for (int i = 0; i < kNumModes; ++i) {
        char lbl[32]; sprintf_s(lbl, "%s %s", kModeKeys[i], kModeNames[i]);
        SIZE sz{}; GetTextExtentPoint32A(dc, lbl, (int)strlen(lbl), &sz);
        int bw = sz.cx + 22;
        g_modeBtnX[i] = x; g_modeBtnW[i] = bw;

        bool act = ((int)g_visMode == i);
        if (act) {
            FR(dc, x, y0 + 3, bw, kModeBarH - 6, kBgMenuHov);
            FR(dc, x + 3, y0 + kModeBarH - 3, bw - 6, 2, kAccent);
        }
        SetTextColor(dc, act ? kTextActive : kTextDim);
        TextOutA(dc, x + 11, y0 + (kModeBarH - sz.cy) / 2, lbl, (int)strlen(lbl));
        x += bw + 6;
    }

    // ── Bars spinner (right-aligned) ──────────────────────────────────────────
    char numStr[8]; sprintf_s(numStr, "%d", g_numBars);
    SIZE szN{}, szL{};
    GetTextExtentPoint32A(dc, numStr, (int)strlen(numStr), &szN);
    GetTextExtentPoint32A(dc, "Bars", 4, &szL);

    const int btnW  = 18, padX = 6;
    const int totalW = szL.cx + padX + btnW + padX + szN.cx + padX + btnW + 4;
    int sx = W - kMarginX - totalW;
    int sy = y0 + (kModeBarH - szL.cy) / 2;

    SetTextColor(dc, kTextDim);
    TextOutA(dc, sx, sy, "Bars", 4);
    sx += szL.cx + padX;

    g_spinArea = { sx, y0 + 2, sx + btnW + padX + szN.cx + padX + btnW, y0 + kModeBarH - 2 };

    RECT rM = { sx, y0 + 4, sx + btnW, y0 + kModeBarH - 4 };
    g_spinMinus = rM;
    FR(dc, rM.left, rM.top, rM.right - rM.left, rM.bottom - rM.top, kBgMenuHov);
    SetTextColor(dc, g_numBars <= kBarsMin ? kTextDim : kTextActive);
    TextOutA(dc, rM.left + (btnW - 6) / 2, sy, "<", 1);
    sx += btnW + padX;

    SetTextColor(dc, kTextMain);
    TextOutA(dc, sx, sy, numStr, (int)strlen(numStr));
    sx += szN.cx + padX;

    RECT rP = { sx, y0 + 4, sx + btnW, y0 + kModeBarH - 4 };
    g_spinPlus = rP;
    FR(dc, rP.left, rP.top, rP.right - rP.left, rP.bottom - rP.top, kBgMenuHov);
    SetTextColor(dc, g_numBars >= kBarsMax ? kTextDim : kTextActive);
    TextOutA(dc, rP.left + (btnW - 6) / 2, sy, ">", 1);

    DeleteObject(f);
}

// ══════════════════════════════════════════════════════════════════════════════
//  Draw: info panel with seekbar
// ══════════════════════════════════════════════════════════════════════════════

static void DrawInfoPanel(HDC dc, int W, const StreamStatus& st) {
    const int y = kMenuBarH + kModeBarH;
    HFONT fT = MkFont(17, FW_SEMIBOLD, "Segoe UI");
    HFONT fI = MkFont(13, FW_NORMAL,   "Segoe UI");
    SetBkMode(dc, TRANSPARENT);

    if (g_hasFile) {
        // Title
        SelectObject(dc, fT);
        SetTextColor(dc, kTextMain);
        TextOutA(dc, kMarginX, y + 7, g_title, (int)strlen(g_title));

        // Sub-line  artist / technical
        char sub[256];
        if (g_artist[0])
            sprintf_s(sub, "%s   %d Hz / %d ch / %d kbps",
                      g_artist, g_sampleRate, g_channels, g_bitrate);
        else
            sprintf_s(sub, "%d Hz / %d ch / %d kbps",
                      g_sampleRate, g_channels, g_bitrate);
        SelectObject(dc, fI);
        SetTextColor(dc, kTextDim);
        TextOutA(dc, kMarginX, y + 29, sub, (int)strlen(sub));

        // Position text
        StreamTime pos{};
        if (g_player) g_player->GetPosition(pos);
        uint32_t tot = g_totalMs / 1000;
        char tstr[48];
        sprintf_s(tstr, "%02u:%02u  /  %02u:%02u",
                  pos.hms.minute, pos.hms.second, tot / 60, tot % 60);
        SetTextColor(dc, kTextMain);
        TextOutA(dc, kMarginX, y + 49, tstr, (int)strlen(tstr));

        // Status right-aligned
        const char* icon = st.paused ? "|| Pause" : (st.playing ? "> Play" : "[] Stop");
        char ss[48]; sprintf_s(ss, "%s  Vol %d%%", icon, g_volume);
        SIZE sz{}; GetTextExtentPoint32A(dc, ss, (int)strlen(ss), &sz);
        SetTextColor(dc, kTextDim);
        TextOutA(dc, W - sz.cx - kMarginX, y + 7, ss, (int)strlen(ss));

        // ── Seekbar ────────────────────────────────────────────────────────────
        float pr   = g_totalMs > 0
                     ? std::clamp((float)pos.ms / g_totalMs, 0.f, 1.f)
                     : 0.f;
        const int pbW  = W - kMarginX * 2;
        const int pbY  = y + kInfoH - 16;   // bar top
        const int pbH  = 6;                  // bar height

        // Extended hit area (easier to click)
        g_seekRect = { kMarginX, pbY - 6, kMarginX + pbW, pbY + pbH + 6 };

        // Track
        FR(dc, kMarginX, pbY, pbW, pbH, kProgressBg);

        // Fill
        int fillW = (int)(pbW * pr);
        if (fillW > 0) {
            // Gradient-like: slightly brighter at head
            FR(dc, kMarginX, pbY, fillW, pbH, kProgress);
        }

        // Thumb knob — 4px wide, 14px tall, centered on track
        if (g_totalMs > 0) {
            int tx  = kMarginX + fillW;
            int tw  = 4;
            int th  = 14;
            int ty2 = pbY - (th - pbH) / 2;
            FR(dc, tx - tw / 2, ty2, tw, th, kSeekThumb);
        }
    } else {
        SelectObject(dc, fT);
        SetTextColor(dc, kTextDim);
        const char* h = "Drop a file here or use  File > Open  —  Space Pause  1-4 Mode  Q Quit";
        SIZE sz{}; GetTextExtentPoint32A(dc, h, (int)strlen(h), &sz);
        TextOutA(dc, (W - sz.cx) / 2, y + (kInfoH - sz.cy) / 2, h, (int)strlen(h));
    }

    Line(dc, 0, y + kInfoH, W, y + kInfoH, kSep);
    DeleteObject(fT);
    DeleteObject(fI);
}

// ══════════════════════════════════════════════════════════════════════════════
//  Draw: freq labels (left) + branding (right)
// ══════════════════════════════════════════════════════════════════════════════

static void DrawFreqLabels(HDC dc, int lblY, int startX, int barW, int nyquist, int W) {
    static const char*  L[]  = {"50","100","200","500","1k","2k","5k","10k","20k"};
    static const float  Hz[] = { 50,  100,  200,  500, 1000,2000,5000,10000,20000 };
    HFONT f = MkFont(11, FW_NORMAL, "Segoe UI");
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kTextDim);

    // Freq labels — left side
    for (int i = 0; i < 9; ++i) {
        if (Hz[i] >= (float)nyquist) break;
        int lx = startX + (int)(std::sqrt(Hz[i] / (float)nyquist) * g_numBars * barW) - 6;
        TextOutA(dc, lx, lblY, L[i], (int)strlen(L[i]));
    }

    // Branding — right side
    const char* brnd = "libZFork v1.0  |  (c) 2025  |  github.com/thunderbird2013/libZFork";
    SIZE sb{};
    GetTextExtentPoint32A(dc, brnd, (int)strlen(brnd), &sb);
    SetTextColor(dc, RGB(50, 50, 72));   // very dim, non-intrusive
    TextOutA(dc, W - sb.cx - kMarginX, lblY, brnd, (int)strlen(brnd));

    DeleteObject(f);
}

// ══════════════════════════════════════════════════════════════════════════════
//  Draw modes
// ══════════════════════════════════════════════════════════════════════════════

static void DrawBars(HDC dc, int specY, int halfH, int midY,
                     int startX, int barW, int barFW) {
    for (int i = 1; i <= 3; ++i) {
        Line(dc, startX, specY + halfH*i/4,
                 startX + barW*g_numBars, specY + halfH*i/4, kGrid);
        Line(dc, startX, midY+4+halfH*i/4,
                 startX + barW*g_numBars, midY+4+halfH*i/4, kGrid);
    }
    Line(dc, startX, midY+2, startX + barW*g_numBars, midY+2, kSep);

    for (int b = 0; b < g_numBars; ++b) {
        int   x  = startX + b * barW;
        float vL = g_smoothL[b], vR = g_smoothR[b];

        FR(dc, x, specY,    barFW, halfH, kBg);
        FR(dc, x, midY + 4, barFW, halfH, kBg);

        int hL = (int)(vL * halfH);
        int hR = (int)(vR * halfH);
        if (hL > 1) FR(dc, x, midY - hL, barFW, hL, BarColor(vL));
        if (hR > 1) FR(dc, x, midY + 4,  barFW, hR, BarColor(vR));

        if (g_peakL[b] > 0.01f) {
            int py = midY - (int)(g_peakL[b] * halfH) - 2;
            if (py >= specY) FR(dc, x, py, barFW, 2, kPeakDot);
        }
        if (g_peakR[b] > 0.01f) {
            int py = midY + 4 + (int)(g_peakR[b] * halfH) + 1;
            if (py + 2 <= midY + 4 + halfH) FR(dc, x, py, barFW, 2, kPeakDot);
        }
    }
}

static void DrawWave(HDC dc, int specY, int halfH, int midY, int startX, int barW) {
    (void)specY;
    Line(dc, startX, midY + 2, startX + barW * g_numBars, midY + 2, kSep);

    POINT pL[kMaxBars + 2], pR[kMaxBars + 2];
    for (int b = 0; b < g_numBars; ++b) {
        int x  = startX + b * barW + barW / 2;
        pL[b] = { x, midY     - (int)(g_smoothL[b] * halfH) };
        pR[b] = { x, midY + 4 + (int)(g_smoothR[b] * halfH) };
    }
    pL[g_numBars]   = { startX + (g_numBars - 1) * barW + barW / 2, midY };
    pL[g_numBars+1] = { startX + barW / 2,                           midY };
    pR[g_numBars]   = { startX + (g_numBars - 1) * barW + barW / 2, midY + 4 };
    pR[g_numBars+1] = { startX + barW / 2,                           midY + 4 };

    SelectObject(dc, GetStockObject(NULL_PEN));
    SetDCBrushColor(dc, RGB(0, 55, 22));
    SelectObject(dc, GetStockObject(DC_BRUSH));
    Polygon(dc, pL, g_numBars + 2);
    SetDCBrushColor(dc, RGB(10, 30, 70));
    Polygon(dc, pR, g_numBars + 2);

    HPEN penL = CreatePen(PS_SOLID, 2, kWaveL);
    HPEN penR = CreatePen(PS_SOLID, 2, kWaveR);
    SelectObject(dc, penL); Polyline(dc, pL, g_numBars);
    SelectObject(dc, penR); Polyline(dc, pR, g_numBars);
    DeleteObject(penL); DeleteObject(penR);

    for (int b = 0; b < g_numBars; ++b) {
        int x = startX + b * barW + barW / 2;
        if (g_peakL[b] > 0.01f)
            FR(dc, x-1, midY-(int)(g_peakL[b]*halfH)-1, 3, 3, kPeakDot);
        if (g_peakR[b] > 0.01f)
            FR(dc, x-1, midY+4+(int)(g_peakR[b]*halfH)-1, 3, 3, kPeakDot);
    }
}

static void DrawSparks(HDC dc, int halfH, int midY, int startX, int barW, int barFW) {
    Line(dc, startX, midY+2, startX + barW*g_numBars, midY+2, kSep);
    for (int b = 0; b < g_numBars; ++b) {
        int x  = startX + b * barW;
        int hL = (int)(g_smoothL[b] * halfH * 0.35f);
        int hR = (int)(g_smoothR[b] * halfH * 0.35f);
        if (hL > 1) FR(dc, x, midY - hL, barFW, hL, RGB(0, 45, 18));
        if (hR > 1) FR(dc, x, midY + 4,  barFW, hR, RGB(5, 18, 55));
    }
    for (int i = 0; i < g_sparkCount; ++i) {
        const auto& s = g_sparks[i];
        COLORREF c = LerpColor(kBg, s.color, std::clamp(s.life * 1.8f, 0.f, 1.f));
        int sz = s.life > 0.55f ? 3 : 2;
        FR(dc, (int)s.x - sz/2, (int)s.y - sz/2, sz, sz, c);
    }
}

static void DrawRadial(HDC dc, int specY, int specH, int W) {
    constexpr float PI = 3.14159265f;
    int cx    = W / 2;
    int cy    = specY + specH / 2;
    int maxR  = std::min(specH, W / 2) / 2 - 8;
    int inner = maxR / 3;

    HPEN rp  = CreatePen(PS_SOLID, 1, kGrid);
    HPEN old = (HPEN)SelectObject(dc, rp);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, cx - maxR, cy - maxR, cx + maxR, cy + maxR);
    SelectObject(dc, old); DeleteObject(rp);

    for (int b = 0; b < g_numBars; ++b) {
        float angL = PI * (1.f + (float)b / g_numBars);
        float angR = PI * (float)b / g_numBars;
        float vL   = g_smoothL[b], vR = g_smoothR[b];
        float pL   = g_peakL[b],   pR = g_peakR[b];
        auto draw  = [&](float ang, float val, float peak) {
            float outerF = inner + val  * (maxR - inner);
            float peakF  = inner + peak * (maxR - inner);
            int x0 = cx + (int)(inner   * cosf(ang));
            int y0 = cy + (int)(inner   * sinf(ang));
            int x1 = cx + (int)(outerF  * cosf(ang));
            int y1 = cy + (int)(outerF  * sinf(ang));
            if (val  > 0.01f) Line(dc, x0, y0, x1, y1, BarColor(val), 2);
            if (peak > 0.02f) {
                int px = cx + (int)(peakF * cosf(ang));
                int py = cy + (int)(peakF * sinf(ang));
                FR(dc, px - 2, py - 2, 4, 4, kPeakDot);
            }
        };
        draw(angL, vL, pL);
        draw(angR, vR, pR);
    }

    SelectObject(dc, GetStockObject(NULL_PEN));
    SetDCBrushColor(dc, kBgMenu);
    SelectObject(dc, GetStockObject(DC_BRUSH));
    Ellipse(dc, cx - inner, cy - inner, cx + inner, cy + inner);

    HFONT f = MkFont(12, FW_NORMAL, "Segoe UI");
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kTextDim);
    TextOutA(dc, cx - 3, cy - inner / 2 - 7, "L", 1);
    TextOutA(dc, cx - 3, cy + inner / 2 - 7, "R", 1);
    DeleteObject(f);
}

// ══════════════════════════════════════════════════════════════════════════════
//  Main draw
// ══════════════════════════════════════════════════════════════════════════════

static void DrawFrame(HDC dc, int W, int H) {
    FR(dc, 0, 0, W, H, kBg);

    const int specY  = kMenuBarH + kModeBarH + kInfoH + 8;
    const int specH  = H - specY - kFreqLblH - 4;
    const int halfH  = specH / 2 - 2;
    const int midY   = specY + halfH;
    const int barW   = std::max(4, (W - kMarginX * 2) / g_numBars);
    const int barFW  = barW - 2;
    const int startX = kMarginX;
    const int nyq    = g_sampleRate / 2;

    StreamStatus st{};
    if (g_player) st = g_player->GetStatus();

    DrawAppMenuBar(dc, W);
    DrawModeBar(dc, W);
    DrawInfoPanel(dc, W, st);

    switch (g_visMode) {
    case VisMode::Bars:
        DrawBars(dc, specY, halfH, midY, startX, barW, barFW);
        DrawFreqLabels(dc, specY + specH + 4, startX, barW, nyq, W);
        break;
    case VisMode::Wave:
        DrawWave(dc, specY, halfH, midY, startX, barW);
        DrawFreqLabels(dc, specY + specH + 4, startX, barW, nyq, W);
        break;
    case VisMode::Sparks:
        DrawSparks(dc, halfH, midY, startX, barW, barFW);
        DrawFreqLabels(dc, specY + specH + 4, startX, barW, nyq, W);
        break;
    case VisMode::Radial:
        DrawRadial(dc, specY, specH, W);
        break;
    }
}

// ══════════════════════════════════════════════════════════════════════════════
//  Helper: reset bar state after bar-count change
// ══════════════════════════════════════════════════════════════════════════════

static void ResetBarState() {
    memset(g_smoothL, 0, sizeof g_smoothL); memset(g_smoothR, 0, sizeof g_smoothR);
    memset(g_peakL,   0, sizeof g_peakL);   memset(g_peakR,   0, sizeof g_peakR);
    memset(g_holdL,   0, sizeof g_holdL);   memset(g_holdR,   0, sizeof g_holdR);
}

// ══════════════════════════════════════════════════════════════════════════════
//  Window procedure
// ══════════════════════════════════════════════════════════════════════════════

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    // ── Init ──────────────────────────────────────────────────────────────────
    case WM_CREATE: {
        ApplyDarkMode(hwnd);
        DragAcceptFiles(hwnd, TRUE);
        SetTimer(hwnd, 1, kUpdateMs, nullptr);

        // Build popup menus (UxTheme dark mode makes these dark automatically)
        g_hmenuFile = CreatePopupMenu();
        AppendMenuW(g_hmenuFile, MF_STRING, IDM_FILE_OPEN, L"Open...\tCtrl+O");
        AppendMenuW(g_hmenuFile, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(g_hmenuFile, MF_STRING, IDM_FILE_EXIT, L"Exit\tAlt+F4");

        g_hmenuPB = CreatePopupMenu();
        AppendMenuW(g_hmenuPB, MF_STRING, IDM_PB_PLAYPAUSE, L"Play / Pause\tSpace");
        AppendMenuW(g_hmenuPB, MF_STRING, IDM_PB_STOP,      L"Stop");

        g_hmenuHelp = CreatePopupMenu();
        AppendMenuW(g_hmenuHelp, MF_STRING, IDM_HELP_ABOUT, L"About...");
        return 0;
    }

    // ── System dark-mode change ────────────────────────────────────────────────
    case WM_SETTINGCHANGE:
        if (lp && lstrcmpW(reinterpret_cast<LPCWSTR>(lp), L"ImmersiveColorSet") == 0) {
            ApplyDarkMode(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    // ── Menu commands ──────────────────────────────────────────────────────────
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_FILE_OPEN:
            OpenFileDialog(hwnd);
            break;
        case IDM_FILE_EXIT:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
        case IDM_PB_PLAYPAUSE:
            if (g_player && g_hasFile) {
                auto st = g_player->GetStatus();
                if      (st.paused)  g_player->Resume();
                else if (st.playing) g_player->Pause();
            }
            break;
        case IDM_PB_STOP:
            if (g_player) g_player->Stop();
            break;
        case IDM_HELP_ABOUT:
            ShowAbout(hwnd);
            break;
        }
        return 0;

    // ── Timer → update bars and redraw ────────────────────────────────────────
    case WM_TIMER: {
        StreamStatus st{};
        if (g_player) st = g_player->GetStatus();
        UpdateBars(g_hasFile && (st.playing || st.paused));
        if (g_visMode == VisMode::Sparks) {
            RECT rc; GetClientRect(hwnd, &rc);
            const int specY = kMenuBarH + kModeBarH + kInfoH + 8;
            const int specH = rc.bottom - specY - kFreqLblH - 4;
            const int halfH = specH / 2 - 2;
            const int midY  = specY + halfH;
            const int barW  = std::max(4, (int)(rc.right - kMarginX * 2) / g_numBars);
            UpdateSparks(halfH, midY, kMarginX, barW);
        } else {
            g_sparkCount = 0;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    // ── Paint (double-buffered) ────────────────────────────────────────────────
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        int W = rc.right, H = rc.bottom;
        HDC mdc   = CreateCompatibleDC(hdc);
        HBITMAP bm = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP ob = (HBITMAP)SelectObject(mdc, bm);
        DrawFrame(mdc, W, H);
        BitBlt(hdc, 0, 0, W, H, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, ob); DeleteObject(bm); DeleteDC(mdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    // ── Cursor: hand over seekbar ──────────────────────────────────────────────
    case WM_SETCURSOR: {
        POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
        if (g_hasFile && PtInRect(&g_seekRect, pt)) {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    }

    // ── Mouse down ────────────────────────────────────────────────────────────
    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        POINT pt = { mx, my };

        // ── App menu bar ──────────────────────────────────────────────────────
        if (my < kMenuBarH) {
            HMENU menus[3] = { g_hmenuFile, g_hmenuPB, g_hmenuHelp };
            for (int i = 0; i < 3; ++i) {
                if (PtInRect(&g_menuBtns[i], pt)) {
                    POINT sp = { g_menuBtns[i].left, g_menuBtns[i].bottom };
                    ClientToScreen(hwnd, &sp);
                    TrackPopupMenu(menus[i],
                                   TPM_TOPALIGN | TPM_LEFTALIGN | TPM_LEFTBUTTON,
                                   sp.x, sp.y, 0, hwnd, nullptr);
                    break;
                }
            }
            return 0;
        }

        // ── Vis-mode buttons & spinner ────────────────────────────────────────
        if (my >= kMenuBarH && my < kMenuBarH + kModeBarH) {
            for (int i = 0; i < kNumModes; ++i) {
                if (mx >= g_modeBtnX[i] && mx < g_modeBtnX[i] + g_modeBtnW[i]) {
                    g_visMode = (VisMode)i;
                    g_sparkCount = 0;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                }
            }
            if (PtInRect(&g_spinMinus, pt)) {
                g_numBars = std::max(kBarsMin, g_numBars - kBarsStep);
                ResetBarState();
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (PtInRect(&g_spinPlus, pt)) {
                g_numBars = std::min(kBarsMax, g_numBars + kBarsStep);
                ResetBarState();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        // ── Seekbar ───────────────────────────────────────────────────────────
        if (PtInRect(&g_seekRect, pt)) {
            g_seeking = true;
            SetCapture(hwnd);
            SeekToX(mx);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    // ── Mouse move → drag-seek ────────────────────────────────────────────────
    case WM_MOUSEMOVE:
        if (g_seeking) {
            SeekToX(GET_X_LPARAM(lp));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    // ── Mouse up → end seek ───────────────────────────────────────────────────
    case WM_LBUTTONUP:
        if (g_seeking) {
            g_seeking = false;
            ReleaseCapture();
        }
        return 0;

    // ── Mouse wheel → spinner ─────────────────────────────────────────────────
    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        if (PtInRect(&g_spinArea, pt)) {
            int delta = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? kBarsStep : -kBarsStep;
            g_numBars = std::clamp(g_numBars + delta, kBarsMin, kBarsMax);
            ResetBarState();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    // ── Drag & Drop ───────────────────────────────────────────────────────────
    case WM_DROPFILES: {
        HDROP hd = (HDROP)wp;
        wchar_t path[MAX_PATH];
        if (DragQueryFileW(hd, 0, path, MAX_PATH))
            OpenAudioFile(path, hwnd);
        DragFinish(hd);
        return 0;
    }

    // ── Keyboard ──────────────────────────────────────────────────────────────
    case WM_KEYDOWN:
        switch (wp) {
        case VK_SPACE:
            if (g_player && g_hasFile) {
                auto st = g_player->GetStatus();
                if      (st.paused)  g_player->Resume();
                else if (st.playing) g_player->Pause();
            }
            break;
        case 'O':
            if (GetKeyState(VK_CONTROL) & 0x8000)
                OpenFileDialog(hwnd);
            break;
        case VK_OEM_PLUS:  case VK_ADD:
            g_volume = std::min(100, g_volume + 5);
            if (g_player) g_player->SetMasterVolume(g_volume, g_volume);
            break;
        case VK_OEM_MINUS: case VK_SUBTRACT:
            g_volume = std::max(0, g_volume - 5);
            if (g_player) g_player->SetMasterVolume(g_volume, g_volume);
            break;
        case '1': g_visMode = VisMode::Bars;   g_sparkCount = 0; break;
        case '2': g_visMode = VisMode::Wave;   g_sparkCount = 0; break;
        case '3': g_visMode = VisMode::Sparks;                   break;
        case '4': g_visMode = VisMode::Radial; g_sparkCount = 0; break;
        case 'Q': case VK_ESCAPE:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
        }
        return 0;

    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    // ── Cleanup ───────────────────────────────────────────────────────────────
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (g_hmenuFile) { DestroyMenu(g_hmenuFile); g_hmenuFile = nullptr; }
        if (g_hmenuPB)   { DestroyMenu(g_hmenuPB);   g_hmenuPB   = nullptr; }
        if (g_hmenuHelp) { DestroyMenu(g_hmenuHelp); g_hmenuHelp = nullptr; }
        if (g_aboutHwnd) { DestroyWindow(g_aboutHwnd); g_aboutHwnd = nullptr; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ══════════════════════════════════════════════════════════════════════════════
//  Entry point
// ══════════════════════════════════════════════════════════════════════════════

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmd, int) {
    g_player = CreateZFork();
    if (!g_player) {
        MessageBoxA(nullptr, "CreateZFork() failed", "Error", MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(kBg);
    wc.lpszClassName = L"libZForkSpectrum";
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"libZForkSpectrum",
        L"libZFork — Spectrum Analyzer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, kWinW, kWinH,
        nullptr, nullptr, hInst, nullptr);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Open file from command line
    if (lpCmd && lpCmd[0]) {
        wchar_t path[MAX_PATH];
        if (lpCmd[0] == L'"') {
            wcsncpy_s(path, lpCmd + 1, MAX_PATH - 1);
            wchar_t* e = wcschr(path, L'"'); if (e) *e = 0;
        } else {
            wcsncpy_s(path, lpCmd, MAX_PATH - 1);
        }
        OpenAudioFile(path, hwnd);
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (g_player) { g_player->Stop(); DestroyZFork(g_player); }
    return 0;
}
