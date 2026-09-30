#pragma once
#ifdef _WIN32

#include "audio_output.h"
#include "wasapi_output.h"
#include "waveout_output.h"
#include <memory>

namespace libZFork {

// Picks the lowest-latency backend that actually works on this machine:
// tries WasapiOutput (exclusive, event-driven — the real fix for latency,
// see engine_defs.h) first, and falls back to WaveOutOutput if that fails
// to initialise (device already held exclusively by another app, format
// not supported in exclusive mode, virtual/remote audio device, ...).
// Everything after Init() just forwards to whichever backend won.
class AutoOutput final : public IAudioOutput {
public:
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

    // For diagnostics: which backend actually ended up active.
    [[nodiscard]] bool IsUsingWasapi() const noexcept { return active_ == wasapi_.get(); }
    [[nodiscard]] uint32_t ActivePeriodFrames() const noexcept {
        return IsUsingWasapi() ? wasapi_->DevicePeriodFrames() : 0;
    }

    // Must be called before Init() to take effect.
    void SetBackendPreference(AudioBackendPreference pref) noexcept { preference_ = pref; }

private:
    std::unique_ptr<WasapiOutput>  wasapi_ = std::make_unique<WasapiOutput>();
    std::unique_ptr<WaveOutOutput> waveOut_ = std::make_unique<WaveOutOutput>();
    IAudioOutput*                  active_ = nullptr; // points at wasapi_.get() or waveOut_.get()
    AudioBackendPreference         preference_ = AudioBackendPreference::Auto;
};

} // namespace libZFork

#endif // _WIN32
