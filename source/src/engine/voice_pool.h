#pragma once
#include "engine_defs.h"
#include "voice.h"
#include <array>
#include <cstdint>
#include <climits>
#include <concepts>
#include <mutex>

namespace libZFork::engine {

// Fixed-capacity pool of Voice objects.
//
// Threading contract:
//   All methods except IsValid() — engine thread only.
//   IsValid() — any thread (reads only the atomic Voice::state).
//
// Voice IDs are generation-stamped: upper 16 bits = generation counter,
// lower 16 bits = slot index. Stale handles from a previous cycle safely
// return nullptr from Get().
class VoicePool {
public:
    explicit VoicePool(uint32_t mixFrames  = kMixFrames,
                       uint32_t channels   = 2,
                       uint32_t sampleRate = 48000);

    // Engine thread: claim a free slot and return its stamped id.
    // If all slots are Playing, steals the lowest-priority voice.
    // Returns kInvalidVoice (0) only on catastrophic failure.
    [[nodiscard]] uint32_t Acquire(int32_t priority = 0);

    // API thread: claim a free slot without priority-stealing.
    // Thread-safe (protected by allocMutex_). Returns kInvalidVoice if pool is full.
    [[nodiscard]] uint32_t TryPreAcquire(int32_t priority = 0);

    // Engine thread: return a voice back to the free list.
    void Release(uint32_t voiceId);

    // Engine thread: resolve a stamped id to a mutable Voice pointer.
    // Returns nullptr if the id is stale or the slot is Free.
    [[nodiscard]] Voice* Get(uint32_t voiceId);

    // Engine thread: iterate every non-Free voice.
    template<std::invocable<Voice&> F>
    void ForEachActive(F&& fn) {
        for (auto& v : voices_)
            if (v.state.load(std::memory_order_relaxed) != VoiceState::Free)
                fn(v);
    }

    // Any thread: quick liveness check using only the atomic state field.
    [[nodiscard]] bool IsValid(uint32_t voiceId) const noexcept;

    [[nodiscard]] uint32_t ActiveCount() const noexcept;
    [[nodiscard]] uint32_t FreeCount()   const noexcept { return freeCount_; }

    // API thread: release a pre-allocated voice if playback never started.
    void CancelAllocated(uint32_t voiceId) noexcept;

private:
    [[nodiscard]] bool     ValidId(uint32_t id) const noexcept;
    [[nodiscard]] uint32_t FindLowestPriority() const noexcept;

    std::array<Voice,    kMaxVoices> voices_;
    std::array<uint16_t, kMaxVoices> generations_{};  // per-slot generation counter
    std::array<uint8_t,  kMaxVoices> freeList_{};     // indices of free slots
    uint32_t                         freeCount_ = kMaxVoices;
    mutable std::mutex               allocMutex_; // guards freeList_/generations_ for API thread
};

} // namespace libZFork::engine
