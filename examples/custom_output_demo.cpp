#include <libzfork/libzfork.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

using namespace libZFork;

struct ExternalSinkStats {
    std::atomic<uint64_t> framesTotal{0};
    std::atomic<float> peak{0.0f};
    std::atomic<int> channels{0};
    std::atomic<int> sampleRate{0};
};

static void ZFORK_CALL OnExternalPCM(void* /*instance*/,
                                     void* userData,
                                     const float* interleaved,
                                     uint32_t frames,
                                     int channels,
                                     int sampleRate) {
    auto* stats = static_cast<ExternalSinkStats*>(userData);
    if (!stats || !interleaved || frames == 0 || channels <= 0) return;

    float localPeak = 0.0f;
    const uint64_t sampleCount = static_cast<uint64_t>(frames) * static_cast<uint64_t>(channels);
    for (uint64_t i = 0; i < sampleCount; ++i) {
        const float a = std::fabs(interleaved[i]);
        if (a > localPeak) localPeak = a;
    }

    stats->framesTotal.fetch_add(frames, std::memory_order_relaxed);
    stats->channels.store(channels, std::memory_order_relaxed);
    stats->sampleRate.store(sampleRate, std::memory_order_relaxed);

    float prev = stats->peak.load(std::memory_order_relaxed);
    while (localPeak > prev &&
           !stats->peak.compare_exchange_weak(prev, localPeak, std::memory_order_relaxed)) {}

    // Hier würdest du den Buffer an deine eigene Audio-Engine senden:
    // MyDeviceWrite(interleaved, frames, channels, sampleRate);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("Usage:\n");
        std::printf("  custom_output_demo <audio_file> [--external-only]\n\n");
        std::printf("Options:\n");
        std::printf("  --external-only   Disable libZFork built-in output and route only via callback.\n");
        return 1;
    }

    const char* path = argv[1];
    bool externalOnly = false;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--external-only") == 0) {
            externalOnly = true;
        }
    }

    ZFork* player = CreateZFork();
    if (!player) {
        std::printf("CreateZFork failed.\n");
        return 1;
    }

    ExternalSinkStats stats{};
    if (!player->SetPCMOutputCallback(&OnExternalPCM, &stats, externalOnly)) {
        std::printf("SetPCMOutputCallback failed: %s\n", player->GetError());
        DestroyZFork(player);
        return 1;
    }

    if (!player->OpenFileA(path, StreamFormat::Autodetect)) {
        std::printf("OpenFileA failed: %s\n", player->GetError());
        DestroyZFork(player);
        return 1;
    }

    if (!player->Play()) {
        std::printf("Play failed: %s\n", player->GetError());
        DestroyZFork(player);
        return 1;
    }

    std::printf("Playing: %s\n", path);
    std::printf("Mode: %s\n", externalOnly ? "external callback only" : "built-in output + callback tap");
    std::printf("Press Ctrl+C to stop.\n");

    while (player->GetStatus().playing) {
        const uint64_t frames = stats.framesTotal.load(std::memory_order_relaxed);
        const float peak = stats.peak.exchange(0.0f, std::memory_order_relaxed);
        const int ch = stats.channels.load(std::memory_order_relaxed);
        const int sr = stats.sampleRate.load(std::memory_order_relaxed);

        std::printf("\rCallback frames=%llu  peak=%.3f  format=%dch @ %d Hz      ",
                    static_cast<unsigned long long>(frames), peak, ch, sr);
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    std::printf("\nDone.\n");
    (void)player->Stop();
    DestroyZFork(player);
    return 0;
}
