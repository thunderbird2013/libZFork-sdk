#pragma once

#if defined(LIBZFORK_WITH_PORTAUDIO) && !defined(_WIN32)

#include "audio_output.h"
#include <atomic>
#include <cstdint>
#include <vector>

typedef void PaStream;

namespace libZFork {

// Blocking PortAudio push-output for Linux (and other non-Windows hosts).
// Mirrors WaveOutOutput's WriteBlock contract: engine thread pushes int16 PCM.
class PortAudioOutput final : public IAudioOutput {
public:
    PortAudioOutput();
    ~PortAudioOutput() override;

    bool Init(int sampleRate, int channels, int bitsPerSample, uint32_t bufferMs) override;
    bool Start() override;
    void Pause()  override;
    void Resume() override;
    void Stop()   override;
    void Close()  override;
    void Drain()  override;

    bool WriteBlock(const int16_t* pcm, uint32_t frames) override;
    void SetMasterVolume(int left, int right) override;

    int  EnumerateDevices() const override;
    bool GetDeviceInfo(int id, WaveDeviceInfo& out) const override;
    bool SetDevice(int id) override;
    bool SetOutputFile(const wchar_t* path) override;

private:
    bool EnsurePaInitialized();
    void ReleasePa();

    PaStream*        stream_      = nullptr;
    bool             paOwned_     = false;
    int              sampleRate_  = 44100;
    int              channels_    = 2;
    int              bits_        = 16;
    int              deviceId_    = -1; // -1 = default output
    std::atomic<bool> stopped_{ true };
    std::atomic<bool> paused_{ false };
    float            volL_        = 1.f;
    float            volR_        = 1.f;
    std::vector<int16_t> scratch_;
};

} // namespace libZFork

#endif // LIBZFORK_WITH_PORTAUDIO && !_WIN32
