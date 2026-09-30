/**
 * libZFork2 — Multi-Voice / Group-Bus Demo
 *
 * Demonstriert simultane Voices mit Group-Lautstaerke-Steuerung.
 *
 * Startet mehrere Sounds gleichzeitig in verschiedenen Gruppen
 * (music / sfx / ui) und laesst den Benutzer die Gruppen-Lautstaerke
 * interaktiv veraendern. Zeigt ausserdem Priority-Stealing sobald alle
 * 64 Slots belegt sind.
 *
 * Usage:
 *   zfork2_multivoice <music> <sfx> [ui]
 *
 *   music  Musikdatei (geloopt, niedrige Prioritaet)
 *   sfx    Effektdatei (jede Sekunde neu abgespielt, hohe Prioritaet)
 *   ui     Optional: UI-Sound (beim Tastendruck abgespielt)
 *
 * Tasten:
 *   1/2   Music-Gruppe  lauter / leiser
 *   3/4   SFX-Gruppe    lauter / leiser
 *   5/6   UI-Gruppe     lauter / leiser
 *   M/S/U Gruppe mute-Toggel (Music / SFX / UI)
 *   Space SFX manuell ausloesen
 *   Q/Esc Beenden
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
#include <array>
#include <vector>

// ── globals ────────────────────────────────────────────────────────────────────

static ZF2_CONTEXT g_ctx = nullptr;
static ZF2_SOUND   g_sndMusic = nullptr;
static ZF2_SOUND   g_sndSfx   = nullptr;
static ZF2_SOUND   g_sndUi    = nullptr;

static ZF2_GROUP   g_grpMusic = ZF2_INVALID_GROUP;
static ZF2_GROUP   g_grpSfx   = ZF2_INVALID_GROUP;
static ZF2_GROUP   g_grpUi    = ZF2_INVALID_GROUP;

struct GroupState {
    float vol     = 1.0f;
    bool  muted   = false;
    const char* name = "";
};
static GroupState g_state[3];   // 0=music 1=sfx 2=ui

static std::atomic<int>  g_activeVoices{0};
static std::atomic<int>  g_finishedCount{0};

// ── console helpers ────────────────────────────────────────────────────────────

static HANDLE g_con = nullptr;

static void GotoXY(int x, int y) {
    if (!g_con) return;
    COORD c{ static_cast<SHORT>(x), static_cast<SHORT>(y) };
    SetConsoleCursorPosition(g_con, c);
}

static void DrawBar(int x, int y, const char* label, float vol, bool muted) {
    GotoXY(x, y);
    int bars = static_cast<int>(vol * 20.f + 0.5f);
    printf("%-6s [", label);
    for (int i = 0; i < 20; ++i) printf(i < bars ? "#" : "-");
    printf("] %3d%%  %s   ", static_cast<int>(vol * 100.f), muted ? "[MUTE]" : "      ");
}

static void DrawUI() {
    GotoXY(0, 0);
    printf("=== libZFork2 Multi-Voice Demo ===\n\n");
    DrawBar(0, 2, "Music", g_state[0].vol, g_state[0].muted);
    DrawBar(0, 3, "SFX",   g_state[1].vol, g_state[1].muted);
    DrawBar(0, 4, "UI",    g_state[2].vol, g_state[2].muted);
    GotoXY(0, 6);
    printf("Aktive Voices: %d   Fertig: %d    \n",
           g_activeVoices.load(), g_finishedCount.load());
    GotoXY(0, 8);
    printf("1/2 Music laut/leise   3/4 SFX   5/6 UI\n");
    printf("M/S/U Mute-Toggle      Space SFX jetzt    Q Beenden\n");
}

// ── event callback ─────────────────────────────────────────────────────────────

static void OnEvent(int type, ZF2_VOICE /*voice*/, uint32_t /*p1*/,
                    float /*p2*/, void* /*ud*/)
{
    if (type == ZF2_EVENT_VOICE_START)    g_activeVoices.fetch_add(1);
    if (type == ZF2_EVENT_VOICE_FINISHED) {
        g_activeVoices.fetch_sub(1);
        g_finishedCount.fetch_add(1);
    }
}

// ── helpers ────────────────────────────────────────────────────────────────────

static void SetGroupVol(int idx, ZF2_GROUP grp, float delta) {
    g_state[idx].vol = std::max(0.f, std::min(1.f, g_state[idx].vol + delta));
    zfork2_SetGroupVolume(g_ctx, grp, g_state[idx].vol);
}

static void ToggleMute(int idx, ZF2_GROUP grp) {
    g_state[idx].muted = !g_state[idx].muted;
    zfork2_SetGroupMute(g_ctx, grp, g_state[idx].muted ? 1 : 0);
}

static std::wstring ToWide(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}

// ── entry point ────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: zfork2_multivoice <music> <sfx> [ui]\n");
        return 1;
    }

    g_con = GetStdHandle(STD_OUTPUT_HANDLE);
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

    g_grpMusic = zfork2_GetGroup(g_ctx, "music");
    g_grpSfx   = zfork2_GetGroup(g_ctx, "sfx");
    g_grpUi    = zfork2_GetGroup(g_ctx, "ui");

    g_state[0] = { 0.8f, false, "Music" };
    g_state[1] = { 1.0f, false, "SFX"   };
    g_state[2] = { 1.0f, false, "UI"    };

    for (int i = 0; i < 3; ++i) {
        ZF2_GROUP grp = (i == 0) ? g_grpMusic : (i == 1) ? g_grpSfx : g_grpUi;
        zfork2_SetGroupVolume(g_ctx, grp, g_state[i].vol);
    }

    // ── load sounds ────────────────────────────────────────────────────────────
    g_sndMusic = zfork2_LoadSound(g_ctx, ToWide(argv[1]).c_str(), ZF2_SOUND_STREAMING);
    g_sndSfx   = zfork2_LoadSound(g_ctx, ToWide(argv[2]).c_str(), ZF2_SOUND_PREDECODED);
    if (argc >= 4)
        g_sndUi = zfork2_LoadSound(g_ctx, ToWide(argv[3]).c_str(), ZF2_SOUND_PREDECODED);

    if (!g_sndMusic || !g_sndSfx) {
        fprintf(stderr, "Laden fehlgeschlagen\n");
        goto cleanup;
    }

    // ── start music (looped, low priority) ────────────────────────────────────
    {
        ZF2_VOICE mv = zfork2_PlaySound(g_ctx, g_sndMusic, g_grpMusic);
        if (mv != ZF2_INVALID_VOICE) {
            zfork2_SetVoiceLoop(g_ctx, mv, -1);
            zfork2_SetVoicePriority(g_ctx, mv, 0);
        }
    }

    // ── main loop ──────────────────────────────────────────────────────────────
    {
        system("cls");
        DWORD lastSfxTick = GetTickCount();

        while (true) {
            zfork2_Update(g_ctx);
            DrawUI();

            // Auto-trigger SFX every 2 seconds to show simultaneous voices.
            DWORD now = GetTickCount();
            if (now - lastSfxTick >= 2000) {
                lastSfxTick = now;
                ZF2_VOICE sv = zfork2_PlaySound(g_ctx, g_sndSfx, g_grpSfx);
                zfork2_SetVoicePriority(g_ctx, sv, 5);
            }

            if (_kbhit()) {
                int ch = _getch();
                switch (ch) {
                case '1': SetGroupVol(0, g_grpMusic, +0.05f); break;
                case '2': SetGroupVol(0, g_grpMusic, -0.05f); break;
                case '3': SetGroupVol(1, g_grpSfx,   +0.05f); break;
                case '4': SetGroupVol(1, g_grpSfx,   -0.05f); break;
                case '5': SetGroupVol(2, g_grpUi,    +0.05f); break;
                case '6': SetGroupVol(2, g_grpUi,    -0.05f); break;
                case 'm': case 'M': ToggleMute(0, g_grpMusic); break;
                case 's': case 'S': ToggleMute(1, g_grpSfx);   break;
                case 'u': case 'U': ToggleMute(2, g_grpUi);    break;
                case ' ':
                    // Manuell SFX ausloesen.
                    {
                        ZF2_VOICE sv = zfork2_PlaySound(g_ctx, g_sndSfx, g_grpSfx);
                        zfork2_SetVoicePriority(g_ctx, sv, 5);
                        // Optional UI-Sound gleichzeitig
                        if (g_sndUi)
                            zfork2_PlaySound(g_ctx, g_sndUi, g_grpUi);
                    }
                    break;
                case 'q': case 'Q': case 27:
                    goto done;
                }
            }

            Sleep(16);
        }
    }

done:
cleanup:
    GotoXY(0, 11);
    ci.bVisible = TRUE;
    SetConsoleCursorInfo(g_con, &ci);

    if (g_sndMusic) zfork2_UnloadSound(g_sndMusic);
    if (g_sndSfx)   zfork2_UnloadSound(g_sndSfx);
    if (g_sndUi)    zfork2_UnloadSound(g_sndUi);
    zfork2_Stop(g_ctx);
    zfork2_DestroyContext(g_ctx);
    printf("Fertig.\n");
    return 0;
}
