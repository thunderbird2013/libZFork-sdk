/**
 * Timeline Demo — libZFork
 * ========================
 * Demonstriert:
 *   - Mehrere Tracks mit mehreren Clips auf einer Timeline
 *   - Fade-In / Fade-Out pro Clip
 *   - Benutzerdefinierte Lautstärke-Envelope (Keyframes)
 *   - Echtzeit-Wiedergabe mit Position-Anzeige
 *   - Offline-Export in WAV
 *
 * Aufruf:
 *   timeline_demo  track1.mp3  [track2.wav]  [sfx.wav]
 *
 * Ohne Argumente wird eine Demo-Timeline mit Stille erzeugt
 * (nur die Struktur ist sichtbar).
 *
 * Tasten:
 *   Space / P   → Play / Pause
 *   S           → Stop (zurück zu 00:00)
 *   ← →         → Seek ±10 Sekunden
 *   + -         → Master-Lautstärke
 *   E           → Export nach timeline_export.wav
 *   Q / Esc     → Ende
 */

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <string>
#include <format>
#include <cmath>

#include "timeline/timeline.h"
#include "timeline/timeline_player.h"

using namespace libZFork;

// ── Konsole ───────────────────────────────────────────────────────────────────

static std::string FormatTime(double sec) {
    int m = static_cast<int>(sec) / 60;
    int s = static_cast<int>(sec) % 60;
    int ms = static_cast<int>((sec - std::floor(sec)) * 100.0);
    return std::format("{:02}:{:02}.{:02}", m, s, ms);
}

static void PrintBar(double filled, double total, int width = 40) {
    int pos = (total > 0.0)
              ? static_cast<int>((filled / total) * width)
              : 0;
    pos = std::max(0, std::min(pos, width));
    std::putchar('[');
    for (int i = 0; i < width; ++i)
        std::putchar(i < pos ? '=' : (i == pos ? '>' : ' '));
    std::putchar(']');
}

static void PrintEnvelope(const Clip& clip) {
    if (!clip.envelope.empty()) {
        std::printf(" Envelope: %zu Keyframes", clip.envelope.size());
    } else {
        std::printf(" FadeIn=%.1fs FadeOut=%.1fs", clip.fadeInSec, clip.fadeOutSec);
    }
}

static void PrintTimeline(const Timeline& tl) {
    std::printf("┌── Timeline (%s) ────────────────────────────────────\n",
                FormatTime(tl.GetDuration()).c_str());
    for (const auto& track : tl.Tracks()) {
        std::printf("│ Track %u \"%s\"  Gain=%.1f%s\n",
                    track.id, track.name.c_str(), track.gain,
                    track.muted ? " [MUTED]" : "");
        for (const auto& clip : track.clips) {
            double clipEnd = clip.timelinePosSec + clip.durationSec;
            std::printf("│   Clip %u  @%s–%s  Gain=%.2f",
                        clip.id,
                        FormatTime(clip.timelinePosSec).c_str(),
                        FormatTime(clipEnd).c_str(),
                        clip.gain);
            PrintEnvelope(clip);
            std::printf("\n");
        }
    }
    std::printf("└──────────────────────────────────────────────────────\n");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // ANSI-Escape-Codes (Windows 10+)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    std::printf("=== libZFork Timeline Demo ===\n\n");

    // ── Timeline aufbauen ────────────────────────────────────────────────────

    Timeline tl;
    tl.sampleRate = 44100;
    tl.channels   = 2;

    // ── Track 1: Haupt-Musik ─────────────────────────────────────────────────
    auto& trackMusic = tl.AddTrack("Musik");
    trackMusic.gain  = 0.8f;

    if (argc >= 2) {
        std::wstring p(argv[1], argv[1] + strlen(argv[1]));

        // Clip A: startet bei 0s, Fade-In 2s, Fade-Out 3s
        auto& clipA = trackMusic.AddClip(p, /*timelinePos=*/0.0,
                                          /*srcOffset=*/0.0, /*duration=*/30.0);
        clipA.gain = 1.0f;
        clipA.SetFadeIn (2.0, FadeType::EqualPower);
        clipA.SetFadeOut(3.0, FadeType::EqualPower);

        // Clip B: 35s auf der Timeline, beginnt ab Sekunde 10 im gleichen File
        // Überlappung mit Clip A bei 30–35s → natürlicher Crossfade
        if (argc >= 2) {
            auto& clipB = trackMusic.AddClip(p, /*timelinePos=*/28.0,
                                              /*srcOffset=*/10.0, /*duration=*/25.0);
            clipB.gain = 1.0f;
            clipB.SetFadeIn (5.0, FadeType::EqualPower);  // 5s Fade-In → überlappend mit clipA Fade-Out
            clipB.SetFadeOut(2.0, FadeType::EqualPower);
        }
    } else {
        // Kein Argument → Platzhalter
        std::printf("Kein Track übergeben. Starte mit:\n");
        std::printf("  timeline_demo musik.mp3 [sfx.wav]\n\n");
    }

    // ── Track 2: SFX / Jingle mit benutzerdefinierter Envelope ──────────────
    auto& trackSfx = tl.AddTrack("SFX");
    trackSfx.gain  = 0.9f;

    if (argc >= 3) {
        std::wstring p(argv[2], argv[2] + strlen(argv[2]));

        // Clip mit Keyframe-Envelope:
        // 0s=still → 0.5s=laut → 5s=laut → 6s=still
        auto& sfxClip = trackSfx.AddClip(p, /*timelinePos=*/5.0,
                                           /*srcOffset=*/0.0, /*duration=*/6.0);
        sfxClip.gain = 1.0f;
        sfxClip.AddPoint(0.0, 0.0f);   // Clip-Start: still
        sfxClip.AddPoint(0.5, 1.0f);   // 0.5s: voll
        sfxClip.AddPoint(5.0, 1.0f);   // bis 5s: voll
        sfxClip.AddPoint(6.0, 0.0f);   // Clip-Ende: still
    }

    // ── Track 3: Sampler (drittes Argument) ──────────────────────────────────
    if (argc >= 4) {
        auto& trackSmp = tl.AddTrack("Sampler");
        std::wstring p(argv[3], argv[3] + strlen(argv[3]));

        // Drei kurze Wiederholungen desselben Samples
        for (int i = 0; i < 3; ++i) {
            double pos = 10.0 + i * 4.0;
            auto& c = trackSmp.AddClip(p, pos, 0.0, 3.0);
            c.gain = 0.7f;
            c.SetFadeIn (0.1, FadeType::Linear);
            c.SetFadeOut(0.3, FadeType::Linear);
        }
    }

    // ── Timeline-Struktur anzeigen ───────────────────────────────────────────
    PrintTimeline(tl);
    std::printf("\n");

    // ── Player starten ───────────────────────────────────────────────────────
    TimelinePlayer player(tl);

    // Position-Update alle ~23ms (ein Mix-Block)
    player.SetPositionCallback([](double pos) {
        // nur für interne Nutzung — Anzeige läuft im Haupt-Thread
        (void)pos;
    });

    if (!player.Play()) {
        std::fprintf(stderr, "Fehler: %s\n", player.GetError());
        return 1;
    }

    float masterGain = 1.0f;
    bool  running    = true;
    const double totalDur = tl.GetDuration();

    std::printf("\033[2J\033[H");

    while (running) {
        double pos = player.GetPosition();
        TimelineStatus st = player.GetStatus();

        // Anzeige
        std::printf("\033[H");
        PrintTimeline(tl);
        std::printf("\n");
        std::printf("  Position : %s / %s\n",
                    FormatTime(pos).c_str(), FormatTime(totalDur).c_str());
        std::printf("  ");
        PrintBar(pos, totalDur, 50);
        std::printf("\n");
        std::printf("  Status   : %s  Master: %.0f%%\n",
                    st == TimelineStatus::Playing ? "▶ PLAY"
                  : st == TimelineStatus::Paused  ? "⏸ PAUSE" : "⏹ STOP",
                    masterGain * 100.0f);
        std::printf("\n  [Space/P]=Play/Pause  [S]=Stop  [←/→]=±10s  [+/-]=Vol  [E]=Export  [Q]=Ende\n");

        if (!_kbhit()) { Sleep(60); continue; }

        int ch = _getch();
        if (ch == 0 || ch == 0xE0) {
            int ch2 = _getch();
            if (ch2 == 75) player.Seek(std::max(0.0, pos - 10.0));  // ←
            if (ch2 == 77) player.Seek(pos + 10.0);                 // →
            continue;
        }

        switch (ch) {
        case ' ': case 'p': case 'P':
            if (st == TimelineStatus::Playing) player.Pause();
            else player.Resume();
            break;
        case 's': case 'S':
            player.Stop();
            player.Seek(0.0);
            break;
        case '+': case '=':
            masterGain = std::min(2.0f, masterGain + 0.05f);
            player.SetMasterGain(masterGain);
            break;
        case '-':
            masterGain = std::max(0.0f, masterGain - 0.05f);
            player.SetMasterGain(masterGain);
            break;
        case 'e': case 'E': {
            player.Pause();
            std::printf("\nExportiere nach timeline_export.wav …\n");
            bool ok = player.ExportWav(L"timeline_export.wav",
                [](float prog){ std::printf("\r  %.0f%%", prog * 100.0f); });
            std::printf(ok ? "\n  Export fertig!\n" : "\n  Export fehlgeschlagen!\n");
            Sleep(1500);
            player.Resume();
            break;
        }
        case 'q': case 'Q': case 27:
            running = false;
            break;
        }
    }

    player.Stop();
    std::printf("\nFertig.\n");
    return 0;
}
