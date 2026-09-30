/**
 * libZFork2 — Trigger-Latency Probe
 *
 * Measures the time between calling zfork2_PlaySound() and the engine
 * actually dispatching ZF2_EVENT_VOICE_START for that voice — i.e. the
 * command-queue + engine-thread-loop latency that kMixFrames (see
 * engine_defs.h) directly bounds. This does NOT include the audio output's
 * own hardware/driver buffering (WaveOutOutput::kBufferCount), which adds
 * further (separately tunable) delay before the sound is actually audible.
 *
 * Usage: latency_probe [trials]   (default: 200)
 */

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <libzfork/libzfork2.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <vector>

using Clock = std::chrono::high_resolution_clock;

static std::atomic<uint32_t> g_awaitedVoice{ ZF2_INVALID_VOICE };
static std::atomic<long long> g_startTicks{ 0 };
static std::atomic<bool>      g_got{ false };

static void OnEvent(int type, ZF2_VOICE voice, uint32_t, float, void*) {
    if (type == ZF2_EVENT_VOICE_START && voice == g_awaitedVoice.load(std::memory_order_acquire)) {
        g_startTicks.store(Clock::now().time_since_epoch().count(), std::memory_order_release);
        g_got.store(true, std::memory_order_release);
    }
}

int main(int argc, char* argv[]) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const int kTrials = (argc >= 2) ? std::atoi(argv[1]) : 200;

    const std::filesystem::path candidates[] = {
        "samples/Menu Selection Click.wav",
        "examples/samples/Menu Selection Click.wav",
    };
    std::filesystem::path wavPath;
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c)) { wavPath = c; break; }
    }
    if (wavPath.empty()) {
        std::fprintf(stderr, "FEHLER: Testdatei 'Menu Selection Click.wav' nicht gefunden.\n");
        return 1;
    }

    ZF2_CONTEXT ctx = zfork2_CreateContext();
    if (!ctx) { std::fprintf(stderr, "FEHLER: CreateContext fehlgeschlagen\n"); return 1; }
    zfork2_SetCallback(ctx, OnEvent, nullptr);
    if (!zfork2_Start(ctx)) {
        std::fprintf(stderr, "FEHLER: Start fehlgeschlagen (Soundkarte verfuegbar?)\n");
        zfork2_DestroyContext(ctx);
        return 1;
    }

    ZF2_SOUND snd = zfork2_LoadSound(ctx, wavPath.wstring().c_str(), ZF2_SOUND_PREDECODED);
    if (!snd) {
        std::fprintf(stderr, "FEHLER: LoadSound fehlgeschlagen (%s)\n", wavPath.string().c_str());
        zfork2_Stop(ctx);
        zfork2_DestroyContext(ctx);
        return 1;
    }
    ZF2_GROUP sfx = zfork2_GetGroup(ctx, "sfx");

    char     backendName[64] = {};
    uint32_t periodFrames    = 0;
    if (zfork2_GetOutputBackendInfo(ctx, backendName, sizeof(backendName), &periodFrames)) {
        std::printf("Backend: %s", backendName);
        if (periodFrames > 0)
            std::printf(" (Geraete-Periode: %u Frames, %.3f ms)",
                        periodFrames, periodFrames * 1000.0 / 44100.0);
        std::printf("\n");
    }

    std::printf("=== libZFork2 Trigger-Latency Probe ===\n");
    std::printf("Datei:   %s\n", wavPath.string().c_str());
    std::printf("Trials:  %d\n", kTrials);
    std::printf("Miss:    PlaySound()-Aufruf bis ZF2_EVENT_VOICE_START (ohne Hardware-Ausgabepuffer)\n\n");

    std::vector<double> samplesMs;
    samplesMs.reserve(static_cast<size_t>(kTrials));

    for (int i = 0; i < kTrials; ++i) {
        g_got.store(false, std::memory_order_release);

        const auto t0 = Clock::now();
        ZF2_VOICE v = zfork2_PlaySound(ctx, snd, sfx);
        if (v == ZF2_INVALID_VOICE) { --i; continue; }
        g_awaitedVoice.store(v, std::memory_order_release);

        // Tight poll (no sleep) — measures the pipeline's own latency
        // instead of our own poll granularity.
        while (!g_got.load(std::memory_order_acquire)) {
            zfork2_Update(ctx);
        }
        const long long elapsedTicks = g_startTicks.load(std::memory_order_acquire)
                                      - t0.time_since_epoch().count();
        samplesMs.push_back(std::chrono::duration<double, std::milli>(
            Clock::duration(elapsedTicks)).count());

        zfork2_StopVoice(ctx, v);
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // let things settle between trials
    }

    std::sort(samplesMs.begin(), samplesMs.end());
    double sum = 0.0;
    for (double v : samplesMs) sum += v;

    const double avg = sum / samplesMs.size();
    const double p50 = samplesMs[samplesMs.size() / 2];
    const double p95 = samplesMs[static_cast<size_t>(samplesMs.size() * 0.95)];
    const double mn  = samplesMs.front();
    const double mx  = samplesMs.back();

    std::printf("min=%.3f ms  p50=%.3f ms  avg=%.3f ms  p95=%.3f ms  max=%.3f ms\n",
                mn, p50, avg, p95, mx);

    zfork2_UnloadSound(snd);
    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
    return 0;
}
