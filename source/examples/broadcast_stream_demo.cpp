/**
 * libZFork — Icecast MP3 Broadcast Demo
 *
 * Streamt MP3/WAV/OGG/… (Einzeldatei, Ordner oder M3U-Playlist) als MP3
 * zu einem Icecast-/Shoutcast-kompatiblen Server. ICY-Metadaten werden bei
 * jedem Titelskipped automatisch aus ID3 gesetzt.
 *
 * Beispiel (lokaler Icecast):
 *   broadcast_stream_demo show.m3u --host 127.0.0.1 --port 8000 --pass hackme
 *
 * Beispiel (Einzel-MP3, Mount /live.mp3, 128 kbps):
 *   broadcast_stream_demo track.mp3 --host stream.example.com --port 8000 \\
 *       --pass secret --mount /live.mp3 --bitrate 128
 *
 * Tasten während des Streams:
 *   Space = Pause/Fortsetzen   N = nächster Titel   Q = Beenden
 */

#define NOMINMAX
#include <libzfork/libzfork.h>
#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <thread>
#include <chrono>

using namespace libZFork;
namespace fs = std::filesystem;

struct BroadcastUi {
    ZFork*              player = nullptr;
    std::atomic<bool>   running{ true };
    std::atomic<bool>   doNext{ false };
};

static int ZFORK_CALL OnPlayer(void* inst, void* ud, CallbackMessage msg,
                               uint32_t p1, uint32_t p2) {
    auto* ui = static_cast<BroadcastUi*>(ud);
    if (!ui || !ui->player) return 0;

    switch (static_cast<int>(msg)) {
    case static_cast<int>(CallbackMessage::NextSongAsync):
        if (ui->player) {
            ID3InfoEx id3{};
            StreamStatus st = ui->player->GetStatus();
            if (ui->player->LoadID3Ex(st.songIndex, id3)) {
                ui->player->SetBroadcastMetadata(
                    id3.title[0] ? id3.title : "Unknown",
                    id3.artist[0] ? id3.artist : "",
                    id3.album[0] ? id3.album : "");
                std::printf("\n[Meta] Now: %s — %s\n",
                            id3.title[0] ? id3.title : "?",
                            id3.artist[0] ? id3.artist : "?");
            }
        }
        break;
    case static_cast<int>(CallbackMessage::BroadcastConnectedAsync):
        std::printf("\n[Broadcast] Verbunden (Server-Typ %u)\n", p1);
        break;
    case static_cast<int>(CallbackMessage::BroadcastDisconnectedAsync):
        std::printf("\n[Broadcast] Getrennt (Grund %u)\n", p1);
        break;
    case static_cast<int>(CallbackMessage::BroadcastReconnectingAsync):
        std::printf("\n[Broadcast] Reconnect #%u in %u ms\n", p1, p2);
        break;
    case static_cast<int>(CallbackMessage::BroadcastAuthFailedAsync):
        std::printf("\n[Broadcast] Login fehlgeschlagen\n");
        break;
    default:
        break;
    }
    (void)inst; (void)p2;
    return 0;
}

static bool HasFlag(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; ++i)
        if (argv[i] && !std::strcmp(argv[i], flag)) return true;
    return false;
}

static const char* GetOpt(int argc, char** argv, const char* key, const char* def) {
    for (int i = 1; i + 1 < argc; ++i)
        if (argv[i] && !std::strcmp(argv[i], key)) return argv[i + 1];
    return def;
}

static bool IsPlaylist(const fs::path& p) {
    const auto ext = p.extension().string();
    return ext == ".m3u" || ext == ".M3U" || ext == ".m3u8" || ext == ".pls";
}

static bool IsAudioFile(const fs::path& p) {
    static const char* exts[] = {
        ".mp3", ".wav", ".ogg", ".flac", ".opus", ".aac", ".m4a", ".aiff", ".aif"
    };
    const auto e = p.extension().string();
    for (const char* x : exts)
        if (_stricmp(e.c_str(), x) == 0) return true;
    return false;
}

static bool LoadSource(ZFork* p, const char* inputPath) {
    const fs::path path(inputPath);

    if (IsPlaylist(path)) {
        if (!p->LoadPlaylistFileA(inputPath, StreamFormat::Autodetect)) {
            std::printf("LoadPlaylistFileA: %s\n", p->GetError());
            return false;
        }
        std::printf("Playlist geladen: %u Titel\n", p->GetQueueCount());
        return p->GetQueueCount() > 0;
    }

    if (fs::is_directory(path)) {
        std::vector<std::string> files;
        for (const auto& e : fs::directory_iterator(path)) {
            if (e.is_regular_file() && IsAudioFile(e.path()))
                files.push_back(e.path().string());
        }
        std::sort(files.begin(), files.end());
        if (files.empty()) {
            std::printf("Keine Audiodateien in: %s\n", inputPath);
            return false;
        }
        if (!p->OpenFileA(files[0].c_str(), StreamFormat::Autodetect)) {
            std::printf("OpenFileA: %s\n", p->GetError());
            return false;
        }
        for (size_t i = 1; i < files.size(); ++i) {
            if (!p->AddFileA(files[i].c_str(), StreamFormat::Autodetect)) {
                std::printf("AddFileA(%s): %s\n", files[i].c_str(), p->GetError());
                return false;
            }
        }
        std::printf("Ordner geladen: %u Titel\n", p->GetQueueCount());
        return true;
    }

    if (!p->OpenFileA(inputPath, StreamFormat::Autodetect)) {
        std::printf("OpenFileA: %s\n", p->GetError());
        return false;
    }
    std::printf("Datei geöffnet: %s\n", inputPath);
    return true;
}

static void UpdateMetadataNow(ZFork* p) {
    ID3InfoEx id3{};
    StreamStatus st = p->GetStatus();
    if (p->LoadID3Ex(st.songIndex, id3)) {
        p->SetBroadcastMetadata(
            id3.title[0] ? id3.title : "Unknown",
            id3.artist[0] ? id3.artist : "",
            id3.album[0] ? id3.album : "");
    } else {
        p->SetBroadcastMetadata("libZFork Stream", "Demo", "");
    }
}

static void SkipToNext(ZFork* p) {
    StreamInfo info{};
    StreamStatus st = p->GetStatus();
    if (!p->GetStreamInfo(st.songIndex, info)) return;

    StreamTime pos{};
    pos.ms = info.length.ms > 800 ? info.length.ms - 500u : info.length.ms;
    pos.sec = info.length.sec;
    pos.hms = info.length.hms;
    p->Seek(SeekOrigin::Begin, pos);
}

static void PrintUsage() {
    std::printf(
        "libZFork Icecast MP3 Broadcast Demo\n\n"
        "Usage:\n"
        "  broadcast_stream_demo <datei.mp3|ordner|playlist.m3u> [Optionen]\n\n"
        "Optionen:\n"
        "  --host HOST       Icecast-Host (Default: 127.0.0.1)\n"
        "  --port PORT       Port (Default: 8000)\n"
        "  --pass PASS       Source-Passwort (Pflicht)\n"
        "  --mount PATH      Mountpoint (Default: /live.mp3)\n"
        "  --user USER       Login-User (Default: source)\n"
        "  --bitrate KBPS    MP3-Bitrate (Default: 192)\n"
        "  --shoutcast       Shoutcast statt Icecast\n\n"
        "Beispiel:\n"
        "  broadcast_stream_demo musik\\set.m3u --host 127.0.0.1 --port 8000 --pass hackme\n\n"
        "Tasten: Space=Pause  N=Nächster Titel  Q=Beenden\n");
}

int main(int argc, char** argv) {
    if (argc < 2 || HasFlag(argc, argv, "--help") || HasFlag(argc, argv, "-h")) {
        PrintUsage();
        return argc < 2 ? 1 : 0;
    }

    const char* input     = argv[1];
    const char* host      = GetOpt(argc, argv, "--host", "127.0.0.1");
    const char* pass      = GetOpt(argc, argv, "--pass", nullptr);
    const char* mount     = GetOpt(argc, argv, "--mount", "/live.mp3");
    const char* user      = GetOpt(argc, argv, "--user", "source");
    const int   port      = std::atoi(GetOpt(argc, argv, "--port", "8000"));
    const int   bitrate   = std::atoi(GetOpt(argc, argv, "--bitrate", "192"));
    const bool  shoutcast = HasFlag(argc, argv, "--shoutcast");

    if (!pass || !*pass) {
        std::printf("Fehler: --pass ist erforderlich\n\n");
        PrintUsage();
        return 1;
    }

    SetConsoleOutputCP(CP_UTF8);

    ZFork* p = CreateZFork();
    if (!p) { std::printf("CreateZFork failed\n"); return 2; }

    BroadcastUi ui{};
    ui.player = p;
    p->SetCallback(OnPlayer, &ui);

    if (!LoadSource(p, input)) {
        DestroyZFork(p);
        return 3;
    }

    BroadcastConfig cfg{};
    std::snprintf(cfg.host, sizeof(cfg.host), "%s", host);
    std::snprintf(cfg.password, sizeof(cfg.password), "%s", pass);
    std::snprintf(cfg.mount, sizeof(cfg.mount), "%s", mount);
    std::snprintf(cfg.user, sizeof(cfg.user), "%s", user);
    std::snprintf(cfg.streamName, sizeof(cfg.streamName), "%s", "libZFork MP3 Stream");
    cfg.port              = static_cast<uint16_t>(port);
    cfg.bitrateKbps       = bitrate;
    cfg.sampleRate        = 44100;
    cfg.channels          = 2;
    cfg.stereoMode        = BroadcastStereoMode::JointStereo;
    cfg.serverType        = shoutcast ? BroadcastServerType::Shoutcast
                                      : BroadcastServerType::Icecast;
    cfg.autoReconnect     = true;
    cfg.reconnectDelayMs  = 3000;

    if (!p->StartBroadcast(cfg)) {
        std::printf("StartBroadcast: %s\n", p->GetBroadcastError());
        DestroyZFork(p);
        return 4;
    }

    p->AttachBroadcastToCurrentPlayback(true);
    UpdateMetadataNow(p);

    if (!p->Play()) {
        std::printf("Play: %s\n", p->GetError());
        p->StopBroadcast();
        DestroyZFork(p);
        return 5;
    }

    std::printf("\nStream läuft → %s:%d%s  (%d kbps MP3)\n",
                host, port, mount, bitrate);
    std::printf("Space=Pause  N=Next  Q=Quit\n\n");

    while (ui.running.load()) {
        if (_kbhit()) {
            const int ch = _getch();
            if (ch == 'q' || ch == 'Q' || ch == 27) {
                ui.running = false;
            } else if (ch == ' ') {
                StreamStatus st = p->GetStatus();
                if (st.paused) p->Resume(); else p->Pause();
            } else if (ch == 'n' || ch == 'N') {
                SkipToNext(p);
            }
        }

        BroadcastStats st{};
        p->GetBroadcastStats(st);
        StreamStatus ps = p->GetStatus();
        StreamTime pos{};
        p->GetPosition(pos);

        ID3InfoEx id3{};
        p->LoadID3Ex(ps.songIndex, id3);

        std::printf("\r[%s] Titel %d/%d  %s — %s  pos=%02u:%02u  sent=%llukB  conn=%d reconnects=%llu   ",
                    ps.paused ? "PAUSE" : "LIVE ",
                    ps.songIndex + 1, ps.songsInQueue,
                    id3.title[0] ? id3.title : "?",
                    id3.artist[0] ? id3.artist : "",
                    pos.hms.minute, pos.hms.second,
                    static_cast<unsigned long long>(st.bytesSent / 1024),
                    st.connected ? 1 : 0,
                    static_cast<unsigned long long>(st.reconnectCount));
        std::fflush(stdout);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::printf("\n\nStoppe Stream…\n");
    p->Stop();
    p->StopBroadcast();
    DestroyZFork(p);
    return 0;
}
