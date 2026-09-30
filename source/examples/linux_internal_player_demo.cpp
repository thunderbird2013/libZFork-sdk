#include <libzfork/libzfork.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <string>
#include <vector>

#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
#include <portaudio.h>
#endif

using namespace libZFork;

namespace {

struct EventState {
    std::atomic<uint32_t> posMs{0};
    std::atomic<uint32_t> lenMs{0};
};

int ZFORK_CALL OnPlayerEvent(void* /*instance*/,
                             void* userData,
                             CallbackMessage msg,
                             uint32_t param1,
                             uint32_t param2) {
    auto* st = static_cast<EventState*>(userData);
    if (!st) return 0;
    if (msg == CallbackMessage::PositionAsync) {
        st->posMs.store(param1, std::memory_order_relaxed);
        st->lenMs.store(param2, std::memory_order_relaxed);
    }
    return 0;
}

#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
struct PortAudioSink {
    PaStream* stream = nullptr;
    std::atomic<bool> writeFailed{false};
    std::atomic<uint64_t> framesWritten{0};

    static bool InitOnce() {
        static std::atomic<bool> tried{false};
        static std::atomic<bool> ok{false};
        if (!tried.exchange(true)) {
            ok.store(Pa_Initialize() == paNoError, std::memory_order_relaxed);
        }
        return ok.load(std::memory_order_relaxed);
    }

    static void ListDevices() {
        if (!InitOnce()) {
            std::printf("PortAudio init failed.\n");
            return;
        }
        const int count = Pa_GetDeviceCount();
        if (count < 0) {
            std::printf("Pa_GetDeviceCount failed: %s\n", Pa_GetErrorText(count));
            return;
        }
        std::printf("PortAudio devices (%d):\n", count);
        for (int i = 0; i < count; ++i) {
            const PaDeviceInfo* d = Pa_GetDeviceInfo(i);
            if (!d) continue;
            std::printf("  [%d] %s | in=%d out=%d | sr=%.0f\n",
                        i, d->name ? d->name : "(null)",
                        d->maxInputChannels, d->maxOutputChannels, d->defaultSampleRate);
        }
        const PaDeviceIndex defOut = Pa_GetDefaultOutputDevice();
        std::printf("Default output device index: %d\n", static_cast<int>(defOut));
    }

    bool Open(int sampleRate, int channels, int preferredDeviceIndex = -1) {
        if (sampleRate <= 0 || channels <= 0) return false;

        if (!InitOnce()) return false;

        auto tryOpenOnDevice = [&](PaDeviceIndex devIdx) -> bool {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(devIdx);
            if (!info || info->maxOutputChannels <= 0) return false;

            PaStreamParameters out{};
            out.device = devIdx;
            out.channelCount = std::min<int>(channels, info->maxOutputChannels);
            out.sampleFormat = paFloat32;
            out.suggestedLatency = info->defaultLowOutputLatency;
            out.hostApiSpecificStreamInfo = nullptr;

            PaError err = Pa_OpenStream(
                &stream,
                nullptr,
                &out,
                static_cast<double>(sampleRate),
                512,
                paNoFlag,
                nullptr,
                nullptr
            );
            if (err != paNoError) return false;

            err = Pa_StartStream(stream);
            if (err != paNoError) {
                (void)Pa_CloseStream(stream);
                stream = nullptr;
                return false;
            }
            std::printf("PortAudio output opened on device %d (%s), ch=%d, sr=%d\n",
                        static_cast<int>(devIdx), info->name ? info->name : "(null)",
                        out.channelCount, sampleRate);
            return true;
        };

        if (preferredDeviceIndex >= 0 && tryOpenOnDevice(static_cast<PaDeviceIndex>(preferredDeviceIndex))) {
            return true;
        }

        const PaDeviceIndex defOut = Pa_GetDefaultOutputDevice();
        if (defOut != paNoDevice && tryOpenOnDevice(defOut)) {
            return true;
        }

        // Fallback: first available output device
        const int count = Pa_GetDeviceCount();
        for (int i = 0; i < count; ++i) {
            const PaDeviceInfo* d = Pa_GetDeviceInfo(i);
            if (!d || d->maxOutputChannels <= 0) continue;
            if (tryOpenOnDevice(i)) return true;
        }

        return false;
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
    }

    static void Shutdown() {
        if (InitOnce()) (void)Pa_Terminate();
    }
};

struct PaplaySink {
    FILE* pipe = nullptr;
    std::atomic<bool> writeFailed{false};

    bool Open(int sampleRate, int channels) {
        if (sampleRate <= 0 || channels <= 0) return false;
        char cmd[256]{};
        std::snprintf(cmd, sizeof(cmd),
                      "paplay --raw --format=float32le --rate=%d --channels=%d --latency-msec=40 -",
                      sampleRate, channels);
        pipe = popen(cmd, "w");
        return pipe != nullptr;
    }

    void Write(const float* interleaved, uint32_t frames, int channels) {
        if (!pipe || !interleaved || frames == 0 || channels <= 0) return;
        const size_t samples = static_cast<size_t>(frames) * static_cast<size_t>(channels);
        const size_t wrote = fwrite(interleaved, sizeof(float), samples, pipe);
        if (wrote != samples) writeFailed.store(true, std::memory_order_relaxed);
    }

    void Close() {
        if (!pipe) return;
        (void)fflush(pipe);
        (void)pclose(pipe);
        pipe = nullptr;
    }
};

struct RealtimeSinkCtx {
    PortAudioSink* pa = nullptr;
    PaplaySink* pp = nullptr;
};

void ZFORK_CALL OnPCMOut(void* /*instance*/,
                         void* userData,
                         const float* interleaved,
                         uint32_t frames,
                         int channels,
                         int /*sampleRate*/) {
    auto* ctx = static_cast<RealtimeSinkCtx*>(userData);
    if (!ctx) return;
    if (ctx->pa) ctx->pa->Write(interleaved, frames);
    if (ctx->pp) ctx->pp->Write(interleaved, frames, channels);
}
#endif

struct TerminalRawMode {
    termios oldTerm{};
    int oldFlags = 0;
    bool ok = false;

    bool Enable() {
        if (tcgetattr(STDIN_FILENO, &oldTerm) != 0) return false;
        termios raw = oldTerm;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return false;

        oldFlags = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (oldFlags < 0) return false;
        if (fcntl(STDIN_FILENO, F_SETFL, oldFlags | O_NONBLOCK) != 0) return false;
        ok = true;
        return true;
    }

    ~TerminalRawMode() {
        if (!ok) return;
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &oldTerm);
        (void)fcntl(STDIN_FILENO, F_SETFL, oldFlags);
    }
};

enum class EqScene {
    Flat = 0,
    Bass = 1,
    Vocal = 2,
    Bright = 3
};

void ApplyEqScene(ZFork* player, EqScene scene) {
    if (!player) return;
    for (int i = 0; i < 31; ++i) (void)player->SetEqualizerBandGain(i, 0.0f);
    switch (scene) {
        case EqScene::Flat:
            break;
        case EqScene::Bass:
            (void)player->SetEqualizerBandGain(1, +5.0f);
            (void)player->SetEqualizerBandGain(2, +4.0f);
            (void)player->SetEqualizerBandGain(3, +3.0f);
            break;
        case EqScene::Vocal:
            (void)player->SetEqualizerBandGain(6, -2.0f);
            (void)player->SetEqualizerBandGain(9, +4.0f);
            (void)player->SetEqualizerBandGain(11, +3.5f);
            (void)player->SetEqualizerBandGain(14, +2.0f);
            break;
        case EqScene::Bright:
            (void)player->SetEqualizerBandGain(14, +2.0f);
            (void)player->SetEqualizerBandGain(18, +3.5f);
            (void)player->SetEqualizerBandGain(22, +4.0f);
            break;
    }
}

const char* EqSceneName(EqScene scene) {
    switch (scene) {
        case EqScene::Flat: return "Flat";
        case EqScene::Bass: return "Bass";
        case EqScene::Vocal: return "Vocal";
        case EqScene::Bright: return "Bright";
        default: return "Unknown";
    }
}

std::string MakeBar(float v, int width) {
    v = std::clamp(v, 0.0f, 1.0f);
    const int filled = static_cast<int>(v * static_cast<float>(width));
    std::string out;
    out.reserve(static_cast<size_t>(width));
    for (int i = 0; i < width; ++i) out.push_back(i < filled ? '#' : '.');
    return out;
}

constexpr std::array<float, 31> kEq31CentersHz = {
    20.0f, 25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f,
    125.0f, 160.0f, 200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f,
    800.0f, 1000.0f, 1250.0f, 1600.0f, 2000.0f, 2500.0f, 3150.0f, 4000.0f,
    5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f, 20000.0f
};

std::array<float, 31> Compute31BandLevels(const std::vector<float>& fftL,
                                          const std::vector<float>& fftR,
                                          int sampleRate,
                                          uint32_t fftPoints) {
    std::array<float, 31> levels{};
    if (sampleRate <= 0 || fftPoints == 0 || fftL.empty() || fftR.empty()) return levels;

    auto binAtHz = [sampleRate, fftPoints](float hz) -> uint32_t {
        const float binf = (hz * static_cast<float>(fftPoints)) / static_cast<float>(sampleRate);
        if (binf <= 0.0f) return 0u;
        return static_cast<uint32_t>(std::max(0.0f, std::round(binf)));
    };

    for (size_t i = 0; i < kEq31CentersHz.size(); ++i) {
        const float c = kEq31CentersHz[i];
        const float loHz = c / std::sqrt(2.0f);
        const float hiHz = c * std::sqrt(2.0f);
        uint32_t lo = binAtHz(loHz);
        uint32_t hi = binAtHz(hiHz);
        lo = std::min<uint32_t>(lo, static_cast<uint32_t>(fftL.size() - 1u));
        hi = std::min<uint32_t>(hi, static_cast<uint32_t>(fftL.size() - 1u));
        if (hi < lo) std::swap(hi, lo);
        if (hi == lo) hi = std::min<uint32_t>(lo + 1u, static_cast<uint32_t>(fftL.size() - 1u));

        float accum = 0.0f;
        uint32_t count = 0u;
        for (uint32_t b = lo; b <= hi; ++b) {
            accum += (fftL[b] + fftR[b]) * 0.5f;
            ++count;
        }
        const float v = (count > 0u) ? (accum / static_cast<float>(count)) : 0.0f;
        levels[i] = std::clamp(v, 0.0f, 1.0f);
    }
    return levels;
}

std::string BuildFullFftHzMap(int sampleRate, uint32_t fftPoints) {
    if (sampleRate <= 0 || fftPoints == 0) return "FFT map unavailable";
    const uint32_t bins = fftPoints / 2u;
    std::string out;
    out.reserve(static_cast<size_t>(bins) * 18u);
    out += "FFTHz:";
    for (uint32_t i = 0; i < bins; ++i) {
        const float hz = (static_cast<float>(i) * static_cast<float>(sampleRate)) / static_cast<float>(fftPoints);
        char tmp[32]{};
        std::snprintf(tmp, sizeof(tmp), " %u:%.2f", i, hz);
        out += tmp;
    }
    return out;
}

std::string BuildId3Line(ZFork* player) {
    if (!player) return "ID3: n/a";
    ID3Info id3{};
    if (!player->LoadID3(0, id3)) {
        return "ID3: not available";
    }
    char line[1200]{};
    std::snprintf(line, sizeof(line),
                  "ID3: %s - %s | Album: %s | Year: %s | Track: %d",
                  id3.artist[0] ? id3.artist : "(unknown artist)",
                  id3.title[0] ? id3.title : "(unknown title)",
                  id3.album[0] ? id3.album : "(unknown album)",
                  id3.year[0] ? id3.year : "-",
                  id3.trackNumber);
    return std::string(line);
}

void PrintUsage() {
    std::printf("Usage: linux_internal_player_demo <audio_file> [--realtime] [--no-realtime] [--list-devices]\n");
    std::printf("Internal interactive player demo with Echo + EQ scenes + ID3 + key controls.\n");
}

} // namespace

int main(int argc, char** argv) {
    bool listDevices = false;
    const char* inputPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list-devices") == 0) {
            listDevices = true;
            continue;
        }
        if (argv[i][0] == '-') continue;
        if (!inputPath) inputPath = argv[i];
    }

#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
    if (listDevices) {
        PortAudioSink::ListDevices();
        std::printf("Pulse fallback binary: %s\n", (::access("/usr/bin/paplay", X_OK) == 0) ? "available" : "missing");
    }
#else
    if (listDevices) {
        std::printf("No PortAudio support in this build.\n");
    }
#endif

    if (!inputPath) {
        if (listDevices) return 0;
        PrintUsage();
        return 1;
    }
    bool realtimePlayback = true;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-realtime") == 0) realtimePlayback = false;
        if (std::strcmp(argv[i], "--realtime") == 0) realtimePlayback = true;
    }
    TerminalRawMode term;
    (void)term.Enable();

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

    StreamInfo streamInfo{};
    if (!player->GetStreamInfo(0, streamInfo)) {
        std::printf("GetStreamInfo failed: %s\n", player->GetError());
        DestroyZFork(player);
        return 1;
    }

    // --- initial setup ---
    const uint32_t fftPoints = 1024;
    (void)player->SetFFTPoints(fftPoints);
    (void)player->SetFFTWindow(FFTWindow::BlackmanHarris);
    bool eqEnabled = true;
    bool echoEnabled = true;
    bool mono = false;
    EqScene scene = EqScene::Bass;
    float tempo = 1.0f;
    float pitch = 0.0f;
    float rate = 1.0f;

    (void)player->EnableEqualizer(eqEnabled);
    ApplyEqScene(player, scene);
    (void)player->EnableEcho(echoEnabled);
    EchoParams echo{};
    echo.leftDelay = 170;
    echo.rightDelay = 220;
    echo.leftSourceVol = 86;
    echo.rightSourceVol = 86;
    echo.leftEchoVol = 28;
    echo.rightEchoVol = 24;
    (void)player->SetEchoParams(echo);

    (void)player->SetRate(rate);
    (void)player->SetPitch(pitch);
    (void)player->SetTempo(tempo);
    (void)player->SetPlayerVolume(92, 92);

#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
    PortAudioSink sink;
    PaplaySink pulseSink;
    RealtimeSinkCtx sinkCtx{};
    if (realtimePlayback) {
        if (sink.Open(streamInfo.samplingRate, streamInfo.channelNumber)) {
            sinkCtx.pa = &sink;
        } else if (pulseSink.Open(streamInfo.samplingRate, streamInfo.channelNumber)) {
            sinkCtx.pp = &pulseSink;
            std::printf("PortAudio open failed, fallback to paplay enabled.\n");
        } else {
            std::printf("Realtime playback not available (PortAudio + paplay failed).\n");
            std::printf("Use --no-realtime if you still want internal fast-run mode.\n");
            DestroyZFork(player);
            return 1;
        }

        if (!player->SetPCMOutputCallback(&OnPCMOut, &sinkCtx, true)) {
            std::printf("SetPCMOutputCallback failed: %s\n", player->GetError());
            sink.Close();
            pulseSink.Close();
            PortAudioSink::Shutdown();
            DestroyZFork(player);
            return 1;
        }
    }
#else
    if (realtimePlayback) {
        std::printf("No PortAudio support in this build, realtime mode disabled.\n");
        realtimePlayback = false;
    }
#endif

    if (!player->Play()) {
        std::printf("Play failed: %s\n", player->GetError());
#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
        if (realtimePlayback) {
            sink.Close();
            pulseSink.Close();
        }
        PortAudioSink::Shutdown();
#endif
        DestroyZFork(player);
        return 1;
    }

    std::string id3Line = BuildId3Line(player);
    std::string hzMapLine = BuildFullFftHzMap(streamInfo.samplingRate, fftPoints);
    std::string infoLine = "h:help q:quit spc:pause e:echo g:eq 1..4:eqscene +/-:tempo []:pitch ,.:rate m:mono i:id3 z:hz";
    std::string msgLine = realtimePlayback
        ? "Output: realtime (PortAudio/paplay PCM callback)"
        : "Output: internal/default (can run too fast on Linux)";

    std::printf("\033[2J\033[H\033[?25l");

    std::vector<float> fftL(fftPoints, 0.0f), fftR(fftPoints, 0.0f);
    while (player->GetStatus().playing) {
        int ch = 0;
        while ((ch = std::getchar()) != EOF) {
            const char key = static_cast<char>(ch);
            if (key == 'q' || key == 'Q') {
                (void)player->Stop();
                break;
            } else if (key == ' ') {
                auto st = player->GetStatus();
                if (st.paused) (void)player->Resume();
                else (void)player->Pause();
            } else if (key == 'h' || key == 'H') {
                msgLine = "Keys: q quit | spc pause | e echo | g eq | 1..4 scene | +/- tempo | [] pitch | ,. rate | m mono | i id3 | z hz";
            } else if (key == 'i' || key == 'I') {
                id3Line = BuildId3Line(player);
                msgLine = "ID3 refreshed";
            } else if (key == 'z' || key == 'Z') {
                msgLine = hzMapLine;
            } else if (key == 'e' || key == 'E') {
                echoEnabled = !echoEnabled;
                (void)player->EnableEcho(echoEnabled);
                msgLine = echoEnabled ? "Echo enabled" : "Echo disabled";
            } else if (key == 'g' || key == 'G') {
                eqEnabled = !eqEnabled;
                (void)player->EnableEqualizer(eqEnabled);
                msgLine = eqEnabled ? "Equalizer enabled" : "Equalizer disabled";
            } else if (key >= '1' && key <= '4') {
                scene = static_cast<EqScene>(key - '1');
                ApplyEqScene(player, scene);
                (void)player->EnableEqualizer(true);
                eqEnabled = true;
                msgLine = std::string("EQ scene: ") + EqSceneName(scene);
            } else if (key == '+') {
                tempo = std::clamp(tempo + 0.03f, 0.50f, 2.00f);
                (void)player->SetTempo(tempo);
                msgLine = "Tempo changed";
            } else if (key == '-') {
                tempo = std::clamp(tempo - 0.03f, 0.50f, 2.00f);
                (void)player->SetTempo(tempo);
                msgLine = "Tempo changed";
            } else if (key == ']') {
                pitch = std::clamp(pitch + 0.25f, -12.0f, 12.0f);
                (void)player->SetPitch(pitch);
                msgLine = "Pitch changed";
            } else if (key == '[') {
                pitch = std::clamp(pitch - 0.25f, -12.0f, 12.0f);
                (void)player->SetPitch(pitch);
                msgLine = "Pitch changed";
            } else if (key == '.') {
                rate = std::clamp(rate + 0.02f, 0.50f, 2.00f);
                (void)player->SetRate(rate);
                msgLine = "Rate changed";
            } else if (key == ',') {
                rate = std::clamp(rate - 0.02f, 0.50f, 2.00f);
                (void)player->SetRate(rate);
                msgLine = "Rate changed";
            } else if (key == 'm' || key == 'M') {
                mono = !mono;
                (void)player->MixChannels(mono);
                msgLine = mono ? "Mono ON" : "Stereo ON";
            } else if (key == 's' || key == 'S') {
                (void)player->SlideVolume(92, 92, 70, 70, 900);
                msgLine = "Volume slide triggered";
            }
        }

        VUData vu{};
        (void)player->GetVUData(vu);
        (void)player->GetFFTData(fftL.data(), fftR.data(), static_cast<uint32_t>(fftL.size()));
        const float peak = std::max(
            *std::max_element(fftL.begin(), fftL.end()),
            *std::max_element(fftR.begin(), fftR.end()));

        const uint32_t pos = events.posMs.load(std::memory_order_relaxed);
        const uint32_t len = events.lenMs.load(std::memory_order_relaxed);
        const std::string barL = MakeBar(vu.left, 20);
        const std::string barR = MakeBar(vu.right, 20);
        const auto bands31 = Compute31BandLevels(fftL, fftR, streamInfo.samplingRate, fftPoints);
        std::string bandsLine;
        bandsLine.reserve(31);
        for (float v : bands31) {
            const int q = static_cast<int>(std::clamp(v * 8.0f, 0.0f, 8.0f));
            bandsLine.push_back(" .:-=+*#%@"[q]);
        }
        // show a few representative Hz bins for quick monitoring
        auto hzBinVal = [&](float hz) {
            const uint32_t idx = static_cast<uint32_t>(std::clamp(
                std::lround((hz * static_cast<float>(fftPoints)) / static_cast<float>(streamInfo.samplingRate)),
                0l, static_cast<long>(fftPoints - 1)));
            const float lv = (idx < fftL.size()) ? fftL[idx] : 0.0f;
            const float rv = (idx < fftR.size()) ? fftR[idx] : 0.0f;
            return (lv + rv) * 0.5f;
        };
        const float h63 = hzBinVal(63.0f);
        const float h250 = hzBinVal(250.0f);
        const float h1000 = hzBinVal(1000.0f);
        const float h4000 = hzBinVal(4000.0f);
        std::printf("\033[H");
        std::printf("linux_internal_player_demo\n");
        std::printf("File: %s\n", inputPath);
        std::printf("%s\n", msgLine.c_str());
        std::printf("%s\n", id3Line.c_str());
        std::printf("%s\n", infoLine.c_str());
        std::printf("pos=%5u/%5u |L[%s] R[%s]|\n", pos, len, barL.c_str(), barR.c_str());
        std::printf("Hz: 63=%.2f 250=%.2f 1k=%.2f 4k=%.2f peak=%.3f\n",
                    h63, h250, h1000, h4000,
                    peak);
        std::printf("31-band (log): %s\n", bandsLine.c_str());
        std::printf("bands Hz: 20 25 31 40 50 63 80 100 125 160 200 250 315 400 500 630 800 1k 1k2 1k6 2k 2k5 3k15 4k 5k 6k3 8k 10k 12k5 16k 20k\n");
        std::printf("fx: eq=%s(%s) echo=%s mono=%s tempo=%.2f pitch=%.2f rate=%.2f\n",
                    eqEnabled ? "on" : "off", EqSceneName(scene),
                    echoEnabled ? "on" : "off",
                    mono ? "on" : "off",
                    tempo, pitch, rate);
        std::printf("\033[J");
        std::fflush(stdout);

#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
        if (realtimePlayback &&
            (sink.writeFailed.load(std::memory_order_relaxed) ||
             pulseSink.writeFailed.load(std::memory_order_relaxed))) {
            std::printf("\nAudio output write failed.\n");
            (void)player->Stop();
            break;
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }

    std::printf("\033[?25h\nDone.\n");
    (void)player->Stop();
#ifdef LIBZFORK_HAVE_PORTAUDIO_OUT
    if (realtimePlayback) {
        sink.Close();
        pulseSink.Close();
    }
    PortAudioSink::Shutdown();
#endif
    DestroyZFork(player);
    return 0;
}
