#pragma once
#include "audio_output.h"
#include <atomic>

namespace libZFork {

// Discards all audio output. Used for unit tests and Linux builds in Phase 1.
class NullOutput final : public IAudioOutput {
public:
    bool Init(int, int channels, int bitsPerSample, uint32_t) override {
        blockAlign_ = static_cast<uint32_t>(channels * bitsPerSample / 8);
        return true;
    }

    bool Start() override { stopped_ = false; return true; }

    void Pause()  override {}
    void Resume() override {}

    void Stop() override {
        stopped_.store(true, std::memory_order_release);
    }

    void Close() override {}
    void Drain() override {}

    bool WriteBlock(const int16_t*, uint32_t) override {
        return !stopped_.load(std::memory_order_acquire);
    }

    void SetMasterVolume(int, int) override {}

    int  EnumerateDevices() const override { return 1; }
    bool GetDeviceInfo(int id, WaveDeviceInfo& out) const override {
        out.id = id;
        out.channels = 2;
        out.formats  = 0xFFFFFFFF;
        std::snprintf(out.name, sizeof(out.name), "Null Output");
        return true;
    }
    bool SetDevice(int) override { return true; }
    bool SetOutputFile(const wchar_t*) override { return false; }

private:
    std::atomic<bool> stopped_{ false };
    uint32_t          blockAlign_{ 4 };
};

} // namespace libZFork
