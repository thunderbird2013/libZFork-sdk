/**
 * DeckManager Demo — libZFork
 * ===========================
 * Zeigt wie man mehrere Decks (A, B, C) gleichzeitig betreibt:
 *   - Tracks laden auf verschiedene Decks
 *   - Play / Pause / Stop pro Deck
 *   - Crossfader zwischen Deck A und B
 *   - BPM erkennen + synchronisieren
 *   - EQ pro Deck
 *   - VU-Meter für alle Decks
 *
 * Tasten:
 *   A / B / C   → Deck-Auswahl (aktives Deck)
 *   L           → Track laden (Dateinamen eingeben)
 *   P           → Play / Pause toggle
 *   S           → Stop
 *   ← →         → Crossfader Deck A ↔ B
 *   X           → Auto-Crossfade A→B (4 Sekunden)
 *   D           → BPM erkennen (aktives Deck)
 *   Y           → BPM-Sync B an A anpassen
 *   + -         → Pitch ±1 Semitöne
 *   E           → EQ-Demo (Bass boost / Cut)
 *   Q / Esc     → Beenden
 */

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <string>
#include <format>
#include "core/deck_manager.h"

using namespace libZFork;

// ── Konsole-Hilfsfunktionen ───────────────────────────────────────────────────

static void ClearLine() { std::printf("\r\033[K"); }

static void PrintBar(float level, int width = 20) {
    int filled = static_cast<int>(level * width);
    filled = std::max(0, std::min(filled, width));
    std::putchar('[');
    for (int i = 0; i < width; ++i)
        std::putchar(i < filled ? '#' : ' ');
    std::putchar(']');
}

static std::string StatusStr(PlayerStatus s) {
    switch (s) {
    case PlayerStatus::Playing: return "PLAY ";
    case PlayerStatus::Paused:  return "PAUSE";
    default:                    return "STOP ";
    }
}

static std::string FormatTime(float sec) {
    int m = static_cast<int>(sec) / 60;
    int s = static_cast<int>(sec) % 60;
    return std::format("{:02}:{:02}", m, s);
}

// ── VU-Anzeige ────────────────────────────────────────────────────────────────

static void PrintVU(DeckManager& mgr, int deckId, char deckChar) {
    VUData vu{};
    mgr.GetVU(deckId, vu);
    float l = std::min(static_cast<float>(vu.left)  / 100.0f, 1.0f);
    float r = std::min(static_cast<float>(vu.right) / 100.0f, 1.0f);
    std::printf(" Deck %c L:", deckChar); PrintBar(l, 12);
    std::printf(" R:"); PrintBar(r, 12);
}

// ── Status-Zeile ─────────────────────────────────────────────────────────────

static void PrintStatus(DeckManager& mgr, int deckA, int deckB, int deckC,
                        int active, float crossfade) {
    DeckInfo a, b, c;
    mgr.GetDeckInfo(deckA, a);
    mgr.GetDeckInfo(deckB, b);
    mgr.GetDeckInfo(deckC, c);

    std::printf("\n");
    std::printf("┌─────── Deck A [%s] ─────────────────────────────────────────────────┐\n",
                active == deckA ? "●" : " ");
    std::printf("│  Track : %-55ls│\n",
                a.trackPath.empty() ? L"(leer)" : a.trackPath.c_str());
    std::printf("│  Zeit  : %s / %s   BPM: %5.1f   Pitch: %+.1f st   Vol: %3d%%  │\n",
                FormatTime(a.positionSec).c_str(), FormatTime(a.durationSec).c_str(),
                a.bpm, a.pitchSemi, a.volumeLeft);
    std::printf("│  Status: %s  ", StatusStr(a.status).c_str());
    PrintVU(mgr, deckA, 'A');
    std::printf("  │\n");

    // Crossfader-Anzeige
    int xpos = static_cast<int>(crossfade * 20.0f);
    std::printf("│  Crossfader: A ");
    for (int i = 0; i <= 20; ++i)
        std::putchar(i == xpos ? '|' : (i < xpos ? '─' : '─'));
    std::printf(" B  (%.0f%%)  │\n", crossfade * 100.0f);

    std::printf("├─────── Deck B [%s] ─────────────────────────────────────────────────┤\n",
                active == deckB ? "●" : " ");
    std::printf("│  Track : %-55ls│\n",
                b.trackPath.empty() ? L"(leer)" : b.trackPath.c_str());
    std::printf("│  Zeit  : %s / %s   BPM: %5.1f   Pitch: %+.1f st   Vol: %3d%%  │\n",
                FormatTime(b.positionSec).c_str(), FormatTime(b.durationSec).c_str(),
                b.bpm, b.pitchSemi, b.volumeLeft);
    std::printf("│  Status: %s  ", StatusStr(b.status).c_str());
    PrintVU(mgr, deckB, 'B');
    std::printf("  │\n");

    std::printf("├─────── Deck C [%s] (Sampler) ───────────────────────────────────────┤\n",
                active == deckC ? "●" : " ");
    std::printf("│  Track : %-55ls│\n",
                c.trackPath.empty() ? L"(leer)" : c.trackPath.c_str());
    std::printf("│  Zeit  : %s / %s   BPM: %5.1f   Pitch: %+.1f st   Vol: %3d%%  │\n",
                FormatTime(c.positionSec).c_str(), FormatTime(c.durationSec).c_str(),
                c.bpm, c.pitchSemi, c.volumeLeft);
    std::printf("│  Status: %s  ", StatusStr(c.status).c_str());
    PrintVU(mgr, deckC, 'C');
    std::printf("  │\n");

    std::printf("└──────────────────────────────────────────────────────────────────────┘\n");
    std::printf("  [A/B/C]=Deck  [L]=Load  [P]=Play/Pause  [S]=Stop\n");
    std::printf("  [←/→]=Crossfader  [X]=Auto-Crossfade A→B (4s)\n");
    std::printf("  [D]=BPM-Detect  [Y]=BPM-Sync B→A  [+/-]=Pitch  [E]=EQ  [Q]=Ende\n");
}

// ── Hilfsfunktion: Track-Pfad einlesen ───────────────────────────────────────

static std::wstring InputPath(char deckChar) {
    std::printf("\nTrack-Pfad für Deck %c eingeben:\n> ", deckChar);
    std::wstring path(512, L'\0');
    _getws_s(path.data(), static_cast<unsigned>(path.size()));
    path.resize(std::wcslen(path.c_str()));
    return path;
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // ANSI-Escape-Codes aktivieren (Windows 10+)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    std::printf("=== libZFork DeckManager Demo ===\n\n");

    DeckManager mgr;

    // Drei Decks erstellen
    int deckA = mgr.CreateDeck("A");
    int deckB = mgr.CreateDeck("B");
    int deckC = mgr.CreateDeck("C");

    // Optional: Tracks aus Kommandozeile laden
    if (argc >= 2) {
        std::wstring p(argv[1], argv[1] + strlen(argv[1]));
        mgr.LoadTrack(deckA, p.c_str());
    }
    if (argc >= 3) {
        std::wstring p(argv[2], argv[2] + strlen(argv[2]));
        mgr.LoadTrack(deckB, p.c_str());
    }
    if (argc >= 4) {
        std::wstring p(argv[3], argv[3] + strlen(argv[3]));
        mgr.LoadTrack(deckC, p.c_str());
    }

    int   active    = deckA;
    float crossfade = 0.0f;   // 0.0 = voll A, 1.0 = voll B
    bool  running   = true;

    // Crossfader initial setzen
    mgr.SetCrossfader(deckA, deckB, crossfade);

    // Konsole aufräumen
    std::printf("\033[2J\033[H");

    while (running) {
        // Status neu zeichnen
        std::printf("\033[H");   // Cursor nach oben
        PrintStatus(mgr, deckA, deckB, deckC, active, crossfade);

        if (!_kbhit()) {
            Sleep(80);
            continue;
        }

        int ch = _getch();
        if (ch == 0 || ch == 0xE0) {
            // Pfeiltasten
            int ch2 = _getch();
            if (ch2 == 75) {
                // Pfeil links → Crossfader zu A
                crossfade = std::max(0.0f, crossfade - 0.05f);
                mgr.SetCrossfader(deckA, deckB, crossfade);
            } else if (ch2 == 77) {
                // Pfeil rechts → Crossfader zu B
                crossfade = std::min(1.0f, crossfade + 0.05f);
                mgr.SetCrossfader(deckA, deckB, crossfade);
            }
            continue;
        }

        switch (ch) {
        // ── Deck-Auswahl ──────────────────────────────────────────────────────
        case 'a': case 'A': active = deckA; break;
        case 'b': case 'B': active = deckB; break;
        case 'c': case 'C': active = deckC; break;

        // ── Track laden ───────────────────────────────────────────────────────
        case 'l': case 'L': {
            char deckChar = (active == deckA) ? 'A' : (active == deckB) ? 'B' : 'C';
            std::wstring path = InputPath(deckChar);
            if (!path.empty()) {
                if (!mgr.LoadTrack(active, path.c_str()))
                    std::printf("Fehler: %s\n", mgr.GetError());
            }
            break;
        }

        // ── Transport ─────────────────────────────────────────────────────────
        case 'p': case 'P': {
            auto st = mgr.GetStatus(active);
            if (st == PlayerStatus::Playing)
                mgr.Pause(active);
            else if (st == PlayerStatus::Paused)
                mgr.Resume(active);
            else
                mgr.Play(active);
            break;
        }
        case 's': case 'S':
            mgr.Stop(active);
            break;

        // ── Auto-Crossfade A→B ────────────────────────────────────────────────
        case 'x': case 'X':
            std::printf("\nCrossfade A → B (4s)…\n");
            mgr.Crossfade(deckA, deckB, 4000);
            crossfade = 1.0f;
            break;

        // ── BPM ───────────────────────────────────────────────────────────────
        case 'd': case 'D': {
            float bpm = 0.0f;
            std::printf("\nBPM-Analyse läuft…");
            if (mgr.DetectBPM(active, bpm))
                std::printf(" → %.1f BPM\n", bpm);
            else
                std::printf(" Fehler: %s\n", mgr.GetError());
            Sleep(1000);
            break;
        }
        case 'y': case 'Y':
            if (!mgr.SyncBPM(deckA, deckB))
                std::printf("\nSync-Fehler: %s\n", mgr.GetError());
            else
                std::printf("\nDeck B an Deck A synchronisiert.\n");
            Sleep(800);
            break;

        // ── Pitch ─────────────────────────────────────────────────────────────
        case '+': case '=': {
            DeckInfo info;
            mgr.GetDeckInfo(active, info);
            mgr.SetPitch(active, info.pitchSemi + 1.0f);
            break;
        }
        case '-': {
            DeckInfo info;
            mgr.GetDeckInfo(active, info);
            mgr.SetPitch(active, info.pitchSemi - 1.0f);
            break;
        }

        // ── EQ-Demo ───────────────────────────────────────────────────────────
        case 'e': case 'E': {
            static bool eqOn = false;
            eqOn = !eqOn;
            mgr.EnableEQ(active, true);
            if (eqOn) {
                // Bass-Boost: Band 0+1 +6 dB, Höhen Band 8+9 -3 dB
                mgr.SetEQBand(active, 0, +6.0f);
                mgr.SetEQBand(active, 1, +6.0f);
                mgr.SetEQBand(active, 8, -3.0f);
                mgr.SetEQBand(active, 9, -3.0f);
                std::printf("\nEQ: Bass-Boost an\n");
            } else {
                mgr.ResetEQ(active);
                std::printf("\nEQ: Reset\n");
            }
            Sleep(500);
            break;
        }

        // ── Beenden ───────────────────────────────────────────────────────────
        case 'q': case 'Q': case 27:
            running = false;
            break;
        }
    }

    mgr.Stop(deckA);
    mgr.Stop(deckB);
    mgr.Stop(deckC);
    std::printf("\nFertig.\n");
    return 0;
}
