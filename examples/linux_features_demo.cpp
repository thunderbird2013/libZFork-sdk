#include <libzfork/libzfork.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
#include <portaudio.h>
#endif

using namespace libZFork;

namespace {

struct EventState {
    std::atomic<uint32_t> positionMs{0};
    std::atomic<uint32_t> lengthMs{0};
    std::atomic<uint32_t> eventCount{0};
};

int ZFORK_CALL OnPlayerEvent(void* /*instance*/,
                             void* userData,
                             CallbackMessage msg,
                             uint32_t param1,
                             uint32_t param2) {
    auto* st = static_cast<EventState*>(userData);
    if (!st) return 0;
    st->eventCount.fetch_add(1, std::memory_order_relaxed);
    if (msg == CallbackMessage::PositionAsync) {
        st->positionMs.store(param1, std::memory_order_relaxed);
        st->lengthMs.store(param2, std::memory_order_relaxed);
    }
    return 0;
}

#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
struct PortAudioSink {
    PaStream* stream = nullptr;
    std::atomic<bool> writeFailed{false};
    std::atomic<uint64_t> framesWritten{0};

    bool Open(int sampleRate, int channels) {
        if (sampleRate <= 0 || channels <= 0) return false;
        PaError err = Pa_Initialize();
        if (err != paNoError) return false;
        err = Pa_OpenDefaultStream(&stream, 0, channels, paFloat32, sampleRate, 512, nullptr, nullptr);
        if (err != paNoError) {
            (void)Pa_Terminate();
            return false;
        }
        err = Pa_StartStream(stream);
        if (err != paNoError) {
            (void)Pa_CloseStream(stream);
            stream = nullptr;
            (void)Pa_Terminate();
            return false;
        }
        return true;
    }

    void Write(const float* interleaved, uint32_t frames) {
        if (!stream || !interleaved || frames == 0) return;
        const PaError err = Pa_WriteStream(stream, interleaved, frames);
        if (err != paNoError && err != paOutputUnderflowed) {
            writeFailed.store(true, std::memory_order_relaxed);
        } else {
            framesWritten.fetch_add(frames, std::memory_order_relaxed);
        }
    }

    void Close() {
        if (!stream) return;
        (void)Pa_StopStream(stream);
        (void)Pa_CloseStream(stream);
        stream = nullptr;
        (void)Pa_Terminate();
    }
};

void ZFORK_CALL OnPCMOut(void* /*instance*/,
                         void* userData,
                         const float* interleaved,
                         uint32_t frames,
                         int /*channels*/,
                         int /*sampleRate*/) {
    auto* sink = static_cast<PortAudioSink*>(userData);
    if (!sink) return;
    sink->Write(interleaved, frames);
}
#endif

void PrintUsage() {
    std::printf("Usage: linux_features_demo <audio_file> [--internal] [--external]\n");
    std::printf("Features: callbacks, VU+FFT, EQ, Echo, Rate/Pitch/Tempo, volume slide, pause/resume.\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    const char* inputPath = argv[1];
    bool externalMode = false;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--external") == 0) externalMode = true;
        if (std::strcmp(argv[i], "--internal") == 0) externalMode = false;
    }

#ifndef LIBZFORK_HAVE_PORTAUDIO_OUT
    if (externalMode) {
        std::printf("This build has no PortAudio output support.\n");
        return 1;
    }
#endif

    ZFork* player = CreateZFork();
    if (!player) {
        std::printf("CreateZFork failed.\n");
        return 1;
    }

    EventState events{};
    (void)player->SetCallback(&OnPlayerEvent, &events);

    if (!player->OpenFileA(inputPath, StreamFormat::Autodetect)) {
        std::printf("OpenFileA failed: %s\n", player->GetError());
        DestroyZFork(player);
        return 1;
    }

    // Feature setup: FFT + EQ + Echo + playback shaping
    (void)player->SetFFTPoints(1024);
    (void)player->SetFFTWindow(FFTWindow::Hann);
    (void)player->EnableEqualizer(true);
    (void)player->SetEqualizerBandGain(3, +4.0f);
    (void)player->SetEqualizerBandGain(10, -2.5f);
    (void)player->EnableEcho(true);
    EchoParams ep{};
    ep.leftDelay = 180; ep.rightDelay = 220;
    ep.leftSourceVol = 88; ep.rightSourceVol = 88;
    ep.leftEchoVol = 28; ep.rightEchoVol = 24;
    (void)player->SetEchoParams(ep);
    (void)player->SetRate(1.0f);
    (void)player->SetPitch(0.0f);
    (void)player->SetTempo(1.0f);

#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
    PortAudioSink sink;
    if (externalMode) {
        StreamInfo info{};
        if (!player->GetStreamInfo(0, info) || !sink.Open(info.samplingRate, info.channelNumber)) {
            std::printf("External output init failed.\n");
            DestroyZFork(player);
            return 1;
        }
        if (!player->SetPCMOutputCallback(&OnPCMOut, &sink, true)) {
            std::printf("SetPCMOutputCallback failed: %s\n", player->GetError());
            sink.Close();
            DestroyZFork(player);
            return 1;
        }
    } else {
        (void)player->SetPCMOutputCallback(nullptr, nullptr, false);
    }
#endif

    if (!player->Play()) {
        std::printf("Play failed: %s\n", player->GetError());
#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
        if (externalMode) sink.Close();
#endif
        DestroyZFork(player);
        return 1;
    }

    std::printf("Playing '%s' via %s path.\n", inputPath, externalMode ? "external" : "internal");
    std::printf("Auto-demo: slide volume, pause/resume, tempo/pitch changes while running.\n");

    auto start = std::chrono::steady_clock::now();
    bool didSlide = false;
    bool didPause = false;
    bool didResume = false;
    bool didTempo = false;
    bool didPitch = false;
    bool didMono = false;

    std::vector<float> fftL(1024, 0.0f), fftR(1024, 0.0f);
    while (player->GetStatus().playing) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();

        if (!didSlide && elapsed >= 2) {
            (void)player->SlideVolume(100, 100, 70, 70, 1200);
            didSlide = true;
        }
        if (!didPause && elapsed >= 5) {
            (void)player->Pause();
            didPause = true;
        }
        if (didPause && !didResume && elapsed >= 6) {
            (void)player->Resume();
            didResume = true;
        }
        if (!didTempo && elapsed >= 8) {
            (void)player->SetTempo(1.08f);
            didTempo = true;
        }
        if (!didPitch && elapsed >= 10) {
            (void)player->SetPitch(+1.5f);
            didPitch = true;
        }
        if (!didMono && elapsed >= 12) {
            (void)player->MixChannels(true);
            didMono = true;
        }

        VUData vu{};
        (void)player->GetVUData(vu);
        (void)player->GetFFTData(fftL.data(), fftR.data(), static_cast<uint32_t>(fftL.size()));
        const float fftPeak = std::max(
            *std::max_element(fftL.begin(), fftL.end()),
            *std::max_element(fftR.begin(), fftR.end()));

        const uint32_t pos = events.positionMs.load(std::memory_order_relaxed);
        const uint32_t len = events.lengthMs.load(std::memory_order_relaxed);
        const uint32_t evc = events.eventCount.load(std::memory_order_relaxed);
        std::printf("\rpos=%5u/%5u ms  vu=%.2f/%.2f  fftPeak=%.3f  events=%u",
                    pos, len, vu.left, vu.right, fftPeak, evc);
        std::fflush(stdout);

#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
        if (externalMode && sink.writeFailed.load(std::memory_order_relaxed)) {
            std::printf("\nExternal output failed.\n");
            break;
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }

    std::printf("\nDone.\n");
    (void)player->Stop();
#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
    if (externalMode) sink.Close();
#endif
    DestroyZFork(player);
    return 0;
}
