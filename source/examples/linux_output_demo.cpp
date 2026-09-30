#include <libzfork/libzfork.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
#include <portaudio.h>
#endif

using namespace libZFork;

namespace {

#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
struct PortAudioSink {
    PaStream* stream = nullptr;
    std::atomic<bool> writeFailed{false};
    std::atomic<uint64_t> framesWritten{0};

    bool Open(int sampleRate, int channels) {
        if (sampleRate <= 0 || channels <= 0) return false;

        PaError err = Pa_Initialize();
        if (err != paNoError) {
            std::printf("PortAudio init failed: %s\n", Pa_GetErrorText(err));
            return false;
        }

        err = Pa_OpenDefaultStream(
            &stream,
            0,              // no input
            channels,       // output channels
            paFloat32,      // float32 interleaved
            sampleRate,
            512,            // frames per buffer
            nullptr,
            nullptr
        );
        if (err != paNoError) {
            std::printf("Pa_OpenDefaultStream failed: %s\n", Pa_GetErrorText(err));
            Pa_Terminate();
            return false;
        }

        err = Pa_StartStream(stream);
        if (err != paNoError) {
            std::printf("Pa_StartStream failed: %s\n", Pa_GetErrorText(err));
            Pa_CloseStream(stream);
            stream = nullptr;
            Pa_Terminate();
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
#endif

void ZFORK_CALL OnPCMOut(void* /*instance*/,
                         void* userData,
                         const float* interleaved,
                         uint32_t frames,
                         int /*channels*/,
                         int /*sampleRate*/) {
#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
    auto* sink = static_cast<PortAudioSink*>(userData);
    if (!sink) return;
    sink->Write(interleaved, frames);
#else
    (void)userData;
    (void)interleaved;
    (void)frames;
#endif
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("Usage: linux_output_demo <audio_file> [--internal] [--external]\n");
        std::printf("  --internal  Use libZFork internal output (default).\n");
        std::printf("  --external  Route playback via PCM callback to PortAudio output.\n");
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
        std::printf("This build has no PortAudio output support (LIBZFORK_HAVE_PORTAUDIO_OUT not set).\n");
        return 1;
    }
#endif

    ZFork* player = CreateZFork();
    if (!player) {
        std::printf("CreateZFork failed.\n");
        return 1;
    }

    if (!player->OpenFileA(inputPath, StreamFormat::Autodetect)) {
        std::printf("OpenFileA failed: %s\n", player->GetError());
        DestroyZFork(player);
        return 1;
    }

#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
    PortAudioSink sink;
    if (externalMode) {
        StreamInfo info{};
        if (!player->GetStreamInfo(0, info)) {
            std::printf("GetStreamInfo failed: %s\n", player->GetError());
            DestroyZFork(player);
            return 1;
        }
        if (!sink.Open(info.samplingRate, info.channelNumber)) {
            DestroyZFork(player);
            return 1;
        }
        // disableBuiltInOutput=true: libZFork writes only to callback.
        if (!player->SetPCMOutputCallback(&OnPCMOut, &sink, true)) {
            std::printf("SetPCMOutputCallback failed: %s\n", player->GetError());
            sink.Close();
            DestroyZFork(player);
            return 1;
        }
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

    std::printf("Playing '%s' via %s output path...\n",
                inputPath, externalMode ? "external (PCM callback + PortAudio)" : "internal");
    while (player->GetStatus().playing) {
#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
        if (sink.writeFailed.load(std::memory_order_relaxed)) {
            std::printf("\nPortAudio write failed.\n");
            break;
        }
        if (externalMode) {
            const uint64_t f = sink.framesWritten.load(std::memory_order_relaxed);
            std::printf("\rFrames written: %llu", static_cast<unsigned long long>(f));
        } else {
            std::printf("\rPlaying...");
        }
#else
        std::printf("\rPlaying...");
#endif
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::printf("\nDone.\n");

    (void)player->Stop();
#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
    if (externalMode) sink.Close();
#endif
    DestroyZFork(player);
    return 0;
}
