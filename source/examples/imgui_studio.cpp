/**
 * imgui_studio.cpp — libZFork ImGui Studio v2  (Traktor-Style)
 * =============================================================
 * Layout:
 *   Oben   : Wellenform Deck A (links) | Deck B (rechts)  — Echtzeit-Scroll
 *   Mitte  : Deck-A-Controls | Crossfader | Deck-B-Controls
 *   Unten  : Timeline — Clips, Fade-In/Out per Maus ziehbar, WAV-Export
 *   Ganz unten: Statusleiste
 *
 * Wellenform-Farben (wie Traktor):
 *   Bass-lastiger Inhalt  → Orange-Rot   (#FF8020)
 *   Treble-lastiger Inhalt → Cyan-Blau   (#20A0FF)
 *   Oben: L-Kanal-Peaks, unten: R-Kanal-Peaks (gespiegelt)
 */

#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <d3d11.h>

#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "comdlg32")

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include "core/deck_manager.h"
#include "timeline/timeline.h"
#include "timeline/timeline_player.h"
#include "decoders/decoder_base.h"
#include "decoders/decoder_factory.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace libZFork;

// ── DX11 ─────────────────────────────────────────────────────────────────────

static ID3D11Device*           g_pd3dDevice          = nullptr;
static ID3D11DeviceContext*    g_pd3dDeviceContext    = nullptr;
static IDXGISwapChain*         g_pSwapChain           = nullptr;
static UINT                    g_ResizeWidth          = 0;
static UINT                    g_ResizeHeight         = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static HWND                    g_hwnd                 = nullptr;

// ── Drag-&-Drop ───────────────────────────────────────────────────────────────
struct PendingDrop { std::wstring path; POINT pt{}; };
static std::vector<PendingDrop> g_pendingDrops;

static void CreateRenderTarget() {
    ID3D11Texture2D* pBack = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBack));
    if (pBack) {
        g_pd3dDevice->CreateRenderTargetView(pBack, nullptr, &g_mainRenderTargetView);
        pBack->Release();
    }
}
static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}
static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            levels, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext);
    if (hr != S_OK) return false;
    CreateRenderTarget();
    return true;
}
static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain)        { g_pSwapChain->Release();        g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release();  g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice)        { g_pd3dDevice->Release();         g_pd3dDevice = nullptr; }
}
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wP, LPARAM lP) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wP, lP)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wP != SIZE_MINIMIZED) { g_ResizeWidth = LOWORD(lP); g_ResizeHeight = HIWORD(lP); }
        return 0;
    case WM_SYSCOMMAND:
        if ((wP & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DROPFILES: {
        HDROP hDrop = reinterpret_cast<HDROP>(wP);
        UINT  n     = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        POINT pt{}; DragQueryPoint(hDrop, &pt);
        for (UINT i = 0; i < n; ++i) {
            wchar_t buf[MAX_PATH]{};
            if (DragQueryFileW(hDrop, i, buf, MAX_PATH) > 0)
                g_pendingDrops.push_back({ std::wstring(buf), pt });
        }
        DragFinish(hDrop);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wP, lP);
}

// ── Plattform-Helfer ──────────────────────────────────────────────────────────

static std::wstring FileOpenDialog() {
    wchar_t buf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hwnd;
    ofn.lpstrFilter = L"Audio\0*.mp3;*.wav;*.ogg;*.flac;*.aac;*.opus\0Alle\0*.*\0\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&ofn) ? std::wstring(buf) : std::wstring{};
}
static std::wstring FileSaveDialog() {
    wchar_t buf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hwnd;
    ofn.lpstrFilter = L"WAV\0*.wav\0\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = L"wav";
    ofn.Flags       = OFN_OVERWRITEPROMPT;
    return GetSaveFileNameW(&ofn) ? std::wstring(buf) : std::wstring{};
}
static std::string WtoU8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), -1, s.data(), n, nullptr, nullptr);
    return s;
}
static std::string Basename(const std::wstring& p) {
    auto u = WtoU8(p);
    auto pos = u.find_last_of("/\\");
    return (pos == std::string::npos) ? u : u.substr(pos + 1);
}
static std::string FormatTime(double sec) {
    int m  = static_cast<int>(sec) / 60;
    int s  = static_cast<int>(sec) % 60;
    int ms = static_cast<int>((sec - std::floor(sec)) * 100.0);
    return std::format("{:02}:{:02}.{:02}", m, s, ms);
}

// ── Wellenform-Daten ──────────────────────────────────────────────────────────

struct WaveformFrame {
    float peakL    = 0.f;   // Peak L-Kanal [0..1]
    float peakR    = 0.f;   // Peak R-Kanal [0..1]
    float treble   = 0.f;   // 0=Bass(orange), 1=Treble(cyan)
};

struct WaveformData {
    std::vector<WaveformFrame> frames;
    float                      durSec        = 0.f;
    std::atomic<bool>          ready         {false};
    std::atomic<bool>          cancel        {false};
    std::atomic<float>         detectedBPM   {0.f};
    std::atomic<float>         firstSoundSec {0.f};  // erste nicht-stille Position [s]
    std::atomic<int>           scanPct       {0};
};

// Hintergrund-Scanner — decoded die komplette Datei, befüllt WaveformData,
// berechnet BPM per Energie-Autokorrelation (kein Window-Freeze).
static void RunWaveformScan(std::wstring path, std::shared_ptr<WaveformData> data) {
    try {
        auto dec = CreateDecoderForFile(path.c_str());
        if (!dec || !dec->OpenFile(path.c_str())) { data->ready.store(true); return; }

        const int sr = dec->SampleRate();
        const int ch = std::clamp(dec->Channels(), 1, 2);
        if (sr <= 0) { data->ready.store(true); return; }

        // Wellenform: 20 ms pro Frame (50 fps) — feiner als vorher (war 50 ms)
        const int wfBlock  = std::max(1, sr / 50);
        // BPM-Energie: 200 ms Blöcke (grob genug für Autokorrelation)
        const int bpmBlock = std::max(1, sr / 5);

        std::vector<float> buf(static_cast<size_t>(wfBlock) * ch);

        // Für die Gesamtlänge (Fortschrittsanzeige) — Rough-Schätzung via GetLength
        const float totalSec = static_cast<float>(dec->GetLength().ms) / 1000.f;

        // BPM-Akkumulator
        float bpmSumSq = 0.f;
        int   bpmAccum = 0;
        std::vector<float> bpmEnergy;

        bool  firstSoundFound = false;
        int   frameIdx        = 0;  // für Throttle-Zähler

        while (!data->cancel.load(std::memory_order_relaxed)) {
            uint32_t got = dec->DecodeFloat(buf.data(), static_cast<uint32_t>(wfBlock));
            if (got == 0) break;

            // Throttle: alle 200 Frames kurz yielden damit Playback-Decoder nicht verhungert
            if ((++frameIdx & 0xFF) == 0)
                std::this_thread::yield();

            WaveformFrame f;
            float sumSq = 0.f, hfSumSq = 0.f;
            float lpState = 0.f;
            // One-pole LP: Grenzfreq ≈ (1-α)·sr/(2π) ≈ 1050 Hz @44 kHz
            // Trennt Bass (LP) von Treble (HP = signal - LP)
            const float lpAlpha = 0.85f;

            for (uint32_t i = 0; i < got; ++i) {
                float l = buf[static_cast<size_t>(i) * ch];
                float r = (ch > 1) ? buf[static_cast<size_t>(i) * ch + 1] : l;
                f.peakL = std::max(f.peakL, std::abs(l));
                f.peakR = std::max(f.peakR, std::abs(r));
                float s = (l + r) * 0.5f;
                lpState  = lpAlpha * lpState + (1.f - lpAlpha) * s;
                float hp = s - lpState;  // one-pole Hochpass
                sumSq   += s * s;
                hfSumSq += hp * hp;
                bpmSumSq += s * s;
            }
            bpmAccum += static_cast<int>(got);

            // Treble-Ratio: HP-Energie / Gesamt-Energie → 0=Bass, 1=Treble
            f.treble = (sumSq > 1e-9f)
                ? std::clamp(std::sqrt(hfSumSq / sumSq) * 3.5f, 0.f, 1.f)
                : 0.f;

            data->frames.push_back(f);
            data->durSec = static_cast<float>(data->frames.size()) *
                           static_cast<float>(wfBlock) / static_cast<float>(sr);

            // Erste nicht-stille Position merken (Peak > 1% FS)
            if (!firstSoundFound && (f.peakL > 0.01f || f.peakR > 0.01f)) {
                firstSoundFound = true;
                const float fss = data->durSec - static_cast<float>(wfBlock) / static_cast<float>(sr);
                data->firstSoundSec.store(std::max(0.f, fss), std::memory_order_release);
            }

            // Fortschritt aktualisieren
            if (totalSec > 0.f)
                data->scanPct.store(static_cast<int>(
                    std::clamp(data->durSec / totalSec * 100.f, 0.f, 99.f)));

            // BPM-Block voll?
            if (bpmAccum >= bpmBlock) {
                bpmEnergy.push_back(
                    (bpmAccum > 0) ? std::sqrt(bpmSumSq / static_cast<float>(bpmAccum)) : 0.f);
                bpmSumSq = 0.f;
                bpmAccum = 0;
            }
        }

        // ── BPM-Autokorrelation auf Energie-Hüllkurve ─────────────────────────
        // Mindestens 8 Blöcke (=1.6 s) für sinnvolle Korrelation
        if (!data->cancel.load() && bpmEnergy.size() >= 8) {
            const float blockSec = static_cast<float>(bpmBlock) / static_cast<float>(sr);

            // Einsatzstärke: positive Energiesteigung (Onset-Detektion)
            const int   M = static_cast<int>(bpmEnergy.size());
            std::vector<float> onset(static_cast<size_t>(M), 0.f);
            for (int i = 1; i < M; ++i)
                onset[static_cast<size_t>(i)] = std::max(0.f,
                    bpmEnergy[static_cast<size_t>(i)] - bpmEnergy[static_cast<size_t>(i - 1)]);

            // Lag-Bereich für 60–180 BPM umrechnen
            const int lagMin = std::max(1, static_cast<int>(60.f / (180.f * blockSec)));
            const int lagMax = std::min(M / 2,  static_cast<int>(60.f / ( 60.f * blockSec)));

            float bestCorr = 0.f;
            int   bestLag  = 0;
            for (int lag = lagMin; lag <= lagMax; ++lag) {
                float c = 0.f;
                for (int i = lag; i < M; ++i)
                    c += onset[static_cast<size_t>(i)] * onset[static_cast<size_t>(i - lag)];
                if (c > bestCorr) { bestCorr = c; bestLag = lag; }
            }
            if (bestLag > 0) {
                float bpm = 60.f / (static_cast<float>(bestLag) * blockSec);
                // In 60–180 BPM normalisieren
                while (bpm <  60.f) bpm *= 2.f;
                while (bpm > 180.f) bpm *= 0.5f;
                data->detectedBPM.store(bpm, std::memory_order_release);
            }
        }
    } catch (...) {}
    data->scanPct.store(100);
    data->ready.store(true);
}

// ── App-Zustand ───────────────────────────────────────────────────────────────

struct DeckState {
    std::wstring trackPath;
    char         pathBuf[512] = {};
    int          volL = 100, volR = 100;
    float        pitch = 0.f, tempo = 0.f, bpm = 0.f;
    bool         eqEnabled = false, echoEnabled = false;
    float        eq[10] = {};
    VUData       vu = {};

    // Wellenform
    std::shared_ptr<WaveformData> wf;
    std::thread                   wfThread;
    float                         wfViewDur  = 10.f;   // Sichtfenster-Breite [s]
    float                         smoothPos  = 0.f;    // geglättete Displayposition [s]
    bool                          bpmAutoSet = false;  // BPM bereits vom Scanner gesetzt
    bool                          silSkipped = false;  // Stille am Anfang bereits übersprungen

    DeckState()                            = default;
    DeckState(const DeckState&)            = delete;
    DeckState& operator=(const DeckState&) = delete;
    ~DeckState() {
        if (wf) wf->cancel.store(true);
        if (wfThread.joinable()) wfThread.join();
    }
};

struct ClipUI {
    std::wstring path;
    double start   = 0.0;    // Timeline-Position [s]
    double dur     = 30.0;   // Clip-Dauer [s]
    float  fadeIn  = 0.f;    // Fade-In-Dauer [s]
    float  fadeOut = 0.f;    // Fade-Out-Dauer [s]
};
struct TrackUI {
    char  nameBuf[64] = {};
    float gain  = 1.f;
    bool  muted = false;
    std::vector<ClipUI> clips;
};

struct AppState {
    DeckManager mgr;
    int         deckId[2]  = {-1, -1};
    DeckState   deck[2];
    float       crossPos   = 0.5f;
    float       crossDurS  = 4.0f;

    Timeline                        tl;
    std::unique_ptr<TimelinePlayer> tlPlayer;
    std::vector<TrackUI>            tracks;

    std::string status = "Bereit — Track in Deck A oder B laden.";
};

// ── Wellenform zeichnen — Traktor-Stil ───────────────────────────────────────
// Farb-Schema: Bass #FF5000 (orange-rot) → Mitte #FFFFFF (weiß) → Treble #0088FF (cyan)
// Symmetrische Balken oben/unten von Mittellinie, Gradient: Spitze hell, Mitte dunkel
// Bei hohem Pegel (>80%) weißer Blitz-Highlight an den Spitzen
static void DrawWaveform(ImDrawList* dl, ImVec2 p0, ImVec2 p1,
                          const WaveformData* wd, float playSec,
                          float viewStart, float viewDur,
                          ImU32 tintColor) {
    const float W    = p1.x - p0.x;
    const float H    = p1.y - p0.y;
    const float midY = p0.y + H * 0.5f;

    // ── Hintergrund: sehr dunkel ──────────────────────────────────────────────
    dl->AddRectFilled(p0, p1, IM_COL32(6, 6, 10, 255));

    // ── Scanning / Leer-Zustand ───────────────────────────────────────────────
    if (!wd || wd->frames.empty()) {
        const float t   = static_cast<float>(ImGui::GetTime());
        int   pct = wd ? wd->scanPct.load() : 0;
        float pw  = W * static_cast<float>(pct) / 100.f;
        dl->AddRectFilled({p0.x, midY - 1.5f}, {p0.x + pw, midY + 1.5f},
                          IM_COL32(60, 80, 160, 200));
        float cx = p0.x + pw;
        dl->AddLine({cx, p0.y}, {cx, p1.y},
                    IM_COL32(80, 120, 220,
                             static_cast<int>(130 + 90 * std::sin(t * 8.f))), 1.5f);
        char txt[64];
        if (wd && !wd->ready.load())
            std::snprintf(txt, sizeof(txt), "Scanning %d%%", pct);
        else
            std::snprintf(txt, sizeof(txt), "Drop audio file here  —  or use Laden button");
        float tw = ImGui::CalcTextSize(txt).x;
        dl->AddText({p0.x + (W - tw) * 0.5f, midY - 7.f},
                    IM_COL32(90, 110, 200, 220), txt);
        dl->AddRect(p0, p1, tintColor);
        return;
    }

    const int   N   = static_cast<int>(wd->frames.size());
    const float fps = static_cast<float>(N) / std::max(0.001f, wd->durSec);

    // ── Zeitgitter ────────────────────────────────────────────────────────────
    {
        const float gs = (viewDur > 60.f) ? 30.f : (viewDur > 20.f) ? 10.f :
                         (viewDur >  8.f) ?  5.f : (viewDur >  3.f) ?  1.f : 0.5f;
        float t0 = std::ceil(viewStart / gs) * gs;
        for (float gt = t0; gt < viewStart + viewDur; gt += gs) {
            float gx = p0.x + W * (gt - viewStart) / viewDur;
            dl->AddLine({gx, p0.y}, {gx, p1.y}, IM_COL32(28, 28, 46, 160));
            char buf[16];
            int sec = static_cast<int>(gt);
            std::snprintf(buf, sizeof(buf), "%d:%02d", sec / 60, sec % 60);
            dl->AddText({gx + 2.f, p0.y + 2.f}, IM_COL32(50, 50, 80, 190), buf);
        }
    }

    // ── Wellenform — Traktor-Stil — pro Pixel (1 Draw-Call) ──────────────────
    // Solid-Color-Bars: Frequenzfarbe × Amplitudenhelligkeit.
    // Kein Gradient = halbe Draw-Calls vs. vorher → deutlich flüssiger.
    const float halfH = H * 0.47f;

    // Alle Bars in einem Batch: PrimReserve(6*W, 4*W) vorab allozieren
    dl->PrimReserve(6 * static_cast<int>(W), 4 * static_cast<int>(W));
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;

    for (int px = 0; px < static_cast<int>(W); ++px) {
        const float ts = viewStart + viewDur * static_cast<float>(px)     / W;
        const float te = viewStart + viewDur * static_cast<float>(px + 1) / W;
        if (te < 0.f || ts > wd->durSec) {
            // Leeres Quad einfügen damit Index-Offset stimmt
            dl->PrimQuadUV({0,0},{0,0},{0,0},{0,0}, uv,uv,uv,uv, IM_COL32_BLACK_TRANS);
            continue;
        }

        const int fi0 = std::clamp(static_cast<int>(ts * fps), 0, N - 1);
        const int fi1 = std::clamp(static_cast<int>(te * fps), 0, N - 1);

        float peakL = 0.f, peakR = 0.f, treble = 0.f;
        for (int fi = fi0; fi <= fi1; ++fi) {
            const auto& f = wd->frames[static_cast<size_t>(fi)];
            peakL  = std::max(peakL, f.peakL);
            peakR  = std::max(peakR, f.peakR);
            treble += f.treble;
        }
        treble /= static_cast<float>(fi1 - fi0 + 1);

        const float peak = std::min((peakL + peakR) * 0.5f, 1.f);

        // Frequenz-Farbe: Bass #FF5000 → Weiß → Treble #0088FF
        uint8_t cr, cg, cb;
        if (treble < 0.5f) {
            const float u = treble * 2.f;
            cr = 255;
            cg = static_cast<uint8_t>(80  + 175 * u);
            cb = static_cast<uint8_t>(0   + 255 * u);
        } else {
            const float u = (treble - 0.5f) * 2.f;
            cr = static_cast<uint8_t>(255 - 239 * u);
            cg = static_cast<uint8_t>(255 - 119 * u);
            cb = 255;
        }

        // Helligkeit proportional zur Amplitude (leise = transparent)
        const uint8_t alpha = static_cast<uint8_t>(40 + 195 * peak);
        const float   barH  = halfH * peak;
        const float   x     = p0.x + static_cast<float>(px) + 0.5f;
        const ImU32   col   = IM_COL32(cr, cg, cb, alpha);

        // Ein Quad von Spitze zu Spitze — symmetrisch um Mittellinie
        dl->PrimQuadUV(
            {x - 0.5f, midY - barH}, {x + 0.5f, midY - barH},
            {x + 0.5f, midY + barH}, {x - 0.5f, midY + barH},
            uv, uv, uv, uv, col);
    }

    // ── Mittellinie ───────────────────────────────────────────────────────────
    dl->AddLine({p0.x, midY}, {p1.x, midY}, IM_COL32(38, 38, 58, 200));

    // ── Playhead — fest in der Mitte, Wellenform scrollt darunter ────────────
    {
        const float phx = p0.x + W * 0.5f;
        // Weicher Schatten
        dl->AddLine({phx + 2.f, p0.y}, {phx + 2.f, p1.y}, IM_COL32(0, 0, 0, 80), 5.f);
        // Hauptlinie
        dl->AddLine({phx, p0.y}, {phx, p1.y}, IM_COL32(255, 228, 0, 245), 2.f);
        // Dreieck-Marker oben
        dl->AddTriangleFilled(
            {phx - 6.f, p0.y}, {phx + 6.f, p0.y}, {phx, p0.y + 11.f},
            IM_COL32(255, 228, 0, 255));
        // Dreieck-Marker unten (zeigt nach oben)
        dl->AddTriangleFilled(
            {phx - 6.f, p1.y}, {phx + 6.f, p1.y}, {phx, p1.y - 11.f},
            IM_COL32(255, 228, 0, 255));
    }

    // ── Rahmen ────────────────────────────────────────────────────────────────
    dl->AddRect(p0, p1, tintColor);
}

// ── Deck-Konstanten (müssen vor LoadTrackIntoDeck stehen) ────────────────────
static const ImVec4 kDeckCol[] = {
    {0.25f, 0.65f, 1.00f, 1.f},   // A: Blau
    {1.00f, 0.50f, 0.15f, 1.f},   // B: Orange
};
static const char* kDeckName[] = { "Deck A", "Deck B" };

// ── Track-Lade-Helper ─────────────────────────────────────────────────────────
// Muss vor RenderDeck stehen, da dieser sie aufruft.

static void LoadTrackIntoDeck(AppState& app, int idx, const std::wstring& w) {
    int&       id = app.deckId[idx];
    DeckState& ds = app.deck[idx];
    try {
        ds.trackPath = w;
        auto u = WtoU8(w);
        std::strncpy(ds.pathBuf, u.c_str(), sizeof(ds.pathBuf) - 1);
        if (id < 0) id = app.mgr.CreateDeck(kDeckName[idx]);
        app.mgr.Stop(id);
        if (app.mgr.LoadTrack(id, w.c_str())) {
            if (ds.wf) ds.wf->cancel.store(true);
            if (ds.wfThread.joinable()) ds.wfThread.join();
            ds.wf           = std::make_shared<WaveformData>();
            auto wfPtr      = ds.wf;
            ds.wfThread     = std::thread([wfPtr, w] { RunWaveformScan(w, wfPtr); });
            ds.bpm       = 0.f;
            ds.smoothPos  = 0.f;
            ds.bpmAutoSet = false;
            ds.silSkipped = false;
            app.status = std::format("{}: '{}' geladen", kDeckName[idx], Basename(w));
        } else {
            app.status = std::format("{}: Fehler — {}", kDeckName[idx], app.mgr.GetError());
        }
    } catch (const std::exception& ex) {
        app.status = std::format("{}: Ausnahme — {}", kDeckName[idx], ex.what());
    } catch (...) {
        app.status = std::format("{}: Unbekannter Fehler", kDeckName[idx]);
    }
}

// ── VU-Meter ──────────────────────────────────────────────────────────────────

static void DrawVU(ImDrawList* dl, ImVec2 pos, ImVec2 sz, float v) {
    dl->AddRectFilled(pos, {pos.x + sz.x, pos.y + sz.y}, IM_COL32(20, 20, 20, 255));
    float fh = sz.y * std::clamp(v, 0.f, 1.f);
    float y0 = pos.y + sz.y - fh;
    ImU32 col = (v < 0.70f) ? IM_COL32(20, 200, 60, 255)
              : (v < 0.90f) ? IM_COL32(220, 200, 0, 255)
                            : IM_COL32(240, 40, 40, 255);
    if (fh > 0.f) dl->AddRectFilled({pos.x, y0}, {pos.x + sz.x, pos.y + sz.y}, col);
    dl->AddRect(pos, {pos.x + sz.x, pos.y + sz.y}, IM_COL32(50, 50, 50, 255));
}

// ── Deck-Controls ─────────────────────────────────────────────────────────────

static void RenderDeck(AppState& app, int idx) {
    int&       id = app.deckId[idx];
    DeckState& ds = app.deck[idx];
    ImGui::PushID(idx);

    const bool hasTrack = (id >= 0) && !ds.trackPath.empty();
    PlayerStatus st = hasTrack ? app.mgr.GetStatus(id) : PlayerStatus::Stopped;
    bool playing = (st == PlayerStatus::Playing);
    bool paused  = (st == PlayerStatus::Paused);
    bool stopped = !playing && !paused;

    // Status
    ImVec4 stCol = stopped ? ImVec4(.45f,.45f,.45f,1.f)
                 : playing ? ImVec4(.1f,.9f,.2f,1.f)
                           : ImVec4(.9f,.8f,.1f,1.f);
    ImGui::TextColored(stCol, stopped ? "[  ]" : playing ? "[ > ]" : "[ = ]");
    ImGui::SameLine();
    ImGui::TextColored(kDeckCol[idx], "%s", kDeckName[idx]);

    // Position (Wellenform-Scroll ist jetzt positionsgesteuert, kein Smooth-Scroll)
    float pos = 0.f, dur = 1.f;
    if (hasTrack) {
        pos = app.mgr.GetPosition(id);
        dur = std::max(0.001f, app.mgr.GetDuration(id));
    }

    // Auto-BPM: wenn der Scan fertig ist und BPM noch nicht gesetzt wurde
    if (!ds.bpmAutoSet && ds.wf && ds.wf->ready.load()) {
        const float detBPM = ds.wf->detectedBPM.load(std::memory_order_acquire);
        if (detBPM > 0.f) {
            ds.bpm = detBPM;
            if (id >= 0) app.mgr.SetBPM(id, detBPM);
            app.status = std::format("{}: BPM auto ≈ {:.1f}", kDeckName[idx], detBPM);
        }
        ds.bpmAutoSet = true;
    }

    // Auto-Stille-Überspringen: Deck steht noch → einmalig zum ersten Ton springen
    // Nur wenn wir gestoppt sind (nicht während des Spielens!)
    if (!ds.silSkipped && ds.wf && ds.wf->ready.load()) {
        const float fss = ds.wf->firstSoundSec.load(std::memory_order_acquire);
        if (fss > 0.5f && id >= 0 && stopped) {
            app.mgr.Seek(id, static_cast<double>(std::max(0.f, fss - 0.05f)));
            ds.smoothPos = fss - 0.05f;
        }
        ds.silSkipped = true;  // nur einmal, auch wenn kein Seek nötig war
    }

    // Positions-Bar (klickbar = Seek)
    float frac = pos / dur;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kDeckCol[idx]);
    ImGui::ProgressBar(frac, {-1.f, 5.f}, "");
    ImGui::PopStyleColor();
    if (ImGui::IsItemClicked()) {
        float mx = (ImGui::GetIO().MousePos.x - ImGui::GetItemRectMin().x)
                 / ImGui::GetItemRectSize().x;
        if (id >= 0) app.mgr.Seek(id, mx * dur);
    }
    ImGui::TextDisabled("  %s / %s", FormatTime(pos).c_str(), FormatTime(dur).c_str());

    // Datei laden
    ImGui::SetNextItemWidth(-80.f);
    ImGui::InputText("##p", ds.pathBuf, sizeof(ds.pathBuf));
    ImGui::SameLine();
    if (ImGui::Button("Laden")) {
        auto w = FileOpenDialog();
        if (!w.empty())
            LoadTrackIntoDeck(app, idx, w);
    }

    // VU abholen
    if (hasTrack) app.mgr.GetVU(id, ds.vu);

    // Transport
    ImGui::Spacing();
    ImGui::BeginDisabled(!hasTrack);
    if (ImGui::Button(" Play "))  { if (!playing) app.mgr.Play(id); }
    ImGui::SameLine();
    if (ImGui::Button("Pause"))   { app.mgr.Pause(id); }
    ImGui::SameLine();
    if (ImGui::Button("Stop"))    { app.mgr.Stop(id); }
    ImGui::SameLine();
    if (ImGui::Button("Loop"))    { app.mgr.SetLoop(id, 0xFFFFFFFFu); }
    ImGui::EndDisabled();

    ImGui::Separator();

    // Lautstärke + Pitch + Tempo
    ImGui::BeginDisabled(id < 0);
    bool vch = false;
    vch |= ImGui::SliderInt("Vol L", &ds.volL, 0, 100);
    vch |= ImGui::SliderInt("Vol R", &ds.volR, 0, 100);
    if (vch && id >= 0) app.mgr.SetVolume(id, ds.volL, ds.volR);

    // Pitch-Slider = Rate-Control (DJ-Stil: Geschwindigkeit + Ton zusammen wie Vinyl)
    // rate = 2^(semitones/12), Doppelklick = Reset
    {
        bool pitchChanged = ImGui::SliderFloat("Pitch", &ds.pitch, -12.f, 12.f, "%.2f st");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Geschwindigkeit (+ Tonhöhe) — wie Vinyl-Pitch-Fader\nDoppelklick = Reset");
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            ds.pitch = 0.f; pitchChanged = true;
        }
        if (pitchChanged && id >= 0) {
            const float rate = std::pow(2.f, ds.pitch / 12.f);
            app.mgr.SetRate(id, rate);
        }
    }
    // Tempo-Slider = Time-Stretch (Tonhöhe bleibt, nur Speed ändert sich)
    {
        bool tempoChanged = ImGui::SliderFloat("Tempo", &ds.tempo, -50.f, 50.f, "%.1f%%");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zeit-Dehnung — Tempo ±50%%\nDoppelklick = Reset");
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            ds.tempo = 0.f; tempoChanged = true;
        }
        if (tempoChanged && id >= 0)
            app.mgr.SetTempo(id, ds.tempo);
    }
    ImGui::EndDisabled();

    // BPM — detected + effektive (live berechnet aus Pitch und Tempo)
    ImGui::Spacing();
    {
        // Effektive BPM = detected_BPM × rate × tempo_factor
        //   rate         = 2^(pitch/12)      [Pitch-Slider ändert Geschwindigkeit]
        //   tempo_factor = 1 + tempo%/100    [Tempo-Slider]
        const float rate   = std::pow(2.f, ds.pitch / 12.f);
        const float tfac   = 1.f + ds.tempo / 100.f;
        const float effBPM = (ds.bpm > 0.f) ? ds.bpm * rate * tfac : 0.f;

        ImGui::TextDisabled("BPM:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.f);
        if (ImGui::InputFloat("##bpm", &ds.bpm, 0.f, 0.f, "%.1f") && id >= 0)
            app.mgr.SetBPM(id, ds.bpm);
        ImGui::SameLine();
        ImGui::BeginDisabled(id < 0);
        if (ImGui::Button("Detect") && id >= 0) {
            float b = 0.f;
            if (app.mgr.DetectBPM(id, b)) {
                ds.bpm = b; app.mgr.SetBPM(id, b);
                app.status = std::format("{}: BPM = {:.1f}", kDeckName[idx], b);
            }
        }
        ImGui::EndDisabled();
        // Effektiv-BPM daneben anzeigen wenn von Basis abweicht
        if (effBPM > 0.f) {
            const bool changed = std::fabs(effBPM - ds.bpm) > 0.05f;
            ImGui::SameLine();
            if (changed)
                ImGui::TextColored({1.f, 0.75f, 0.1f, 1.f}, "→ %.1f", effBPM);
            else
                ImGui::TextDisabled("→ %.1f", effBPM);
        }
    }

    // 10-Band EQ
    ImGui::Separator();
    if (ImGui::Checkbox("EQ", &ds.eqEnabled) && id >= 0) app.mgr.EnableEQ(id, ds.eqEnabled);
    if (ds.eqEnabled && id >= 0) {
        static const char* bands[] = {"32","63","125","250","500","1k","2k","4k","8k","16k"};
        ImGui::PushItemWidth(22.f);
        for (int b = 0; b < 10; ++b) {
            ImGui::PushID(b);
            if (ImGui::VSliderFloat("##eq", {20, 64}, &ds.eq[b], -12.f, 12.f, ""))
                app.mgr.SetEQBand(id, b, ds.eq[b]);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s Hz\n%.1f dB", bands[b], ds.eq[b]);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { ds.eq[b] = 0.f; app.mgr.SetEQBand(id, b, 0.f); }
            ImGui::PopID();
            if (b < 9) ImGui::SameLine();
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("0")) { app.mgr.ResetEQ(id); std::fill(std::begin(ds.eq), std::end(ds.eq), 0.f); }
    }
    if (ImGui::Checkbox("Echo", &ds.echoEnabled) && id >= 0) app.mgr.EnableEcho(id, ds.echoEnabled);

    // VU-Meter
    ImGui::Separator();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 cp = ImGui::GetCursorScreenPos();
    DrawVU(dl, {cp.x,       cp.y}, {14, 52}, ds.vu.left);
    DrawVU(dl, {cp.x + 18,  cp.y}, {14, 52}, ds.vu.right);
    ImGui::Dummy({36, 54});
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextDisabled("L %.0f%%", ds.vu.left * 100.f);
    ImGui::TextDisabled("R %.0f%%", ds.vu.right * 100.f);
    ImGui::EndGroup();

    ImGui::PopID();
}

// ── Crossfader ────────────────────────────────────────────────────────────────

static void RenderCrossfader(AppState& app) {
    ImGui::TextColored({0.9f, 0.9f, 0.3f, 1.f}, "Crossfader");
    ImGui::Spacing();

    float ga = std::cos(app.crossPos * 3.14159265f * 0.5f);
    float gb = std::sin(app.crossPos * 3.14159265f * 0.5f);
    ImGui::TextColored(kDeckCol[0], "A %.0f%%", ga * 100.f);
    ImGui::SameLine();
    ImGui::TextColored(kDeckCol[1], "B %.0f%%", gb * 100.f);

    // Vertikaler Slider für Crossfader
    ImGui::SliderFloat("##cf", &app.crossPos, 0.f, 1.f, "");
    // Preset-Buttons
    if (ImGui::Button("|< A", {-1.f, 0.f})) app.crossPos = 0.f;
    if (ImGui::Button("Mid",  {-1.f, 0.f})) app.crossPos = 0.5f;
    if (ImGui::Button("B >|", {-1.f, 0.f})) app.crossPos = 1.f;

    // Volume anwenden
    for (int i = 0; i < 2; ++i) {
        if (app.deckId[i] >= 0) {
            float g = (i == 0) ? ga : gb;
            int v = static_cast<int>(app.deck[i].volL * g);
            app.mgr.SetVolume(app.deckId[i], v, v);
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Auto-Fade");
    ImGui::SliderFloat("s", &app.crossDurS, 0.5f, 30.f, "%.1fs");

    // BPM-Sync
    ImGui::Separator();
    ImGui::TextDisabled("BPM-Sync");
    bool hasA = app.deckId[0] >= 0 && app.deck[0].bpm > 0.f;
    bool hasB = app.deckId[1] >= 0 && app.deck[1].bpm > 0.f;
    ImGui::BeginDisabled(!hasA || !hasB);
    if (ImGui::Button("A->B Tempo", {-1.f, 0.f}) && hasA && hasB) {
        float ratio = app.deck[0].bpm / app.deck[1].bpm;
        app.mgr.SetTempo(app.deckId[1], (ratio - 1.f) * 100.f);
        app.deck[1].tempo = (ratio - 1.f) * 100.f;
    }
    if (ImGui::Button("B->A Tempo", {-1.f, 0.f}) && hasA && hasB) {
        float ratio = app.deck[1].bpm / app.deck[0].bpm;
        app.mgr.SetTempo(app.deckId[0], (ratio - 1.f) * 100.f);
        app.deck[0].tempo = (ratio - 1.f) * 100.f;
    }
    ImGui::EndDisabled();
    if (!hasA || !hasB)
        ImGui::TextDisabled("BPM beider Decks erkennen");
}

// ── Timeline ──────────────────────────────────────────────────────────────────

static void RenderTimeline(AppState& app) {
    // Header
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.5f, 1.f));
    ImGui::Text("Timeline — DAW-Arrangement");
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 20);

    float tlPos = 0.f;
    float tlDur = 1.f;
    bool tlPlaying = false;
    if (app.tlPlayer) {
        tlPos     = static_cast<float>(app.tlPlayer->GetPosition());
        tlDur     = static_cast<float>(app.tl.GetDuration());
        tlPlaying = (app.tlPlayer->GetStatus() == TimelineStatus::Playing);
    }

    // Transport
    if (ImGui::Button("Play")) {
        if (!app.tlPlayer) {
            app.tlPlayer = std::make_unique<TimelinePlayer>(app.tl);
            app.tlPlayer->SetMasterGain(1.f);
        }
        if (!tlPlaying) app.tlPlayer->Play();
    }
    ImGui::SameLine();
    if (ImGui::Button("Pause")  && app.tlPlayer) app.tlPlayer->Pause();
    ImGui::SameLine();
    if (ImGui::Button("Stop")   && app.tlPlayer) { app.tlPlayer->Stop(); app.tlPlayer.reset(); }
    ImGui::SameLine();
    if (ImGui::Button("|< Start") && app.tlPlayer) app.tlPlayer->Seek(0.0);

    ImGui::SameLine(0, 20);
    // Fortschritt
    float frac = (tlDur > 0.f) ? std::clamp(tlPos / tlDur, 0.f, 1.f) : 0.f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(.55f, .45f, .85f, 1.f));
    ImGui::ProgressBar(frac, {200.f, 6.f}, "");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("%s / %s", FormatTime(tlPos).c_str(), FormatTime(tlDur).c_str());

    // Track-Verwaltung
    ImGui::Separator();
    if (ImGui::Button("+ Track")) {
        TrackUI t;
        std::snprintf(t.nameBuf, sizeof(t.nameBuf), "Track %d",
                      static_cast<int>(app.tracks.size()) + 1);
        app.tracks.push_back(std::move(t));
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(app.tl.Tracks().empty());
    if (ImGui::Button("Aufbauen & Play")) {
        app.tlPlayer.reset();
        app.tlPlayer = std::make_unique<TimelinePlayer>(app.tl);
        app.tlPlayer->SetMasterGain(1.f);
        app.tlPlayer->Play();
        app.status = std::format("Timeline gestartet ({} Tracks)", app.tl.Tracks().size());
    }
    ImGui::SameLine();
    if (ImGui::Button("Export WAV")) {
        auto savePath = FileSaveDialog();
        if (!savePath.empty()) {
            TimelinePlayer exp(app.tl);
            if (exp.ExportWav(savePath.c_str()))
                app.status = "WAV exportiert: " + Basename(savePath);
            else
                app.status = "Export fehlgeschlagen.";
        }
    }
    ImGui::EndDisabled();

    // ── Clip-Canvas ────────────────────────────────────────────────────────────
    ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = { ImGui::GetContentRegionAvail().x,
                          ImGui::GetContentRegionAvail().y - 4.f };
    if (canvasSize.x < 50.f || canvasSize.y < 20.f) return;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Zeitfenster: 0 … max(tlDur+10, 120)
    float viewDur  = std::max(120.f, tlDur + 10.f);
    float viewStart = 0.f;
    float tlW  = canvasSize.x;
    float tlX  = canvasPos.x;
    float tlY  = canvasPos.y;
    float tlH  = canvasSize.y;

    auto secToX = [&](double sec) -> float {
        return tlX + static_cast<float>((sec - viewStart) / viewDur) * tlW;
    };
    auto xToSec = [&](float x) -> double {
        return viewStart + static_cast<double>((x - tlX) / tlW) * viewDur;
    };

    // Hintergrund
    dl->AddRectFilled(canvasPos, {canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y},
                      IM_COL32(16, 16, 24, 255));

    // Zeit-Raster (alle 10 s)
    for (float t = 0.f; t < viewDur; t += 10.f) {
        float x = secToX(t);
        dl->AddLine({x, tlY}, {x, tlY + tlH}, IM_COL32(40, 40, 55, 255));
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.0fs", t);
        dl->AddText({x + 2, tlY + 2}, IM_COL32(70, 70, 90, 255), buf);
    }

    // Track-Höhe
    const float labelW = 80.f;
    const float trackH = std::clamp(tlH / std::max(1.f, static_cast<float>(app.tracks.size())), 24.f, 60.f);

    // Drag-State (statisch)
    static int  s_dragTrk  = -1;
    static int  s_dragClip = -1;
    enum class DragTarget { None, Body, FadeIn, FadeOut };
    static DragTarget s_dragType = DragTarget::None;

    // Track-Zeilen rendern
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("##canvas", canvasSize);   // Eingabe-Capture
    bool canvasHovered = ImGui::IsItemHovered();
    ImVec2 mouse = ImGui::GetIO().MousePos;
    float  mouseDX = ImGui::GetIO().MouseDelta.x;

    for (int ti = 0; ti < static_cast<int>(app.tracks.size()); ++ti) {
        TrackUI& track = app.tracks[ti];
        float y0 = tlY + ti * trackH;
        float y1 = y0 + trackH;

        // Track-Label
        dl->AddRectFilled({tlX, y0}, {tlX + labelW, y1}, IM_COL32(30, 30, 45, 255));
        dl->AddRect      ({tlX, y0}, {tlX + labelW, y1}, IM_COL32(55, 55, 75, 255));
        dl->AddText({tlX + 4, y0 + 4}, IM_COL32(200, 200, 220, 255), track.nameBuf);

        // Mute-Button (kleines M)
        ImGui::PushID(ti * 100);
        ImGui::SetCursorScreenPos({tlX + labelW - 18, y0 + 4});
        ImGui::PushStyleColor(ImGuiCol_Button, track.muted
            ? ImVec4(.8f,.2f,.2f,.9f) : ImVec4(.3f,.3f,.45f,.9f));
        if (ImGui::Button("M", {14.f, 14.f})) track.muted = !track.muted;
        ImGui::PopStyleColor();
        ImGui::PopID();

        // "+ Clip" Button
        ImGui::PushID(ti * 100 + 1);
        ImGui::SetCursorScreenPos({tlX + 2, y0 + trackH - 18});
        if (ImGui::Button("+C", {labelW - 4, 16.f})) {
            auto w = FileOpenDialog();
            if (!w.empty()) {
                ClipUI c;
                c.path  = w;
                c.start = track.clips.empty() ? 0.0 : track.clips.back().start + track.clips.back().dur + 1.0;
                c.dur   = 30.0;
                track.clips.push_back(c);

                // Timeline-Objekt synchronisieren
                app.tl = Timeline{};
                for (auto& tr2 : app.tracks) {
                    TimelineTrack& ref = app.tl.AddTrack(std::string(tr2.nameBuf));
                    ref.gain  = tr2.gain;
                    ref.muted = tr2.muted;
                    for (auto& cl : tr2.clips) {
                        Clip& cr = ref.AddClip(cl.path, cl.start, 0.0, cl.dur);
                        cr.SetFadeIn(cl.fadeIn);
                        cr.SetFadeOut(cl.fadeOut);
                    }
                }
                app.status = "Clip hinzugefügt: " + Basename(w);
            }
        }
        ImGui::PopID();

        // Clips rendern
        for (int ci = 0; ci < static_cast<int>(track.clips.size()); ++ci) {
            ClipUI& clip = track.clips[static_cast<size_t>(ci)];

            float cx0 = secToX(clip.start);
            float cx1 = secToX(clip.start + clip.dur);
            float clipW = cx1 - cx0;
            if (clipW < 2.f || cx1 < tlX + labelW || cx0 > tlX + tlW) continue;
            cx0 = std::max(cx0, tlX + labelW);

            float cy0 = y0 + 2.f, cy1 = y1 - 2.f;
            float ch  = cy1 - cy0;

            // Clip-Grundfläche
            ImU32 clipBg = track.muted
                ? IM_COL32(50, 50, 60, 180)
                : (ti % 2 == 0 ? IM_COL32(55, 90, 175, 200) : IM_COL32(80, 140, 90, 200));
            dl->AddRectFilled({cx0, cy0}, {cx1, cy1}, clipBg, 3.f);

            // Fade-In Gradient (dunkler Bereich links)
            float fiW = std::min(clipW * 0.5f, (clip.fadeIn / std::max(0.001f, (float)clip.dur)) * clipW);
            if (fiW > 1.f) {
                dl->AddRectFilledMultiColor({cx0, cy0}, {cx0 + fiW, cy1},
                    IM_COL32(0,0,0,220), IM_COL32(0,0,0,0),
                    IM_COL32(0,0,0,0),   IM_COL32(0,0,0,220));
            }
            // Fade-Out Gradient (dunkler Bereich rechts)
            float foW = std::min(clipW * 0.5f, (clip.fadeOut / std::max(0.001f, (float)clip.dur)) * clipW);
            if (foW > 1.f) {
                dl->AddRectFilledMultiColor({cx1 - foW, cy0}, {cx1, cy1},
                    IM_COL32(0,0,0,0),   IM_COL32(0,0,0,220),
                    IM_COL32(0,0,0,220), IM_COL32(0,0,0,0));
            }

            // Fade-In Handle (gelb)
            float fiX = cx0 + fiW;
            bool onFI = canvasHovered && mouse.y >= cy0 && mouse.y <= cy1
                        && std::abs(mouse.x - fiX) < 7.f;
            ImU32 fiCol = (onFI || (s_dragTrk == ti && s_dragClip == ci && s_dragType == DragTarget::FadeIn))
                ? IM_COL32(255,220,0,255) : IM_COL32(200,180,0,160);
            dl->AddLine({fiX, cy0}, {fiX, cy1}, fiCol, onFI ? 3.f : 1.5f);
            dl->AddCircleFilled({fiX, cy0 + ch * 0.5f}, onFI ? 5.f : 3.5f, fiCol);

            // Fade-Out Handle (orange)
            float foX = cx1 - foW;
            bool onFO = canvasHovered && mouse.y >= cy0 && mouse.y <= cy1
                        && std::abs(mouse.x - foX) < 7.f && !onFI;
            ImU32 foCol = (onFO || (s_dragTrk == ti && s_dragClip == ci && s_dragType == DragTarget::FadeOut))
                ? IM_COL32(255,100,0,255) : IM_COL32(200,80,0,160);
            dl->AddLine({foX, cy0}, {foX, cy1}, foCol, onFO ? 3.f : 1.5f);
            dl->AddCircleFilled({foX, cy0 + ch * 0.5f}, onFO ? 5.f : 3.5f, foCol);

            // Clip-Name
            if (clipW > 50.f) {
                auto name = Basename(clip.path);
                ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
                float tx = std::clamp(cx0 + 4.f, cx0 + 2.f, cx1 - textSize.x - 2.f);
                dl->AddText({tx, cy0 + 3.f}, IM_COL32(255,255,255,200), name.c_str());
            }

            // Clip-Rahmen
            dl->AddRect({cx0, cy0}, {cx1, cy1}, IM_COL32(150,150,200,120), 3.f);

            // Drag-Start: Klick auf Handle oder Body
            bool onBody = canvasHovered && mouse.y >= cy0 && mouse.y <= cy1
                          && mouse.x >= cx0 && mouse.x <= cx1 && !onFI && !onFO;

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if      (onFI)   { s_dragTrk = ti; s_dragClip = ci; s_dragType = DragTarget::FadeIn; }
                else if (onFO)   { s_dragTrk = ti; s_dragClip = ci; s_dragType = DragTarget::FadeOut; }
                else if (onBody) { s_dragTrk = ti; s_dragClip = ci; s_dragType = DragTarget::Body; }
            }
        }

        // Trennlinie
        dl->AddLine({tlX, y1}, {tlX + tlW, y1}, IM_COL32(40, 40, 60, 255));
    }

    // Drag anwenden
    if (s_dragTrk >= 0 && s_dragClip >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        auto& cl = app.tracks[static_cast<size_t>(s_dragTrk)].clips[static_cast<size_t>(s_dragClip)];
        double dSec = static_cast<double>(mouseDX / tlW) * viewDur;
        if      (s_dragType == DragTarget::FadeIn) {
            cl.fadeIn = std::clamp(cl.fadeIn + static_cast<float>(dSec), 0.f, static_cast<float>(cl.dur * 0.5));
        } else if (s_dragType == DragTarget::FadeOut) {
            cl.fadeOut = std::clamp(cl.fadeOut - static_cast<float>(dSec), 0.f, static_cast<float>(cl.dur * 0.5));
        } else if (s_dragType == DragTarget::Body) {
            cl.start = std::max(0.0, cl.start + dSec);
        }
    } else {
        s_dragTrk = -1; s_dragClip = -1; s_dragType = DragTarget::None;
    }

    // Playhead
    if (tlPos > 0.f || tlPlaying) {
        float phX = secToX(tlPos);
        if (phX >= tlX && phX <= tlX + tlW) {
            dl->AddLine({phX, tlY}, {phX, tlY + tlH}, IM_COL32(200, 80, 220, 200), 2.f);
            dl->AddTriangleFilled({phX - 5, tlY}, {phX + 5, tlY}, {phX, tlY + 8},
                                  IM_COL32(200, 80, 220, 255));
        }
    }
}

// ── Haupt-Layout ──────────────────────────────────────────────────────────────

static void RenderUI(AppState& app) {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x;
    float H = io.DisplaySize.y;

    // ── Pending Drag-&-Drop ───────────────────────────────────────────────────
    // Selbe Nachrichtenschleife → kein Mutex nötig
    for (auto& drop : g_pendingDrops) {
        // Linke Hälfte → Deck A, rechte Hälfte → Deck B
        int idx = (drop.pt.x < static_cast<LONG>(W * 0.5f)) ? 0 : 1;
        // Falls Ziel-Deck besetzt → anderen nehmen
        if (!app.deck[idx].trackPath.empty()) idx ^= 1;
        LoadTrackIntoDeck(app, idx, drop.path);
    }
    g_pendingDrops.clear();

    // Proportionen
    // Wellenform: Deck A oben (volle Breite) + Deck B darunter (volle Breite)
    // Pfeil fest in der Mitte jeder Wellenform — Track scrollt darunter
    const float perDeckH = 128.f;           // Höhe pro Deck-Wellenform
    const float waveH    = perDeckH * 2.f + 2.f;  // A + Trenner + B = 258 px
    const float statusH  = 22.f;
    const float deckH    = 340.f;
    const float tlH      = H - waveH - deckH - statusH;

    // ── Wellenform-Panels — übereinander, volle Breite ───────────────────────
    {
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({W, waveH});
        ImGui::Begin("##waves", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar  | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoBackground);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Latenz-Offset: WaveOut puffert ~90ms vor → Display entsprechend zurücksetzen
        // so dass Wellenform mit dem sichtbaren Ton synchron ist.
        static constexpr float kAudioLatency = 0.09f;
        const float dt = std::max(0.001f, ImGui::GetIO().DeltaTime);

        // ── Deck A — oben (volle Breite) ─────────────────────────────────────
        {
            DeckState& ds    = app.deck[0];
            const float rawA = (app.deckId[0] >= 0) ? app.mgr.GetPosition(app.deckId[0]) : 0.f;
            // Leichte Glättung: eliminiert Thread-Jitter, bleibt aber quasi instant
            ds.smoothPos += (rawA - ds.smoothPos) * std::min(1.f, dt * 40.f);
            const float dispA = std::max(0.f, ds.smoothPos - kAudioLatency);
            const float vsA   = dispA - ds.wfViewDur * 0.5f;
            DrawWaveform(dl, {0.f, 0.f}, {W, perDeckH},
                         ds.wf.get(), dispA, vsA, ds.wfViewDur,
                         IM_COL32(60, 130, 230, 200));
            dl->AddText({8.f, 4.f}, IM_COL32(100, 165, 255, 230), "DECK A");
        }

        // ── Trenner ───────────────────────────────────────────────────────────
        dl->AddLine({0.f, perDeckH + 1.f}, {W, perDeckH + 1.f},
                    IM_COL32(50, 50, 70, 255), 2.f);

        // ── Deck B — darunter (volle Breite) ─────────────────────────────────
        {
            DeckState& ds    = app.deck[1];
            const float rawB = (app.deckId[1] >= 0) ? app.mgr.GetPosition(app.deckId[1]) : 0.f;
            ds.smoothPos += (rawB - ds.smoothPos) * std::min(1.f, dt * 40.f);
            const float dispB = std::max(0.f, ds.smoothPos - kAudioLatency);
            const float vsB   = dispB - ds.wfViewDur * 0.5f;
            const float y0    = perDeckH + 2.f;
            DrawWaveform(dl, {0.f, y0}, {W, y0 + perDeckH},
                         ds.wf.get(), dispB, vsB, ds.wfViewDur,
                         IM_COL32(230, 100, 30, 200));
            dl->AddText({8.f, y0 + 4.f}, IM_COL32(255, 165, 60, 230), "DECK B");
        }

        ImGui::End();
    }

    // ── Deck A (links) ───────────────────────────────────────────────────────
    float deckY    = waveH;
    float deckAW   = W * 0.38f;
    float crossW   = W * 0.24f;
    float deckBW   = W - deckAW - crossW;

    ImGui::SetNextWindowPos({0, deckY});
    ImGui::SetNextWindowSize({deckAW, deckH});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(.05f, .07f, .12f, 1.f));
    ImGui::Begin("##deckA", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar  | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0,0,0,0));
    RenderDeck(app, 0);
    ImGui::PopStyleColor();
    ImGui::End();
    ImGui::PopStyleColor();

    // ── Crossfader (Mitte) ───────────────────────────────────────────────────
    ImGui::SetNextWindowPos({deckAW, deckY});
    ImGui::SetNextWindowSize({crossW, deckH});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(.07f, .07f, .1f, 1.f));
    ImGui::Begin("##cross", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar  | ImGuiWindowFlags_NoBringToFrontOnFocus);
    RenderCrossfader(app);
    ImGui::End();
    ImGui::PopStyleColor();

    // ── Deck B (rechts) ──────────────────────────────────────────────────────
    ImGui::SetNextWindowPos({deckAW + crossW, deckY});
    ImGui::SetNextWindowSize({deckBW, deckH});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(.12f, .07f, .05f, 1.f));
    ImGui::Begin("##deckB", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar  | ImGuiWindowFlags_NoBringToFrontOnFocus);
    RenderDeck(app, 1);
    ImGui::End();
    ImGui::PopStyleColor();

    // ── Timeline (unten) ─────────────────────────────────────────────────────
    ImGui::SetNextWindowPos({0, deckY + deckH});
    ImGui::SetNextWindowSize({W, tlH});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(.04f, .04f, .07f, 1.f));
    ImGui::Begin("##tl", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar  | ImGuiWindowFlags_NoBringToFrontOnFocus);
    RenderTimeline(app);
    ImGui::End();
    ImGui::PopStyleColor();

    // ── Statusleiste ──────────────────────────────────────────────────────────
    ImGui::SetNextWindowPos({0, H - statusH});
    ImGui::SetNextWindowSize({W, statusH});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(.06f, .06f, .09f, 1.f));
    ImGui::Begin("##status", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar  | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::TextDisabled(" %s", app.status.c_str());
    ImGui::End();
    ImGui::PopStyleColor();
}

// ── WinMain ───────────────────────────────────────────────────────────────────

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"libZForkStudio";
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName,
        L"libZFork Studio — DJ + Timeline",
        WS_OVERLAPPEDWINDOW, 100, 50, 1360, 940,
        nullptr, nullptr, hInst, nullptr);

    if (!CreateDeviceD3D(g_hwnd)) { CleanupDeviceD3D(); UnregisterClassW(wc.lpszClassName, hInst); return 1; }
    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);
    DragAcceptFiles(g_hwnd, TRUE);   // Drag-&-Drop aktivieren

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    // Dark-Theme anpassen
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding   = 0.f;
    style.WindowBorderSize = 0.f;
    style.FrameRounding    = 3.f;
    style.ItemSpacing      = {4, 4};
    style.Colors[ImGuiCol_WindowBg]  = ImVec4(.06f, .06f, .09f, 1.f);
    style.Colors[ImGuiCol_FrameBg]   = ImVec4(.12f, .12f, .18f, 1.f);
    style.Colors[ImGuiCol_Button]    = ImVec4(.20f, .22f, .32f, 1.f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(.30f, .33f, .48f, 1.f);

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    AppState app;

    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }
        if (g_ResizeWidth && g_ResizeHeight) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderUI(app);

        ImGui::Render();
        const float cc[4] = { 0.04f, 0.04f, 0.07f, 1.f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    // Wellenform-Threads beenden
    for (auto& ds : app.deck) {
        if (ds.wf) ds.wf->cancel.store(true);
        if (ds.wfThread.joinable()) ds.wfThread.join();
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(g_hwnd);
    UnregisterClassW(wc.lpszClassName, hInst);
    return 0;
}
