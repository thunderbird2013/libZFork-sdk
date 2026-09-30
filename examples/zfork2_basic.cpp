/**
 * libZFork2 — Basic Demo
 *
 * Minimales Beispiel fuer die v2 Game Audio Engine.
 *
 * Laedt eine Audiodatei, spielt sie einmal ab und wartet per Callback bis sie
 * fertig ist. Zeigt VoiceStart / VoiceFinished Events auf der Konsole.
 *
 * Usage:
 *   zfork2_basic <audiodatei>
 *
 * Tasten waehrend der Wiedergabe:
 *   Space   Pause / Weiter
 *   Q/Esc   Abbruch
 */

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <libzfork/libzfork2.h>
#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <atomic>

// ── globals ────────────────────────────────────────────────────────────────────

static std::atomic<bool> g_finished{false};
static ZF2_CONTEXT       g_ctx  = nullptr;
static ZF2_VOICE         g_voice = ZF2_INVALID_VOICE;
static bool              g_paused = false;

// ── event callback (called from Update on main thread) ─────────────────────────

static void OnEvent(int type, ZF2_VOICE voice, uint32_t param1,
                    float /*param2*/, void* /*ud*/)
{
    switch (type) {
    case ZF2_EVENT_VOICE_START:
        printf("[event] VoiceStart   voice=%u\n", voice);
        break;
    case ZF2_EVENT_VOICE_FINISHED:
        printf("[event] VoiceFinished voice=%u\n", voice);
        g_finished.store(true);
        break;
    case ZF2_EVENT_VOICE_LOOP:
        printf("[event] VoiceLoop     voice=%u  loops_left=%u\n", voice, param1);
        break;
    default:
        break;
    }
}

// ── entry point ────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: zfork2_basic <audiodatei>\n");
        return 1;
    }

    // Convert path to wide string.
    int wlen = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, wpath.data(), wlen);

    // ── context ────────────────────────────────────────────────────────────────
    g_ctx = zfork2_CreateContext();
    if (!g_ctx) { fprintf(stderr, "CreateContext fehlgeschlagen\n"); return 1; }

    zfork2_SetCallback(g_ctx, OnEvent, nullptr);

    if (!zfork2_Start(g_ctx)) {
        fprintf(stderr, "Start fehlgeschlagen\n");
        zfork2_DestroyContext(g_ctx);
        return 1;
    }

    // ── load & play ────────────────────────────────────────────────────────────
    ZF2_SOUND snd = zfork2_LoadSound(g_ctx, wpath.c_str(), ZF2_SOUND_PREDECODED);
    if (!snd) {
        fprintf(stderr, "LoadSound fehlgeschlagen: %s\n", argv[1]);
        zfork2_Stop(g_ctx);
        zfork2_DestroyContext(g_ctx);
        return 1;
    }

    ZF2_GROUP sfx   = zfork2_GetGroup(g_ctx, "sfx");
    g_voice = zfork2_PlaySound(g_ctx, snd, sfx);
    if (g_voice == ZF2_INVALID_VOICE) {
        fprintf(stderr, "PlaySound fehlgeschlagen\n");
        zfork2_UnloadSound(snd);
        zfork2_Stop(g_ctx);
        zfork2_DestroyContext(g_ctx);
        return 1;
    }

    printf("Spiele: %s  (voice=%u)\n", argv[1], g_voice);
    printf("Space = Pause/Weiter   Q/Esc = Beenden\n\n");

    // ── game loop ──────────────────────────────────────────────────────────────
    while (!g_finished.load()) {
        zfork2_Update(g_ctx);   // dispatches events on this thread

        if (_kbhit()) {
            int ch = _getch();
            if (ch == ' ') {
                if (g_paused) {
                    zfork2_ResumeVoice(g_ctx, g_voice);
                    g_paused = false;
                    printf("[pause] weiter\n");
                } else {
                    zfork2_PauseVoice(g_ctx, g_voice);
                    g_paused = true;
                    printf("[pause] pausiert\n");
                }
            } else if (ch == 'q' || ch == 'Q' || ch == 27 /*Esc*/) {
                printf("\nAbgebrochen.\n");
                break;
            }
        }

        Sleep(16);
    }

    // ── cleanup ────────────────────────────────────────────────────────────────
    zfork2_UnloadSound(snd);
    zfork2_Stop(g_ctx);
    zfork2_DestroyContext(g_ctx);
    printf("Fertig.\n");
    return 0;
}
