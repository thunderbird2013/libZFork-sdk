#pragma once
#ifdef _WIN32

#include "audio_output.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

namespace libZFork {

// Low-latency WASAPI output: exclusive mode, event-driven, using the
// device's minimum supported period. WinMM's waveOut (see WaveOutOutput)
// has an inherent latency floor around a few ms that no amount of buffer
// tuning gets under — this backend is the real fix, typically reaching
// sub-2ms device periods on hardware/drivers that support it.
//
// Exclusive mode can fail to initialise for reasons outside our control
// (another app already holds the device exclusively, the device doesn't
// support the requested format, virtual/remote audio devices, ...), so
// Init() is expected to fail sometimes — callers should fall back to
// WaveOutOutput in that case (see CreatePlatformAudioOutput()).
//
// Bridges the push-style IAudioOutput::WriteBlock() contract (caller
// supplies arbitrarily-sized blocks) to WASAPI's pull-style event/period
// model (the device wants exactly bufferFrames_ frames each time the
// event fires) via a small byte ring buffer.
class WasapiOutput final : public IAudioOutput {
public:
    WasapiOutput();
    ~WasapiOutput() override;

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

    // Actual device period achieved, in frames — for diagnostics.
    [[nodiscard]] uint32_t DevicePeriodFrames() const noexcept { return bufferFrames_; }

private:
    void RenderThreadLoop();
    bool InitializeClient(int sampleRate, int channels, int bitsPerSample);
    bool EnsureEnumerator() const;

    // mutable: EnumerateDevices/GetDeviceInfo sind const, brauchen aber COM-Enumerator.
    mutable Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator_;
    Microsoft::WRL::ComPtr<IMMDevice>           device_;
    Microsoft::WRL::ComPtr<IAudioClient>        client_;
    Microsoft::WRL::ComPtr<IAudioRenderClient>  renderClient_;

    bool   comInitialized_ = false;
    HANDLE hEvent_         = nullptr;

    uint32_t blockAlign_    = 4;
    uint32_t bufferFrames_  = 0; // device period, in frames
    int      selectedDeviceId_ = -1;

    std::thread             renderThread_;
    std::atomic<bool>       running_{ false };
    std::atomic<bool>       stopped_{ true };

    // Byte ring buffer bridging WriteBlock() (producer, any calling thread)
    // and the WASAPI render thread (consumer).
    std::vector<uint8_t>    ring_;
    size_t                  ringHead_ = 0; // next byte to read
    size_t                  ringTail_ = 0; // next byte to write
    size_t                  ringUsed_ = 0;
    mutable std::mutex      ringMutex_;
    std::condition_variable ringNotFull_;
    std::condition_variable ringNotEmpty_;
};

} // namespace libZFork

#endif // _WIN32
