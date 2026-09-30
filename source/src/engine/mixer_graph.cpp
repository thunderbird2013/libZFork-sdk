#include "engine/mixer_graph.h"
#include "engine/spatial.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace libZFork::engine {

static constexpr float kPi = std::numbers::pi_v<float>;

MixerGraph::MixerGraph(uint32_t sampleRate, uint32_t channels)
    : sampleRate_(sampleRate), channels_(channels)
{
    masterBuf_.resize(kMixFrames * channels, 0.f);
    masterPCM_.resize(kMixFrames * channels, 0);
    for (auto& g : groups_)
        g.Init(kMixFrames, channels);
}

uint32_t MixerGraph::RegisterGroup(std::string_view name) {
    if (groupCount_ >= kMaxGroups) return kInvalidGroup;
    uint32_t idx = groupCount_++;
    std::memcpy(groups_[idx].name, name.data(),
                std::min(name.size(), sizeof(ChannelGroup::name) - 1));
    nameMap_.emplace(std::string(name), idx);
    return idx;
}

uint32_t MixerGraph::FindGroup(std::string_view name) const {
    auto it = nameMap_.find(std::string(name));
    return (it != nameMap_.end()) ? it->second : kInvalidGroup;
}

// Constant-power pan: pan in [-1, +1].
// angle maps 0..pi/2 → gainL=1..0, gainR=0..1.
static void PanToGain(float volume, float pan, float& gainL, float& gainR) noexcept {
    float angle = (pan + 1.0f) * (kPi / 4.0f);
    gainL = volume * std::cos(angle);
    gainR = volume * std::sin(angle);
}

const int16_t* MixerGraph::Render(VoicePool& pool, uint32_t frames) {
    const uint32_t samples = frames * channels_;

    // Ensure buffers are large enough (no-op if already sized).
    if (masterBuf_.size() < samples) masterBuf_.resize(samples);
    if (masterPCM_.size() < samples) masterPCM_.resize(samples);

    // 1. Zero master and all registered group buses.
    std::fill(masterBuf_.begin(), masterBuf_.begin() + samples, 0.f);
    for (uint32_t g = 0; g < groupCount_; ++g)
        std::fill(groups_[g].busBuf.begin(),
                  groups_[g].busBuf.begin() + samples, 0.f);

    // 2. Render each active voice and accumulate into its group bus.
    pool.ForEachActive([&](Voice& v) {
        if (v.state.load(std::memory_order_acquire) != VoiceState::Playing)
            return;

        uint32_t produced = v.RenderChunk(frames);
        if (produced == 0) {
            // Voice finished — mark Done for cleanup in the next command cycle.
            v.state.store(VoiceState::Done, std::memory_order_release);
            return;
        }

        uint32_t gIdx = v.groupIndex;
        if (gIdx >= groupCount_) gIdx = 0;
        auto& bus = groups_[gIdx].busBuf;

        float gainL, gainR;
        float vol = v.params.volume.load(std::memory_order_relaxed);
        if (v.params.positional.load(std::memory_order_relaxed)) {
            float px = v.params.posX.load(std::memory_order_relaxed);
            float py = v.params.posY.load(std::memory_order_relaxed);
            float pz = v.params.posZ.load(std::memory_order_relaxed);
            SpatialResult sr = (pz == 0.f)
                ? Compute2D(px, py, listener_)
                : Compute3D(px, py, pz, listener_);
            PanToGain(vol * sr.gain, sr.pan, gainL, gainR);
        } else {
            PanToGain(vol, v.params.pan.load(std::memory_order_relaxed), gainL, gainR);
        }

        const float* src = v.mixBuf.data();
        if (channels_ == 2) {
            for (uint32_t f = 0; f < produced; ++f) {
                bus[f * 2    ] += src[f * 2    ] * gainL;
                bus[f * 2 + 1] += src[f * 2 + 1] * gainR;
            }
        } else {
            float gain = (gainL + gainR) * 0.5f;
            for (uint32_t f = 0; f < produced; ++f)
                bus[f] += src[f] * gain;
        }
    });

    // 3. Per group: apply effects + volume → sum into masterBuf_.
    for (uint32_t g = 0; g < groupCount_; ++g) {
        ChannelGroup& grp = groups_[g];
        if (grp.muted.load(std::memory_order_relaxed)) {
            float dl = grp.meterL.load(std::memory_order_relaxed) * 0.85f;
            float dr = grp.meterR.load(std::memory_order_relaxed) * 0.85f;
            grp.meterL.store(dl, std::memory_order_relaxed);
            grp.meterR.store(dr, std::memory_order_relaxed);
            continue;
        }

        grp.ApplyBusEffects(frames, channels_, sampleRate_);

        float vol = grp.volume.load(std::memory_order_relaxed);
        float pkL = 0.f, pkR = 0.f;
        if (channels_ == 2) {
            for (uint32_t f = 0; f < frames; ++f) {
                pkL = std::max(pkL, std::abs(grp.busBuf[f * 2    ] * vol));
                pkR = std::max(pkR, std::abs(grp.busBuf[f * 2 + 1] * vol));
            }
        } else {
            for (uint32_t f = 0; f < frames; ++f) {
                float pk = std::abs(grp.busBuf[f] * vol);
                pkL = std::max(pkL, pk);
                pkR = std::max(pkR, pk);
            }
        }
        float curL = grp.meterL.load(std::memory_order_relaxed);
        float curR = grp.meterR.load(std::memory_order_relaxed);
        grp.meterL.store(curL * 0.85f + pkL * 0.15f, std::memory_order_relaxed);
        grp.meterR.store(curR * 0.85f + pkR * 0.15f, std::memory_order_relaxed);

        for (uint32_t s = 0; s < samples; ++s)
            masterBuf_[s] += grp.busBuf[s] * vol;
    }

    // 4. Clamp and convert float master → int16.
    for (uint32_t s = 0; s < samples; ++s) {
        float v = masterBuf_[s];
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        masterPCM_[s] = static_cast<int16_t>(v * 32767.f);
    }

    return masterPCM_.data();
}

} // namespace libZFork::engine
