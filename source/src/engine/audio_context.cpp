#include "engine/audio_context.h"
#include "engine/sound.h"
#include "engine/effect_chain.h"
#include <algorithm>
#include <cassert>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <avrt.h>
#  pragma comment(lib, "avrt.lib")
#endif

namespace libZFork::engine {

AudioContext::AudioContext(std::unique_ptr<::libZFork::IAudioOutput> output,
                           uint32_t sampleRate,
                           uint32_t channels)
    : sampleRate_(sampleRate)
    , channels_(channels)
    , output_(std::move(output))
    , pool_(kMixFrames, channels, sampleRate)
    , graph_(sampleRate, channels)
{}

AudioContext::~AudioContext() {
    Stop();
}

uint32_t AudioContext::RegisterGroup(std::string_view name) {
    return graph_.RegisterGroup(name);
}

uint32_t AudioContext::FindGroup(std::string_view name) const {
    return graph_.FindGroup(name);
}

bool AudioContext::Start() {
    if (!output_) return false;
    if (!output_->Init(static_cast<int>(sampleRate_),
                       static_cast<int>(channels_),
                       16, 80))
        return false;
    if (!output_->Start()) return false;

    engineThread_ = std::jthread([this](std::stop_token tok) {
        EngineLoop(std::move(tok));
    });
    return true;
}

void AudioContext::Stop() {
    engineThread_.request_stop();
    if (output_) output_->Stop();
    if (engineThread_.joinable()) engineThread_.join();
    if (output_) output_->Close();
}

bool AudioContext::Post(const Command& cmd) noexcept {
    return cmdQueue_.TryPush(cmd);
}

uint32_t AudioContext::AllocVoice(int32_t priority) noexcept {
    return pool_.TryPreAcquire(priority);
}

void AudioContext::SetDefaultCallback(EventCallback cb, void* userData) noexcept {
    defaultCb_     = cb;
    defaultCbData_ = userData;
}

void AudioContext::Update() noexcept {
    events_.DrainAndDispatch(defaultCb_, defaultCbData_);
}

void AudioContext::Update(EventCallback cb, void* userData) noexcept {
    events_.DrainAndDispatch(cb, userData);
}

bool AudioContext::IsVoiceActive(uint32_t voiceId) const noexcept {
    return pool_.IsValid(voiceId);
}

void AudioContext::GetGroupLevels(uint32_t groupId,
                                  float& outLeft, float& outRight) const noexcept {
    if (groupId >= graph_.GroupCount()) {
        outLeft = outRight = 0.f;
        return;
    }
    const ChannelGroup& grp = graph_.GetGroup(groupId);
    outLeft  = grp.meterL.load(std::memory_order_relaxed);
    outRight = grp.meterR.load(std::memory_order_relaxed);
}

void AudioContext::CancelAllocatedVoice(uint32_t voiceId) noexcept {
    pool_.CancelAllocated(voiceId);
}

void AudioContext::SetGroupEffect(uint32_t groupId, uint32_t slot, IEffect* effect) noexcept {
    Command c{};
    c.type       = CommandType::SetGroupEffect;
    c.groupId    = groupId;
    c.param.i[0] = static_cast<int32_t>(slot);
    c.ptr        = effect;
    if (!Post(c)) delete effect; // queue full — drop rather than leak
}

void AudioContext::ClearGroupEffect(uint32_t groupId, uint32_t slot) noexcept {
    Command c{};
    c.type       = CommandType::ClearGroupEffect;
    c.groupId    = groupId;
    c.param.i[0] = static_cast<int32_t>(slot);
    (void)Post(c);
}

// ---------------------------------------------------------------------------
// Engine thread
// ---------------------------------------------------------------------------

void AudioContext::EngineLoop(std::stop_token tok) {
#ifdef _WIN32
    // Reduce scheduling jitter for this thread: with the smaller kMixFrames
    // block size (see engine_defs.h) a delayed wakeup is more likely to
    // starve the output before the next WriteBlock() lands. MMCSS ("Pro
    // Audio") is what real audio apps use for this; THREAD_PRIORITY_HIGHEST
    // is a lightweight fallback if MMCSS isn't available.
    DWORD mmcssTaskIndex = 0;
    HANDLE mmcssHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmcssTaskIndex);
    if (!mmcssHandle)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif

    while (!tok.stop_requested()) {
        ProcessCommands();

        const int16_t* pcm = graph_.Render(pool_, kMixFrames);

        HandleDoneVoices();

        if (!output_->WriteBlock(pcm, kMixFrames)) break; // output closed
    }

#ifdef _WIN32
    if (mmcssHandle) AvRevertMmThreadCharacteristics(mmcssHandle);
#endif
}

void AudioContext::ProcessCommands() {
    static Command buf[256];
    uint32_t n = cmdQueue_.DrainAll(buf, 256);

    for (uint32_t i = 0; i < n; ++i) {
        const Command& c = buf[i];
        switch (c.type) {

        case CommandType::PlayVoice: {
            // cmd.ptr may carry a heap-allocated shared_ptr<Sound> (nullable).
            // We take ownership here and delete the heap allocation.
            std::shared_ptr<Sound> snd;
            if (c.ptr) {
                auto* p = static_cast<std::shared_ptr<Sound>*>(c.ptr);
                snd = std::move(*p);
                delete p;
            }

            // If voiceId is pre-allocated (by API thread via AllocVoice), use it directly.
            // Otherwise fall back to Acquire on the engine thread (legacy/test path).
            Voice* v = nullptr;
            if (c.voiceId != kInvalidVoice) {
                v = pool_.Get(c.voiceId);
            } else {
                uint32_t id = pool_.Acquire(c.param.i[0] /*priority*/);
                if (id != kInvalidVoice) v = pool_.Get(id);
            }
            if (!v) break;
            v->groupIndex = c.groupId < graph_.GroupCount() ? c.groupId : 0;
            v->sound_      = std::move(snd);
            v->readCursor_ = 0;
            if (v->sound_ && v->sound_->Type() == SoundType::Streaming)
                v->decoder_ = v->sound_->CreateDecoder();
            v->state.store(VoiceState::Playing, std::memory_order_release);
            events_.Push({ EventType::VoiceStart, {}, v->id, 0, 0.f });
            break;
        }

        case CommandType::StopVoice: {
            Voice* v = pool_.Get(c.voiceId);
            if (v) {
                events_.Push({ EventType::VoiceFinished, {}, v->id, 0, 0.f });
                pool_.Release(c.voiceId);
            }
            break;
        }

        case CommandType::PauseVoice: {
            Voice* v = pool_.Get(c.voiceId);
            if (v && v->state.load(std::memory_order_relaxed) == VoiceState::Playing)
                v->state.store(VoiceState::Paused, std::memory_order_release);
            break;
        }

        case CommandType::ResumeVoice: {
            Voice* v = pool_.Get(c.voiceId);
            if (v && v->state.load(std::memory_order_relaxed) == VoiceState::Paused)
                v->state.store(VoiceState::Playing, std::memory_order_release);
            break;
        }

        case CommandType::SetVoiceVolume: {
            Voice* v = pool_.Get(c.voiceId);
            if (v) v->params.volume.store(c.param.f[0], std::memory_order_relaxed);
            break;
        }

        case CommandType::SetVoicePan: {
            Voice* v = pool_.Get(c.voiceId);
            if (v) v->params.pan.store(c.param.f[0], std::memory_order_relaxed);
            break;
        }

        case CommandType::SetVoicePitch: {
            Voice* v = pool_.Get(c.voiceId);
            if (v) v->params.pitch.store(c.param.f[0], std::memory_order_relaxed);
            break;
        }

        case CommandType::SetVoiceTempo: {
            Voice* v = pool_.Get(c.voiceId);
            if (v) v->params.tempo.store(c.param.f[0], std::memory_order_relaxed);
            break;
        }

        case CommandType::SetVoiceLoop: {
            Voice* v = pool_.Get(c.voiceId);
            if (v) v->params.loopCount.store(c.param.i[0], std::memory_order_relaxed);
            break;
        }

        case CommandType::SetVoicePriority: {
            Voice* v = pool_.Get(c.voiceId);
            if (v) v->params.priority.store(c.param.i[0], std::memory_order_relaxed);
            break;
        }

        case CommandType::SetVoicePosition2D: {
            Voice* v = pool_.Get(c.voiceId);
            if (v) {
                v->params.posX      .store(c.param.f[0], std::memory_order_relaxed);
                v->params.posY      .store(c.param.f[1], std::memory_order_relaxed);
                v->params.positional.store(true,          std::memory_order_relaxed);
            }
            break;
        }

        case CommandType::SetVoicePosition3D: {
            Voice* v = pool_.Get(c.voiceId);
            if (v) {
                v->params.posX      .store(c.param.f[0], std::memory_order_relaxed);
                v->params.posY      .store(c.param.f[1], std::memory_order_relaxed);
                v->params.posZ      .store(c.param.f[2], std::memory_order_relaxed);
                v->params.positional.store(true,          std::memory_order_relaxed);
            }
            break;
        }

        case CommandType::SetGroupVolume: {
            if (c.groupId < graph_.GroupCount())
                graph_.GetGroup(c.groupId).volume.store(c.param.f[0],
                                                        std::memory_order_relaxed);
            break;
        }

        case CommandType::SetGroupMute: {
            if (c.groupId < graph_.GroupCount())
                graph_.GetGroup(c.groupId).muted.store(c.param.i[0] != 0,
                                                       std::memory_order_relaxed);
            break;
        }

        case CommandType::SetListenerPosition2D:
            graph_.SetListenerPosition(c.param.f[0], c.param.f[1]);
            break;

        case CommandType::SetListenerPosition3D:
            graph_.SetListenerPosition(c.param.f[0], c.param.f[1], c.param.f[2]);
            break;

        case CommandType::SetListenerDirection:
            graph_.SetListenerDirection(c.param.f[0], c.param.f[1], c.param.f[2]);
            break;

        case CommandType::SetListenerRange:
            graph_.SetListenerRange(c.param.f[0], c.param.f[1], c.param.f[2]);
            break;

        case CommandType::SetGroupEffect: {
            if (c.groupId >= graph_.GroupCount()) break;
            uint32_t slot = static_cast<uint32_t>(c.param.i[0]);
            if (slot >= kMaxEffectSlots) break;
            auto* newFx = static_cast<IEffect*>(c.ptr);
            // Atomically swap in the new effect and delete the old one.
            IEffect* oldFx = graph_.GetGroup(c.groupId)
                                   .effectSlots_[slot]
                                   .exchange(newFx, std::memory_order_acq_rel);
            delete oldFx;
            break;
        }

        case CommandType::ClearGroupEffect: {
            if (c.groupId >= graph_.GroupCount()) break;
            uint32_t slot = static_cast<uint32_t>(c.param.i[0]);
            if (slot >= kMaxEffectSlots) break;
            IEffect* oldFx = graph_.GetGroup(c.groupId)
                                   .effectSlots_[slot]
                                   .exchange(nullptr, std::memory_order_acq_rel);
            delete oldFx;
            break;
        }

        case CommandType::SetVoiceEffect: {
            Voice* v = pool_.Get(c.voiceId);
            uint32_t slot = static_cast<uint32_t>(c.param.i[0]);
            if (v && slot < kMaxEffectSlots)
                v->voiceEffects_[slot].reset(static_cast<IEffect*>(c.ptr));
            else
                delete static_cast<IEffect*>(c.ptr); // discard if voice gone
            break;
        }

        case CommandType::ClearVoiceEffect: {
            Voice* v = pool_.Get(c.voiceId);
            uint32_t slot = static_cast<uint32_t>(c.param.i[0]);
            if (v && slot < kMaxEffectSlots)
                v->voiceEffects_[slot].reset();
            break;
        }

        case CommandType::SetVoiceMarker: {
            Voice* v = pool_.Get(c.voiceId);
            if (!v || v->markerCount_ >= kMaxMarkers) break;
            uint32_t markerId  = static_cast<uint32_t>(c.param.i[0]);
            float    posSec    = c.param.f[1];
            int      soundSR   = v->sound_ ? v->sound_->SampleRate() : static_cast<int>(sampleRate_);
            uint32_t framePos  = static_cast<uint32_t>(posSec * soundSR);
            uint32_t idx       = v->markerCount_++;
            v->markers_[idx]   = { framePos, markerId, false };
            break;
        }

        case CommandType::ClearVoiceMarkers: {
            Voice* v = pool_.Get(c.voiceId);
            if (v) { v->markerCount_ = 0; v->firedMarkerMask_ = 0; }
            break;
        }

        case CommandType::NoOp:
        default:
            break;
        }
    }
}

void AudioContext::HandleDoneVoices() {
    // Collect IDs first to avoid mutating pool while iterating.
    uint32_t doneIds[kMaxVoices];
    uint32_t doneCount = 0;

    pool_.ForEachActive([&](Voice& v) {
        // Dispatch any marker events fired this render cycle.
        if (v.firedMarkerMask_ != 0) {
            for (uint32_t m = 0; m < v.markerCount_; ++m) {
                if (v.firedMarkerMask_ & (1u << m)) {
                    float posSec = v.sound_
                        ? static_cast<float>(v.markers_[m].framePos) / v.sound_->SampleRate()
                        : 0.f;
                    events_.Push({ EventType::PositionCrossed, {}, v.id,
                                   v.markers_[m].markerId, posSec });
                }
            }
            v.firedMarkerMask_ = 0;
        }

        VoiceState s = v.state.load(std::memory_order_acquire);
        if (s == VoiceState::Done || s == VoiceState::Stopping) {
            if (doneCount < kMaxVoices)
                doneIds[doneCount++] = v.id;
        }
    });

    for (uint32_t k = 0; k < doneCount; ++k) {
        events_.Push({ EventType::VoiceFinished, {}, doneIds[k], 0, 0.f });
        pool_.Release(doneIds[k]);
    }
}

} // namespace libZFork::engine
