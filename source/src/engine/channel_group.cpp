#include "engine/channel_group.h"
#include "engine/effect_chain.h"
#include <algorithm>

namespace libZFork::engine {

void ChannelGroup::Init(uint32_t mixFrames, uint32_t channels) {
    busBuf.assign(mixFrames * channels, 0.f);
    for (auto& slot : effectSlots_)
        slot.store(nullptr, std::memory_order_relaxed);
}

void ChannelGroup::ApplyBusEffects(uint32_t frames, uint32_t channels, uint32_t sampleRate) {
    std::span<float> pcm(busBuf.data(), frames * channels);
    for (auto& slot : effectSlots_) {
        IEffect* fx = slot.load(std::memory_order_acquire);
        if (fx) fx->Process(pcm, channels, sampleRate);
    }
}

} // namespace libZFork::engine
