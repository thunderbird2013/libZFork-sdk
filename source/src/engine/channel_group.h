#pragma once
#include "engine_defs.h"
#include <atomic>
#include <array>
#include <vector>
#include <cstdint>
#include <cstring>

namespace libZFork::engine {

class IEffect; // defined in effect_chain.h (Phase 3)

// A named mix bus with volume control, mute, and an effect chain.
//
// Threading contract:
//   volume / muted — atomic; API thread may write, engine thread reads
//   effectSlots_   — atomic IEffect*; ownership transfer via Commands
//   busBuf         — engine thread only (mix scratch buffer)
//   name           — written once at RegisterGroup time, then read-only
class ChannelGroup {
public:
    std::atomic<float> volume { 1.0f };
    std::atomic<bool>  muted  { false };
    char               name[32]{};

    // Effect slots — swapped atomically via SetGroupEffect command.
    // Engine thread reads; ownership is managed via Commands.
    std::array<std::atomic<IEffect*>, kMaxEffectSlots> effectSlots_{};

    // Mix bus buffer — zeroed at the start of each Render cycle.
    // Size: kMixFrames * channels * sizeof(float)
    std::vector<float> busBuf;

    // Post-fader peak meters (0..1), updated on the engine thread.
    std::atomic<float> meterL { 0.f };
    std::atomic<float> meterR { 0.f };

    // Initialise the bus buffer.
    void Init(uint32_t mixFrames, uint32_t channels);

    // Called by the engine thread: apply effect chain and volume to busBuf in-place.
    void ApplyBusEffects(uint32_t frames, uint32_t channels, uint32_t sampleRate);
};

} // namespace libZFork::engine
