#pragma once
#include <soundtouch/SoundTouch.h>
#include <span>
#include <vector>
#include <cstdint>
#include <atomic>

namespace libZFork {

// Streaming wrapper around SoundTouch for real-time tempo/pitch processing.
class SoundTouchProc {
public:
    SoundTouchProc();

    void Configure(int sampleRate, int channels);

    bool SetRate(float rate);       // 0.5 – 2.0 (1.0 = normal)
    bool SetPitch(float semitones); // negative = lower
    bool SetTempo(float tempo);     // 0.5 – 2.0

    // Legacy in-place path (used by libZFork Player).
    void Process(std::span<float> pcm, int channels);

    // Streaming API (used by zfork2 voices).
    void PutSamples(const float* data, uint32_t numFrames, int channels);
    uint32_t Receive(float* dest, uint32_t maxFrames, int channels);
    [[nodiscard]] uint32_t AvailableFrames() const;
    void Clear();

    [[nodiscard]] bool IsActive() const noexcept;

private:
    void DrainSoundTouch();

    soundtouch::SoundTouch st_;
    int                    channels_   = 2;
    float                  rate_       = 1.0f;
    float                  semitones_  = 0.0f;
    float                  tempo_      = 1.0f;
    std::vector<float>     pending_;  // interleaved output waiting to be received
    std::vector<float>     pullBuf_;

    // Wenn Rate/Pitch/Tempo geändert wird, setzen wir diesen Flag.
    // Process() erkennt ihn und leert SoundTouchs internen Buffer,
    // bevor neue Samples eingespeist werden → verhindert Klick-Artefakte.
    std::atomic<bool>      clearPending_{false};
};

} // namespace libZFork
