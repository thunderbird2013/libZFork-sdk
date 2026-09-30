#if defined(LIBZFORK_WITH_PORTAUDIO) && !defined(_WIN32)

#include "portaudio_output.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <portaudio.h>

namespace libZFork {

namespace {

int CountOutputDevices() {
    const int n = Pa_GetDeviceCount();
    if (n < 0) return 0;
    int out = 0;
    for (int i = 0; i < n; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels > 0) ++out;
    }
    return out;
}

// Map logical index (0..N-1 among output-capable devices) → PortAudio device index.
int MapLogicalToPaDevice(int logical) {
    const int n = Pa_GetDeviceCount();
    if (n < 0 || logical < 0) return paNoDevice;
    int out = 0;
    for (int i = 0; i < n; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info || info->maxOutputChannels <= 0) continue;
        if (out == logical) return i;
        ++out;
    }
    return paNoDevice;
}

} // namespace

PortAudioOutput::PortAudioOutput() = default;

PortAudioOutput::~PortAudioOutput() {
    Stop();
    Close();
}

bool PortAudioOutput::EnsurePaInitialized() {
    if (paOwned_) return true;
    if (Pa_Initialize() != paNoError) return false;
    paOwned_ = true;
    return true;
}

void PortAudioOutput::ReleasePa() {
    if (!paOwned_) return;
    Pa_Terminate();
    paOwned_ = false;
}

bool PortAudioOutput::Init(int sampleRate, int channels, int bitsPerSample, uint32_t /*bufferMs*/) {
    Close();
    if (bitsPerSample != 16) return false;
    if (channels < 1 || channels > 8) return false;
    if (sampleRate < 8000) return false;

    if (!EnsurePaInitialized()) return false;

    sampleRate_ = sampleRate;
    channels_   = channels;
    bits_       = bitsPerSample;

    PaDeviceIndex paDev = (deviceId_ < 0)
        ? Pa_GetDefaultOutputDevice()
        : MapLogicalToPaDevice(deviceId_);
    if (paDev == paNoDevice) {
        ReleasePa();
        return false;
    }

    const PaDeviceInfo* info = Pa_GetDeviceInfo(paDev);
    if (!info || info->maxOutputChannels < channels) {
        ReleasePa();
        return false;
    }

    PaStreamParameters out{};
    out.device                    = paDev;
    out.channelCount              = channels;
    out.sampleFormat              = paInt16;
    out.suggestedLatency          = info->defaultLowOutputLatency;
    out.hostApiSpecificStreamInfo = nullptr;

    // framesPerBuffer = 0 → PortAudio picks; WriteBlock pushes engine chunks.
    PaError err = Pa_OpenStream(&stream_,
                                nullptr,
                                &out,
                                static_cast<double>(sampleRate),
                                paFramesPerBufferUnspecified,
                                paClipOff,
                                nullptr,
                                nullptr);
    if (err != paNoError || !stream_) {
        stream_ = nullptr;
        ReleasePa();
        return false;
    }

    stopped_.store(false, std::memory_order_release);
    paused_.store(false, std::memory_order_release);
    return true;
}

bool PortAudioOutput::Start() {
    if (!stream_) return false;
    if (Pa_IsStreamActive(stream_) == 1) return true;
    return Pa_StartStream(stream_) == paNoError;
}

void PortAudioOutput::Pause() {
    paused_.store(true, std::memory_order_release);
    if (stream_ && Pa_IsStreamActive(stream_) == 1)
        Pa_StopStream(stream_);
}

void PortAudioOutput::Resume() {
    paused_.store(false, std::memory_order_release);
    if (stream_ && !stopped_.load(std::memory_order_acquire))
        Pa_StartStream(stream_);
}

void PortAudioOutput::Stop() {
    stopped_.store(true, std::memory_order_release);
    if (stream_) {
        Pa_AbortStream(stream_);
    }
}

void PortAudioOutput::Close() {
    Stop();
    if (stream_) {
        Pa_CloseStream(stream_);
        stream_ = nullptr;
    }
    ReleasePa();
    scratch_.clear();
}

void PortAudioOutput::Drain() {
    if (!stream_ || stopped_.load(std::memory_order_acquire)) return;
    // Blocking write stream: nothing queued beyond Pa_WriteStream completion.
    // Small sleep not needed — engine Drain waits on free buffers on Windows;
    // here WriteBlock already blocks until consumed.
}

bool PortAudioOutput::WriteBlock(const int16_t* pcm, uint32_t frames) {
    if (!stream_ || !pcm || frames == 0) return false;
    if (stopped_.load(std::memory_order_acquire)) return false;
    if (paused_.load(std::memory_order_acquire)) {
        // Drop while paused so the engine thread does not stall forever.
        return true;
    }

    const size_t samples = static_cast<size_t>(frames) * static_cast<size_t>(channels_);
    const float vl = volL_;
    const float vr = volR_;
    const bool needScale = (vl < 0.999f) || (vr < 0.999f) || (channels_ >= 2 && std::fabs(vl - vr) > 0.001f);

    const int16_t* src = pcm;
    if (needScale) {
        scratch_.resize(samples);
        for (uint32_t f = 0; f < frames; ++f) {
            for (int c = 0; c < channels_; ++c) {
                const float v = (c == 0) ? vl : ((c == 1) ? vr : vl);
                const float s = static_cast<float>(pcm[f * channels_ + c]) * v;
                const int clamped = static_cast<int>(std::lround(std::clamp(s, -32768.f, 32767.f)));
                scratch_[f * channels_ + c] = static_cast<int16_t>(clamped);
            }
        }
        src = scratch_.data();
    }

    // Ensure stream is running (Start may not have been called yet).
    if (Pa_IsStreamActive(stream_) != 1) {
        if (Pa_StartStream(stream_) != paNoError) return false;
    }

    const PaError err = Pa_WriteStream(stream_, src, frames);
    if (err == paOutputUnderflowed) return !stopped_.load(std::memory_order_acquire);
    if (err != paNoError) return false;
    return !stopped_.load(std::memory_order_acquire);
}

void PortAudioOutput::SetMasterVolume(int left, int right) {
    volL_ = std::clamp(left, 0, 100) / 100.f;
    volR_ = std::clamp(right, 0, 100) / 100.f;
}

int PortAudioOutput::EnumerateDevices() const {
    // Enumeration without owning a stream: temporary Pa_Initialize.
    const bool owned = paOwned_;
    if (!owned && Pa_Initialize() != paNoError) return 0;
    const int n = CountOutputDevices();
    if (!owned) Pa_Terminate();
    return n;
}

bool PortAudioOutput::GetDeviceInfo(int id, WaveDeviceInfo& out) const {
    const bool owned = paOwned_;
    if (!owned && Pa_Initialize() != paNoError) return false;

    const int paDev = MapLogicalToPaDevice(id);
    bool ok = false;
    if (paDev != paNoDevice) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(paDev);
        if (info) {
            out.id       = id;
            out.channels = info->maxOutputChannels;
            out.formats  = 0xFFFFFFFFu;
            std::snprintf(out.name, sizeof(out.name), "%s",
                          info->name ? info->name : "PortAudio Output");
            ok = true;
        }
    }
    if (!owned) Pa_Terminate();
    return ok;
}

bool PortAudioOutput::SetDevice(int id) {
    deviceId_ = id;
    return true;
}

bool PortAudioOutput::SetOutputFile(const wchar_t*) {
    return false;
}

} // namespace libZFork

#endif // LIBZFORK_WITH_PORTAUDIO && !_WIN32
