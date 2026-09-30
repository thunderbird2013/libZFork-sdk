#pragma once
#include "engine_defs.h"
#include "channel_group.h"
#include "voice_pool.h"
#include "engine/spatial.h"
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace libZFork::engine {

// The mix graph owns all ChannelGroups and drives the render cycle.
//
// Threading contract — all methods except RegisterGroup/FindGroup must be
// called from the engine thread.  RegisterGroup/FindGroup are safe to call
// before Start() (single-threaded setup phase).
class MixerGraph {
public:
    MixerGraph(uint32_t sampleRate, uint32_t channels);

    // Setup (before AudioContext::Start): register a named group.
    // Returns the group index or kInvalidGroup if the limit is reached.
    [[nodiscard]] uint32_t RegisterGroup(std::string_view name);

    // Lookup by name — O(1). Safe after construction (map is read-only at runtime).
    [[nodiscard]] uint32_t FindGroup(std::string_view name) const;

    // Engine thread: reference to a group by index.
    ChannelGroup& GetGroup(uint32_t idx) { return groups_[idx]; }
    [[nodiscard]] const ChannelGroup& GetGroup(uint32_t idx) const { return groups_[idx]; }

    // Engine thread: set listener state for spatial audio.
    void SetListenerPosition(float x, float y, float z = 0.f) noexcept {
        listener_.x = x; listener_.y = y; listener_.z = z;
    }
    void SetListenerDirection(float dx, float dy, float dz = 0.f) noexcept {
        listener_.dirX = dx; listener_.dirY = dy; listener_.dirZ = dz;
    }
    void SetListenerRange(float minDist, float maxDist, float rolloff = 1.f) noexcept {
        listener_.minDist = minDist; listener_.maxDist = maxDist; listener_.rolloff = rolloff;
    }
    [[nodiscard]] const ListenerState& GetListenerState() const noexcept { return listener_; }

    // Engine thread: run one full mix cycle.
    //   1. Zero all group buses and master buffer.
    //   2. For each active voice: RenderChunk → accumulate into its group bus.
    //   3. Apply group effects + volume → sum into master.
    //   4. Convert float master → int16.
    // Returns pointer to the int16 master buffer (valid until the next Render call).
    [[nodiscard]] const int16_t* Render(VoicePool& pool, uint32_t frames);

    [[nodiscard]] uint32_t GroupCount()  const noexcept { return groupCount_; }
    [[nodiscard]] uint32_t SampleRate()  const noexcept { return sampleRate_; }
    [[nodiscard]] uint32_t Channels()    const noexcept { return channels_; }

private:
    uint32_t sampleRate_;
    uint32_t channels_;

    std::array<ChannelGroup, kMaxGroups> groups_;
    uint32_t                              groupCount_ = 0;
    std::unordered_map<std::string, uint32_t> nameMap_;

    // Listener state for spatial audio.
    ListenerState listener_{};

    // Output buffers — owned by MixerGraph, stable across one render cycle.
    std::vector<float>   masterBuf_;  // interleaved float
    std::vector<int16_t> masterPCM_;  // interleaved int16 for IAudioOutput
};

} // namespace libZFork::engine
