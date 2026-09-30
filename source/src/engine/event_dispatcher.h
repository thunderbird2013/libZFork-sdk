#pragma once
#include "engine_defs.h"
#include <atomic>
#include <array>
#include <cstdint>

namespace libZFork::engine {

enum class EventType : uint8_t {
    VoiceStart,
    VoiceFinished,
    VoiceLoop,
    PositionCrossed,
};

struct AudioEvent {
    EventType type    = EventType::VoiceFinished;
    uint8_t   _pad[3]{};
    uint32_t  voiceId = 0;
    uint32_t  param1  = 0;  // e.g. loops remaining
    float     param2  = 0;  // e.g. position in seconds
};
static_assert(sizeof(AudioEvent) == 16);

// Callback signature used by zfork2_Update (Phase 5).
// Defined here so EventDispatcher can reference it without including api2.h.
using EventCallback = void(*)(int eventType, uint32_t voiceId,
                              uint32_t param1, float param2, void* userData);

// SPSC lock-free ring buffer — audio/engine thread pushes, control thread drains.
//
// Threading contract:
//   Push — engine thread only (single producer)
//   DrainAndDispatch — control thread only (zfork2_Update, single consumer)
//
// writePos_ and readPos_ are on separate cache lines to prevent false sharing.
template<uint32_t Capacity = 256>
class EventDispatcher {
    static_assert((Capacity & (Capacity - 1)) == 0);
    static constexpr uint32_t kMask = Capacity - 1;

public:
    // Engine thread — non-blocking; silently drops if full.
    void Push(const AudioEvent& e) noexcept {
        uint64_t wp = writePos_.load(std::memory_order_relaxed);
        uint64_t rp = readPos_.load(std::memory_order_acquire);
        if (wp - rp >= Capacity) return; // full — drop
        ring_[wp & kMask] = e;
        writePos_.store(wp + 1, std::memory_order_release);
    }

    // Control thread — calls cb for each pending event, then advances readPos_.
    void DrainAndDispatch(EventCallback cb, void* userData) noexcept {
        uint64_t rp = readPos_.load(std::memory_order_relaxed);
        uint64_t wp = writePos_.load(std::memory_order_acquire);
        while (rp != wp) {
            const AudioEvent& e = ring_[rp & kMask];
            if (cb) cb(static_cast<int>(e.type), e.voiceId, e.param1, e.param2, userData);
            ++rp;
        }
        readPos_.store(rp, std::memory_order_release);
    }

    uint32_t Pending() const noexcept {
        uint64_t wp = writePos_.load(std::memory_order_acquire);
        uint64_t rp = readPos_.load(std::memory_order_acquire);
        return static_cast<uint32_t>(wp - rp);
    }

private:
    std::array<AudioEvent, Capacity> ring_{};
    alignas(64) std::atomic<uint64_t> writePos_{ 0 };
    alignas(64) std::atomic<uint64_t> readPos_ { 0 };
};

} // namespace libZFork::engine
