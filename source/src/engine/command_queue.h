#pragma once
#include "engine_defs.h"
#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#if defined(_MSC_VER)
#  include <immintrin.h>
#endif

namespace libZFork::engine {

namespace detail {

inline void SpinPause() noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _mm_pause();
#elif defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    __asm__ __volatile__("yield");
#elif defined(__arm__)
    __asm__ __volatile__("yield");
#endif
}

} // namespace detail

enum class CommandType : uint16_t {
    NoOp = 0,
    PlayVoice,
    StopVoice,
    PauseVoice,
    ResumeVoice,
    SetVoiceVolume,
    SetVoicePan,
    SetVoicePitch,
    SetVoiceTempo,
    SetVoiceLoop,
    SetVoicePriority,
    SetVoicePosition2D,
    SetVoicePosition3D,
    SetGroupVolume,
    SetGroupMute,
    SetGroupEffect,
    ClearGroupEffect,
    SetVoiceEffect,
    ClearVoiceEffect,
    SetVoiceMarker,
    ClearVoiceMarkers,
    SetListenerPosition2D,
    SetListenerPosition3D,
    SetListenerDirection,
    SetListenerRange,
};

// One command fits in 40 bytes; Slot pads it to a full 64-byte cache line.
struct Command {
    CommandType type    = CommandType::NoOp;
    uint16_t    _pad0   = 0;
    uint32_t    voiceId = 0;  // generation-stamped voice id
    uint32_t    groupId = 0;  // group index
    uint32_t    _pad1   = 0;
    void*       ptr     = nullptr;  // e.g. Sound* for PlayVoice
    union {
        float   f[4];
        int32_t i[4];
    } param{};
};
static_assert(sizeof(Command) == 40);

// MPSC lock-free ring buffer using sequence numbers (disruptor pattern).
//
// Multiple API threads push Commands; the single engine thread drains them.
// Capacity must be a power of two.
//
// Threading contract:
//   TryPush — any thread (atomic fetch_add claims a slot)
//   TryPop / DrainAll — engine thread only (single consumer, no CAS needed)
template<uint32_t Capacity = 1024>
class CommandQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr uint32_t kMask = Capacity - 1;

    // Each Slot occupies exactly one cache line so producer and consumer
    // reads/writes don't false-share across slots.
    struct alignas(64) Slot {
        std::atomic<uint64_t> sequence{0};
        Command               cmd{};
        // sizeof(Slot) = 8 (atomic) + 40 (cmd) + 16 pad = 64
        uint8_t _pad[64 - 8 - sizeof(Command)]{};
    };
    static_assert(sizeof(Slot) == 64);

public:
    CommandQueue() {
        for (uint64_t i = 0; i < Capacity; ++i)
            slots_[i].sequence.store(i, std::memory_order_relaxed);
    }

    // Producer (any thread): atomically claim a slot, write the command, publish.
    // Returns false only if the queue has been full for an unusually long time.
    [[nodiscard]] bool TryPush(const Command& cmd) noexcept {
        uint64_t pos = pushPos_.fetch_add(1, std::memory_order_relaxed);
        Slot&    slot = slots_[pos & kMask];

        // Wait until the consumer has freed this slot (sequence == pos).
        // For Capacity=1024 with normal usage this should never spin.
        int spins = 0;
        while (slot.sequence.load(std::memory_order_acquire) != pos) {
            if (++spins > 4096) return false; // genuinely full — drop command
            detail::SpinPause();
        }

        slot.cmd = cmd;
        slot.sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    // Consumer (engine thread only): pop one command.
    bool TryPop(Command& out) noexcept {
        uint64_t pos  = popPos_.load(std::memory_order_relaxed);
        Slot&    slot = slots_[pos & kMask];
        if (slot.sequence.load(std::memory_order_acquire) != pos + 1)
            return false;  // nothing published yet
        out = slot.cmd;
        slot.sequence.store(pos + Capacity, std::memory_order_release);
        popPos_.store(pos + 1, std::memory_order_relaxed);
        return true;
    }

    // Consumer: drain up to maxCount commands into outBuf. Returns count read.
    uint32_t DrainAll(Command* outBuf, uint32_t maxCount) noexcept {
        uint32_t n = 0;
        while (n < maxCount && TryPop(outBuf[n])) ++n;
        return n;
    }

private:
    alignas(64) std::array<Slot, Capacity> slots_;
    alignas(64) std::atomic<uint64_t>      pushPos_{ 0 };
    alignas(64) std::atomic<uint64_t>      popPos_ { 0 };
};

} // namespace libZFork::engine
