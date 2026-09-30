/**
 * libZFork — Dynamic Stream Demo
 *
 * Portiert von libZPlay/example/dynamic_stream.cpp
 *
 * Demonstriert Push-PCM-Streaming: Ein Hintergrund-Thread dekodiert die
 * Audiodatei blockweise und schiebt rohes PCM in einen zweiten Player.
 * Der Haupt-Player spielt die PCM-Daten in Echtzeit ab.
 *
 * Das entspricht dem libZPlay-Konzept "dynamic stream" — Daten werden
 * nicht einmalig geladen, sondern on-demand nachgefüllt.
 *
 * Usage:  dynamic_stream <audiodatei>
 * Tasten: Space=Pause  Q=Beenden
 */

#define NOMINMAX
#include <libzfork/libzfork.h>
#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <algorithm>

// ──────────────────────────────────────────────────────────────────────────────
// Push-Kontext: verbindet Decoder-Thread mit PCM-Stream-Player
// ──────────────────────────────────────────────────────────────────────────────

struct PushCtx {
    libZFork::ZFork*        sink    = nullptr;  // PCM-Stream-Player
    std::atomic<bool>       stop    { false };
    std::atomic<bool>       paused  { false };
    std::mutex              pauseMtx;
    std::condition_variable pauseCV;
};

// ── Decoder-Thread: liest Datei → decoded PCM → pusht in Sink ────────────────

static void DecoderThread(const wchar_t* path, PushCtx* ctx) {
    // Separaten Player zum Dekodieren öffnen (kein Audio-Ausgang)
    auto* src = libZFork::CreateZFork();
    if (!src) { ctx->stop = true; return; }

    if (!src->OpenFile(path, libZFork::StreamFormat::Autodetect)) {
        std::printf("\n[Decoder] OpenFile fehlgeschlagen: %s\n", src->GetError());
        libZFork::DestroyZFork(src);
        ctx->stop = true;
        return;
    }

    // Chunk-Größe: 4096 Frames * 2 ch * 4 Byte (float32)
    constexpr uint32_t kFrames = 4096;
    libZFork::StreamInfo info{};
    src->GetStreamInfo(0, info);
    const uint32_t ch = static_cast<uint32_t>(info.channelNumber > 0 ? info.channelNumber : 2);
    std::vector<float> buf(kFrames * ch);

    // Lautlos abspielen (nur Dekodierung, kein WaveOut)
    src->SetMasterVolume(0, 0);
    src->Play();

    while (!ctx->stop) {
        // Pause-Synchronisation
        {
            std::unique_lock<std::mutex> lk(ctx->pauseMtx);
            ctx->pauseCV.wait(lk, [&]{ return !ctx->paused || ctx->stop; });
        }
        if (ctx->stop) break;

        // Nächsten Block dekodieren
        uint32_t decoded = 0;
        if (!src->DecodeNextChunk(buf.data(), kFrames, decoded) || decoded == 0)
            break;

        // Als PCM-Bytes in den Sink schieben
        // Float32 * channels * frames = byte count
        uint32_t bytes = decoded * ch * sizeof(float);
        ctx->sink->PushPCMData(buf.data(), bytes);

        // Etwas warten falls Sink-Puffer voll (Backpressure)
        auto st = ctx->sink->GetStatus();
        while (!ctx->stop && st.playing) {
            libZFork::VUData vu{};
            ctx->sink->GetVUData(vu);
            // Wenn beide Kanäle > 0.01, genug Daten vorhanden — kurz warten
            if (vu.left > 0.01f || vu.right > 0.01f) { Sleep(5); break; }
            break;
        }
    }

    src->Stop();
    libZFork::DestroyZFork(src);
    ctx->stop = true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Hilfsfunktionen
// ──────────────────────────────────────────────────────────────────────────────

static std::wstring Utf8ToWide(const char* s) {
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), n);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

// ──────────────────────────────────────────────────────────────────────────────
// Hauptprogramm
// ──────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::printf("Usage: dynamic_stream <audiodatei>\n");
        std::printf("  Dekodiert blockweise in einen PCM-Push-Stream\n");
        return 1;
    }

    auto wpath = Utf8ToWide(argv[1]);

    // ── Quell-Info vorweg lesen ───────────────────────────────────────────────
    auto* probe = libZFork::CreateZFork();
    if (!probe) { std::printf("CreateZFork() fehlgeschlagen\n"); return 1; }

    if (!probe->OpenFile(wpath.c_str(), libZFork::StreamFormat::Autodetect)) {
        std::printf("OpenFile fehlgeschlagen: %s\n", probe->GetError());
        libZFork::DestroyZFork(probe);
        return 1;
    }

    libZFork::StreamInfo info{};
    probe->GetStreamInfo(0, info);
    libZFork::ID3InfoEx tag{};
    probe->LoadID3Ex(0, tag);
    libZFork::DestroyZFork(probe);

    int sr  = info.samplingRate  > 0 ? info.samplingRate  : 44100;
    int ch  = info.channelNumber > 0 ? info.channelNumber : 2;

    std::printf("=== libZFork Dynamic Stream ===\n");
    std::printf("Datei   : %s\n", argv[1]);
    if (tag.title[0])  std::printf("Titel   : %s\n", tag.title);
    if (tag.artist[0]) std::printf("Künstler: %s\n", tag.artist);
    std::printf("Format  : %d Hz  %d ch  %d kbps\n", sr, ch, info.bitrate);
    std::printf("Modus   : Decoder-Thread → PushPCMData → PCM-Player\n");
    std::printf("Space=Pause  Q=Beenden\n\n");

    // ── Sink-Player: PCM-Push-Stream ──────────────────────────────────────────
    auto* sink = libZFork::CreateZFork();
    if (!sink) { std::printf("CreateZFork() fehlgeschlagen\n"); return 1; }

    // Float32-PCM-Stream öffnen (isFloat = true)
    libZFork::PCMFormat pcmFmt{ sr, ch, 32, true };
    if (!sink->OpenPCMStream(pcmFmt)) {
        std::printf("OpenPCMStream fehlgeschlagen: %s\n", sink->GetError());
        libZFork::DestroyZFork(sink);
        return 1;
    }

    sink->SetMasterVolume(85, 85);
    sink->SetBufferSize(500);   // 500 ms Puffer

    PushCtx ctx;
    ctx.sink = sink;

    // ── Decoder-Thread starten ────────────────────────────────────────────────
    std::thread decoder(DecoderThread, wpath.c_str(), &ctx);

    // Kurz warten bis erste Daten gepusht wurden, dann Play()
    Sleep(150);
    sink->Play();

    // ── Haupt-Loop ────────────────────────────────────────────────────────────
    bool userPause = false;

    while (!ctx.stop) {
        if (_kbhit()) {
            int c = _getch();
            if (c == 'q' || c == 'Q' || c == 27) { ctx.stop = true; break; }
            if (c == ' ') {
                userPause = !userPause;
                ctx.paused = userPause;
                if (userPause) {
                    sink->Pause();
                } else {
                    sink->Resume();
                    ctx.pauseCV.notify_all();
                }
            }
        }

        auto st = sink->GetStatus();
        if (!st.playing && !st.paused && ctx.stop) break;

        libZFork::StreamTime pos{};
        sink->GetPosition(pos);
        libZFork::VUData vu{};
        sink->GetVUData(vu);

        int bL = static_cast<int>(std::clamp(vu.left,  0.f, 1.f) * 20);
        int bR = static_cast<int>(std::clamp(vu.right, 0.f, 1.f) * 20);

        std::printf("\r  %s  %02u:%02u:%02u  L[",
                    userPause ? "PAUSE" : "PLAY ",
                    pos.hms.hour, pos.hms.minute, pos.hms.second);
        for (int i = 0; i < 20; ++i)
            std::printf(i < bL ? (i < 14 ? "#" : (i < 18 ? "!" : "*")) : " ");
        std::printf("] R[");
        for (int i = 0; i < 20; ++i)
            std::printf(i < bR ? (i < 14 ? "#" : (i < 18 ? "!" : "*")) : " ");
        std::printf("]   ");
        std::fflush(stdout);

        Sleep(40);
    }

    // ── Aufräumen ─────────────────────────────────────────────────────────────
    ctx.stop = true;
    ctx.pauseCV.notify_all();
    if (decoder.joinable()) decoder.join();

    sink->Stop();
    libZFork::DestroyZFork(sink);

    std::printf("\n\nWiedergabe beendet.\n");
    return 0;
}
