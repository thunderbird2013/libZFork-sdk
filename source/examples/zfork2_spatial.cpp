/**
 * libZFork2 — Spatial Audio Demo
 *
 * Demonstriert 2-D Positional Audio der v2 Game Engine.
 *
 * Ein Sound-Emitter kreist auf einer Kreisbahn um den Listener.
 * Der Zuhörer schaut entlang +Y (Norden). Lautstärke und Pan
 * aendern sich kontinuierlich entsprechend Distanz und Winkel.
 *
 * Usage:
 *   zfork2_spatial <audiodatei> [radius=5] [rpm=10]
 *
 *   radius   Kreisradius in Welt-Einheiten (Standard 5.0)
 *   rpm      Umdrehungen pro Minute         (Standard 10)
 *
 * Tasten:
 *   +/-   Radius vergroessern / verkleinern
 *   Q/Esc Beenden
 */

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <libzfork/libzfork2.h>
#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <atomic>

static constexpr float kPi = 3.14159265358979323846f;

// ── globals ────────────────────────────────────────────────────────────────────

static std::atomic<bool> g_quit{false};
static ZF2_CONTEXT       g_ctx  = nullptr;
static ZF2_VOICE         g_voice = ZF2_INVALID_VOICE;
static float             g_radius = 5.f;

// ── event callback ─────────────────────────────────────────────────────────────

static void OnEvent(int type, ZF2_VOICE voice, uint32_t /*p1*/,
                    float /*p2*/, void* /*ud*/)
{
    if (type == ZF2_EVENT_VOICE_FINISHED) {
        // Loop infinitely: replay as soon as the voice finishes.
        // (In production you'd use zfork2_SetVoiceLoop instead.)
        printf("\n[event] VoiceFinished voice=%u — neustart\n", voice);
        g_quit.store(true);   // simple demo: just exit
    }
}

// ── draw compass ──────────────────────────────────────────────────────────────

static HANDLE g_con = nullptr;

static void GotoXY(int x, int y) {
    if (!g_con) return;
    COORD c{ static_cast<SHORT>(x), static_cast<SHORT>(y) };
    SetConsoleCursorPosition(g_con, c);
}

static void DrawCompass(float angleDeg, float pan, float gain)
{
    const int cx = 20, cy = 3, r = 7;
    float rad = angleDeg * kPi / 180.f;
    int ex = cx + static_cast<int>(std::round(r * std::sin(rad)));
    int ey = cy - static_cast<int>(std::round(r * 0.5f * std::cos(rad)));

    // Clear area
    for (int row = 0; row <= r; ++row) {
        GotoXY(0, row);
        printf("%*s", cx * 2 + 4, "");
    }

    // Listener at center
    GotoXY(cx, cy);
    printf("L");

    // Emitter
    GotoXY(ex, ey);
    printf("*");

    // Stats
    GotoXY(0, cy + 5);
    printf("Winkel: %6.1f deg   Pan: %+5.2f   Gain: %.2f   Radius: %.1f   \n",
           angleDeg, pan, gain, g_radius);
    printf("Tasten: +/- Radius   Q Beenden\n");
}

// ── entry point ────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: zfork2_spatial <audiodatei> [radius] [rpm]\n");
        return 1;
    }

    float rpm    = (argc >= 4) ? static_cast<float>(atof(argv[3])) : 10.f;
    g_radius     = (argc >= 3) ? static_cast<float>(atof(argv[2])) : 5.f;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, wpath.data(), wlen);

    g_con = GetStdHandle(STD_OUTPUT_HANDLE);
    // Hide cursor
    CONSOLE_CURSOR_INFO ci{ 1, FALSE };
    SetConsoleCursorInfo(g_con, &ci);

    // ── context ────────────────────────────────────────────────────────────────
    g_ctx = zfork2_CreateContext();
    if (!g_ctx) { fprintf(stderr, "CreateContext fehlgeschlagen\n"); return 1; }

    zfork2_SetCallback(g_ctx, OnEvent, nullptr);

    if (!zfork2_Start(g_ctx)) {
        fprintf(stderr, "Start fehlgeschlagen\n");
        zfork2_DestroyContext(g_ctx);
        return 1;
    }

    // Listener sitzt im Ursprung, schaut Richtung +Y.
    zfork2_SetListenerPosition2D(g_ctx, 0.f, 0.f);
    zfork2_SetListenerDirection(g_ctx, 0.f, 1.f, 0.f);
    zfork2_SetListenerRange(g_ctx, 1.f, g_radius * 2.f, 1.5f);

    // ── load & play looped ─────────────────────────────────────────────────────
    ZF2_SOUND snd = zfork2_LoadSound(g_ctx, wpath.c_str(), ZF2_SOUND_PREDECODED);
    if (!snd) {
        fprintf(stderr, "LoadSound fehlgeschlagen: %s\n", argv[1]);
        zfork2_Stop(g_ctx);
        zfork2_DestroyContext(g_ctx);
        return 1;
    }

    ZF2_GROUP sfx = zfork2_GetGroup(g_ctx, "sfx");
    g_voice = zfork2_PlaySound(g_ctx, snd, sfx);
    if (g_voice == ZF2_INVALID_VOICE) {
        fprintf(stderr, "PlaySound fehlgeschlagen\n");
        zfork2_UnloadSound(snd);
        zfork2_Stop(g_ctx);
        zfork2_DestroyContext(g_ctx);
        return 1;
    }
    zfork2_SetVoiceLoop(g_ctx, g_voice, -1);  // loop infinitely

    // ── animation loop ─────────────────────────────────────────────────────────
    float   angleDeg   = 0.f;
    float   degPerMs   = rpm * 360.f / 60000.f;
    DWORD   lastTick   = GetTickCount();
    system("cls");

    while (!g_quit.load()) {
        DWORD now = GetTickCount();
        float dt  = static_cast<float>(now - lastTick);
        lastTick  = now;
        angleDeg  = std::fmod(angleDeg + degPerMs * dt, 360.f);

        float rad = angleDeg * kPi / 180.f;
        float ex  = g_radius * std::sin(rad);
        float ey  = g_radius * std::cos(rad);

        zfork2_SetVoicePosition2D(g_ctx, g_voice, ex, ey);
        zfork2_Update(g_ctx);

        // Compute what the engine will render (mirrored for display).
        float dx   = ex, dy = ey;
        float dist = std::sqrt(dx*dx + dy*dy);
        float pan  = (dist > 0.001f) ? (dy * dx - dx * dy) / dist : 0.f;
        // right vector for listener facing +Y: (+1, 0)
        float panVal = (dist > 0.001f) ? dx / dist : 0.f;
        float t    = std::max(0.f, std::min(1.f,
                       (dist - 1.f) / (g_radius * 2.f - 1.f)));
        float gain = std::max(0.f, 1.f - std::pow(t, 1.5f));
        (void)pan;

        DrawCompass(angleDeg, panVal, gain);

        if (_kbhit()) {
            int ch = _getch();
            if (ch == '+' || ch == '=') {
                g_radius += 0.5f;
                zfork2_SetListenerRange(g_ctx, 1.f, g_radius * 2.f, 1.5f);
            } else if (ch == '-' && g_radius > 1.f) {
                g_radius -= 0.5f;
                zfork2_SetListenerRange(g_ctx, 1.f, g_radius * 2.f, 1.5f);
            } else if (ch == 'q' || ch == 'Q' || ch == 27) {
                break;
            }
        }

        Sleep(16);
    }

    // ── cleanup ────────────────────────────────────────────────────────────────
    GotoXY(0, 12);
    printf("\nFertig.\n");
    ci.bVisible = TRUE;
    SetConsoleCursorInfo(g_con, &ci);

    zfork2_UnloadSound(snd);
    zfork2_Stop(g_ctx);
    zfork2_DestroyContext(g_ctx);
    return 0;
}
