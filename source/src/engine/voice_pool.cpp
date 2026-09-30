#include "engine/voice_pool.h"
#include <algorithm>
#include <cassert>
#include <climits>

namespace libZFork::engine {

VoicePool::VoicePool(uint32_t mixFrames, uint32_t channels, uint32_t sampleRate) {
    for (uint32_t i = 0; i < kMaxVoices; ++i) {
        freeList_[i]    = static_cast<uint8_t>(i);
        generations_[i] = 0;
        voices_[i].Init(mixFrames, channels, sampleRate);
    }
    freeCount_ = kMaxVoices;
}

bool VoicePool::ValidId(uint32_t id) const noexcept {
    if (id == kInvalidVoice) return false;
    uint8_t  slot = static_cast<uint8_t>(id & 0xFFFF);
    uint16_t gen  = static_cast<uint16_t>((id >> 16) & 0xFFFF);
    return slot < kMaxVoices && generations_[slot] == gen;
}

uint32_t VoicePool::FindLowestPriority() const noexcept {
    uint32_t bestSlot = kInvalidGroup; // reuse as "not found"
    int32_t  bestPri  = INT32_MAX;
    for (uint32_t i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].state.load(std::memory_order_relaxed) == VoiceState::Playing) {
            int32_t pri = voices_[i].params.priority.load(std::memory_order_relaxed);
            if (pri < bestPri) { bestPri = pri; bestSlot = i; }
        }
    }
    return bestSlot;
}

uint32_t VoicePool::Acquire(int32_t priority) {
    uint8_t slot;

    std::lock_guard<std::mutex> lock(allocMutex_);
    if (freeCount_ > 0) {
        slot = freeList_[--freeCount_];
    } else {
        // No free slots — steal the lowest-priority Playing voice.
        uint32_t victim = FindLowestPriority();
        if (victim == kInvalidGroup) return kInvalidVoice; // all voices busy, none stealable
        voices_[victim].Reset(); // sets state = Free
        slot = static_cast<uint8_t>(victim);
        // Do NOT push to freeList_ — we're immediately reusing it.
    }

    ++generations_[slot];
    if (generations_[slot] == 0) ++generations_[slot]; // skip 0 so id != kInvalidVoice

    uint32_t id = (static_cast<uint32_t>(generations_[slot]) << 16) | slot;
    voices_[slot].id = id;
    voices_[slot].state.store(VoiceState::Starting, std::memory_order_release);
    voices_[slot].params.priority.store(priority, std::memory_order_relaxed);
    return id;
}

void VoicePool::Release(uint32_t voiceId) {
    if (!ValidId(voiceId)) return;
    uint8_t slot = static_cast<uint8_t>(voiceId & 0xFFFF);
    voices_[slot].Reset(); // sets state = Free
    std::lock_guard<std::mutex> lock(allocMutex_);
    freeList_[freeCount_++] = slot;
}

uint32_t VoicePool::TryPreAcquire(int32_t priority) {
    std::lock_guard<std::mutex> lock(allocMutex_);
    if (freeCount_ == 0) return kInvalidVoice; // pool full — no stealing from API thread
    uint8_t slot = freeList_[--freeCount_];

    ++generations_[slot];
    if (generations_[slot] == 0) ++generations_[slot];

    uint32_t id = (static_cast<uint32_t>(generations_[slot]) << 16) | slot;
    voices_[slot].id = id;
    voices_[slot].state.store(VoiceState::Starting, std::memory_order_release);
    voices_[slot].params.priority.store(priority, std::memory_order_relaxed);
    return id;
}

Voice* VoicePool::Get(uint32_t voiceId) {
    if (!ValidId(voiceId)) return nullptr;
    uint8_t slot = static_cast<uint8_t>(voiceId & 0xFFFF);
    if (voices_[slot].state.load(std::memory_order_relaxed) == VoiceState::Free)
        return nullptr;
    return &voices_[slot];
}

bool VoicePool::IsValid(uint32_t voiceId) const noexcept {
    if (voiceId == kInvalidVoice) return false;
    uint8_t slot = static_cast<uint8_t>(voiceId & 0xFFFF);
    if (slot >= kMaxVoices) return false;
    return voices_[slot].state.load(std::memory_order_acquire) != VoiceState::Free;
}

uint32_t VoicePool::ActiveCount() const noexcept {
    uint32_t n = 0;
    for (const auto& v : voices_)
        if (v.state.load(std::memory_order_relaxed) != VoiceState::Free) ++n;
    return n;
}

void VoicePool::CancelAllocated(uint32_t voiceId) noexcept {
    if (!ValidId(voiceId)) return;
    const uint8_t slot = static_cast<uint8_t>(voiceId & 0xFFFF);
    if (voices_[slot].state.load(std::memory_order_acquire) != VoiceState::Starting)
        return;

    voices_[slot].Reset();
    std::lock_guard<std::mutex> lock(allocMutex_);
    freeList_[freeCount_++] = slot;
}

} // namespace libZFork::engine
