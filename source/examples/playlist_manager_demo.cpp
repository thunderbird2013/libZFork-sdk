/**
 * libZFork — Playlist / Queue Manager Demo
 *
 * Interaktive Konsole: Warteschlange anlegen, Titel hinzufuegen, verschieben,
 * loeschen, Duplikate erlauben/verbieten, abspielen.
 *
 * Usage:
 *   playlist_manager_demo                          (leere Queue)
 *   playlist_manager_demo start.mp3                (ein Titel)
 *   playlist_manager_demo playlist.m3u             (M3U/PLS laden)
 *
 * Befehle:
 *   list | l                     Queue anzeigen (Index, Titel, Artist, Dauer)
 *   add  <datei>                 Titel anhaengen
 *   remove | del <index>         Eintrag entfernen (nicht der aktuelle)
 *   move <von> <nach>            Eintrag verschieben
 *   dup on | dup off             Duplikate erlauben/verbieten
 *   play                         Wiedergabe starten
 *   pause                        Pause / Fortsetzen
 *   next | n                     Naechster Titel
 *   stop                         Stoppen
 *   info                         Aktuellen Titel + Position
 *   help | h                     Hilfe
 *   quit | q                     Beenden
 */

#define NOMINMAX
#include <libzfork/libzfork.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <filesystem>

using namespace libZFork;
namespace fs = std::filesystem;

static bool IsPlaylistExt(const fs::path& p) {
    const auto e = p.extension().string();
    return _stricmp(e.c_str(), ".m3u") == 0 ||
           _stricmp(e.c_str(), ".m3u8") == 0 ||
           _stricmp(e.c_str(), ".pls") == 0;
}

static void PrintQueue(ZFork* p) {
    const uint32_t n = p->GetQueueCount();
    StreamStatus st = p->GetStatus();
    const bool dup = p->GetPlaylistAllowDuplicates();

    std::printf("\n-- Queue (%u Titel, Duplikate: %s, aktiv: #%d) --\n",
                n, dup ? "an" : "aus", st.songIndex + 1);

    if (n == 0) {
        std::printf("  (leer — 'add datei.mp3' oder Startargument)\n");
        return;
    }

    for (uint32_t i = 0; i < n; ++i) {
        ID3InfoEx id3{};
        StreamInfo info{};
        p->LoadID3Ex(static_cast<int>(i), id3);
        p->GetStreamInfo(static_cast<int>(i), info);

        const char* marker = (static_cast<int>(i) == st.songIndex) ? ">" : " ";
        std::printf(" %s[%2u] %-32s  %-20s  %02u:%02u:%02u\n",
                    marker, i,
                    id3.title[0] ? id3.title : "(ohne Titel)",
                    id3.artist[0] ? id3.artist : "",
                    info.length.hms.hour,
                    info.length.hms.minute,
                    info.length.hms.second);
    }
    std::printf("\n");
}

static void PrintHelp() {
    std::printf(
        "\nBefehle:\n"
        "  list | l                 Queue anzeigen\n"
        "  add <datei>              Titel anhaengen\n"
        "  remove | del <index>     Eintrag loeschen (nicht aktueller Titel)\n"
        "  move <von> <nach>        Eintrag verschieben\n"
        "  dup on | dup off         Duplikate erlauben/verbieten\n"
        "  play                     Abspielen\n"
        "  pause                    Pause / Fortsetzen\n"
        "  next | n                 Naechster Titel\n"
        "  stop                     Stoppen\n"
        "  info                     Aktueller Titel + Position\n"
        "  help | h                 Diese Hilfe\n"
        "  quit | q                 Beenden\n\n");
}

static void SkipToNext(ZFork* p) {
    StreamStatus st = p->GetStatus();
    if (!st.playing && !st.paused) {
        std::printf("Nicht aktiv — erst 'play'\n");
        return;
    }
    StreamInfo info{};
    if (!p->GetStreamInfo(st.songIndex, info)) return;
    StreamTime pos = info.length;
    if (pos.ms > 800) pos.ms -= 500;
    p->Seek(SeekOrigin::Begin, pos);
}

static void PrintInfo(ZFork* p) {
    StreamStatus st = p->GetStatus();
    StreamTime pos{};
    p->GetPosition(pos);
    ID3InfoEx id3{};
    p->LoadID3Ex(st.songIndex, id3);

    std::printf("\nAktuell #%d/%d  %s — %s\n",
                st.songIndex + 1, st.songsInQueue,
                id3.title[0] ? id3.title : "?",
                id3.artist[0] ? id3.artist : "?");
    std::printf("Status: %s  Position: %02u:%02u:%02u\n\n",
                st.playing ? (st.paused ? "pausiert" : "spielt") : "gestoppt",
                pos.hms.hour, pos.hms.minute, pos.hms.second);
}

static bool LoadInitial(ZFork* p, const char* path) {
    const fs::path fp(path);
    if (IsPlaylistExt(fp)) {
        if (!p->LoadPlaylistFileA(path, StreamFormat::Autodetect)) {
            std::printf("LoadPlaylistFileA: %s\n", p->GetError());
            return false;
        }
        std::printf("Playlist geladen: %u Titel\n", p->GetQueueCount());
        return true;
    }
    if (!p->OpenFileA(path, StreamFormat::Autodetect)) {
        std::printf("OpenFileA: %s\n", p->GetError());
        return false;
    }
    std::printf("Geoeffnet: %s\n", path);
    return true;
}

static void Trim(std::string& s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' '))
        s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    if (i > 0) s.erase(0, i);
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);

    ZFork* p = CreateZFork();
    if (!p) { std::printf("CreateZFork failed\n"); return 1; }

    if (argc >= 2) {
        if (!LoadInitial(p, argv[1])) {
            DestroyZFork(p);
            return 2;
        }
    }

    std::printf("libZFork Playlist Manager — 'help' fuer Befehle\n");
    PrintQueue(p);

    char line[1024];
    for (;;) {
        std::printf("zfork> ");
        std::fflush(stdout);
        if (!std::fgets(line, sizeof(line), stdin)) break;

        std::string cmd(line);
        Trim(cmd);
        if (cmd.empty()) continue;

        if (cmd == "quit" || cmd == "q" || cmd == "exit") break;
        if (cmd == "help" || cmd == "h") { PrintHelp(); continue; }
        if (cmd == "list" || cmd == "l") { PrintQueue(p); continue; }
        if (cmd == "info" || cmd == "i") { PrintInfo(p); continue; }

        if (cmd == "play") {
            if (p->GetQueueCount() == 0) {
                std::printf("Queue ist leer.\n");
                continue;
            }
            if (!p->Play()) std::printf("Play: %s\n", p->GetError());
            else std::printf("Wiedergabe gestartet.\n");
            continue;
        }
        if (cmd == "pause") {
            StreamStatus st = p->GetStatus();
            if (st.paused) {
                if (!p->Resume()) std::printf("Resume: %s\n", p->GetError());
            } else {
                if (!p->Pause()) std::printf("Pause: %s\n", p->GetError());
            }
            continue;
        }
        if (cmd == "stop") {
            p->Stop();
            std::printf("Gestoppt.\n");
            continue;
        }
        if (cmd == "next" || cmd == "n") {
            SkipToNext(p);
            continue;
        }
        if (cmd == "dup on") {
            p->SetPlaylistAllowDuplicates(true);
            std::printf("Duplikate erlaubt.\n");
            continue;
        }
        if (cmd == "dup off") {
            p->SetPlaylistAllowDuplicates(false);
            std::printf("Duplikate verboten.\n");
            continue;
        }

        if (cmd.rfind("add ", 0) == 0) {
            const char* path = cmd.c_str() + 4;
            while (*path == ' ') ++path;
            if (!*path) { std::printf("Usage: add <datei>\n"); continue; }
            if (p->GetQueueCount() == 0) {
                if (!p->OpenFileA(path, StreamFormat::Autodetect))
                    std::printf("OpenFileA: %s\n", p->GetError());
            } else {
                if (!p->AddFileA(path, StreamFormat::Autodetect))
                    std::printf("AddFileA: %s\n", p->GetError());
            }
            if (p->GetQueueCount()) PrintQueue(p);
            continue;
        }

        if (cmd.rfind("remove ", 0) == 0 || cmd.rfind("del ", 0) == 0) {
            const char* pfx = cmd.rfind("remove ", 0) == 0 ? "remove " : "del ";
            const int idx = std::atoi(cmd.c_str() + std::strlen(pfx));
            if (!p->RemoveQueueItem(static_cast<uint32_t>(idx)))
                std::printf("RemoveQueueItem: %s\n", p->GetError());
            else PrintQueue(p);
            continue;
        }

        if (cmd.rfind("move ", 0) == 0) {
            unsigned from = 0, to = 0;
            if (std::sscanf(cmd.c_str() + 5, "%u %u", &from, &to) != 2) {
                std::printf("Usage: move <von> <nach>\n");
                continue;
            }
            if (!p->MoveQueueItem(from, to))
                std::printf("MoveQueueItem: %s\n", p->GetError());
            else PrintQueue(p);
            continue;
        }

        std::printf("Unbekannter Befehl — 'help'\n");
    }

    p->Stop();
    DestroyZFork(p);
    std::printf("Bye.\n");
    return 0;
}
