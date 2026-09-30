#pragma once
#include "effects/equalizer.h"
#include "effects/echo.h"
#include "effects/reverb.h"
#include <span>
#include <cstdint>

namespace libZFork::engine {

// Fixed slot assignment for the two group-bus effects installable via the
// public API (zfork2_SetGroupEcho/Reverb) and via SoundBankV1 @echo/@reverb
// directives. Numeric values matter — they mirror the public ZF2_EFFECT_*
// macros in libzfork2.h so both call paths land in the same slot.
inline constexpr uint32_t kEchoEffectSlot   = 0;
inline constexpr uint32_t kReverbEffectSlot = 1;

// Engine-side effect interface.
// Engine thread has exclusive ownership of IEffect* instances.
// The forward declaration in channel_group.h and voice.h refers to this class.
class IEffect {
public:
    virtual ~IEffect() = default;

    // Apply effect to interleaved float PCM in-place.
    virtual void Process(std::span<float> pcm, uint32_t channels, uint32_t sampleRate) = 0;

    // Reset internal delay lines / filter state.
    virtual void Reset() = 0;
};

// Wraps libZFork::Equalizer as an IEffect.
class EqualizerEffect final : public IEffect {
public:
    EqualizerEffect() = default;

    // band in [0, Equalizer::kBands), gainDb in [-24, +24].
    void SetBandGain(int band, float gainDb) { eq_.SetBandGain(band, gainDb); }

    void Process(std::span<float> pcm, uint32_t channels, uint32_t sampleRate) override {
        eq_.Process(pcm, static_cast<int>(channels), static_cast<int>(sampleRate));
    }
    void Reset() override { eq_.Reset(); }

private:
    libZFork::Equalizer eq_;
};

// Wraps libZFork::EchoEffect as an IEffect.
class EchoEffectWrapper final : public IEffect {
public:
    EchoEffectWrapper() = default;

    void SetParams(const libZFork::EchoParams& p) { echo_.SetParams(p); }
    libZFork::EchoParams GetParams() const        { return echo_.GetParams(); }

    void Process(std::span<float> pcm, uint32_t channels, uint32_t sampleRate) override {
        echo_.Process(pcm, static_cast<int>(channels), static_cast<int>(sampleRate));
    }
    void Reset() override { echo_.Reset(); }

private:
    libZFork::EchoEffect echo_;
};

// Wraps libZFork::ReverbEffect as an IEffect.
class ReverbEffectWrapper final : public IEffect {
public:
    ReverbEffectWrapper() = default;

    void SetParams(const libZFork::ReverbParams& p) { reverb_.SetParams(p); }
    libZFork::ReverbParams GetParams() const        { return reverb_.GetParams(); }

    void Process(std::span<float> pcm, uint32_t channels, uint32_t sampleRate) override {
        reverb_.Process(pcm, static_cast<int>(channels), static_cast<int>(sampleRate));
    }
    void Reset() override { reverb_.Reset(); }

private:
    libZFork::ReverbEffect reverb_;
};

} // namespace libZFork::engine
