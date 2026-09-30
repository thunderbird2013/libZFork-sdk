#pragma once
#ifdef _WIN32

#include "audio_output.h"
#include <atomic>
#include <array>
#include <string>
#include <windows.h>
#include <mmsystem.h>

namespace libZFork {

class WaveOutOutput final : public IAudioOutput {
public:
    WaveOutOutput();
    ~WaveOutOutput() override;

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
    // 64 frames * 2 ch * 2 bytes = 256 bytes — matches engine::kMixFrames
    // so one engine render block maps to exactly one waveOut block.
    // kBufferCount is the safety margin against underruns: too few buffers
    // and a single delayed engine iteration (scheduler hiccup, a slightly
    // slow decode) starves the device, producing an audible click/crackle.
    // 64 buffers of this (small) size keep total driver-side buffering at
    // ~93 ms @ 44.1 kHz — the *same* total as every previous step — so
    // shrinking kMixFrames only sharpens command-drain latency, it doesn't
    // reduce the underrun safety margin. This is the most aggressive step;
    // if it clicks, back off to the 128-frame/32-buffer configuration.
    static constexpr int      kBufferCount = 64;
    static constexpr uint32_t kBlockBytes  = 256; // bytes per waveOut block

    static void CALLBACK StaticCallback(HWAVEOUT hwo, UINT msg,
                                        DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR);

    struct Block {
        WAVEHDR                          hdr{};
        std::array<uint8_t, kBlockBytes> buf{};
        std::atomic<bool>                available { true };
        bool                             prepared  = false;
    };

    HWAVEOUT            hWaveOut_   = nullptr;
    WAVEFORMATEX        format_     {};
    std::array<Block, kBufferCount> blocks_;
    std::atomic<int>    freeBlocks_ { kBufferCount };
    HANDLE              hEvent_     = nullptr;
    std::atomic<bool>   stopped_    { true };

    int          deviceId_  = WAVE_MAPPER;
    std::wstring outputFile_;
};

} // namespace libZFork

#endif // _WIN32
