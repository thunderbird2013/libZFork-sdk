#pragma once
#include <cstdint>

namespace libZFork::engine {

inline constexpr uint32_t kMaxVoices     = 64;   // simultaneous voice slots
inline constexpr uint32_t kMaxGroups     = 16;   // channel group slots
// Frames per mix block. This is also the granularity at which the engine
// thread drains the command queue (PlayVoice, SetVoiceVolume, ...), so it
// directly bounds "trigger-to-audible" latency: a command posted right after
// a block starts must wait for that whole block before it's picked up.
// History: 2048 (~46 ms, original) -> 512 (~11.6 ms) -> 256 (~5.8 ms) -> 128
// (~2.9 ms) -> 64 (~1.45 ms, current). Each halving here is paired with
// doubling WaveOutOutput::kBufferCount so the *total* buffered duration
// (kBufferCount * kMixFrames) — and therefore underrun safety margin —
// stays constant; only the command-drain granularity gets finer. Don't
// change one without the other. Measured with examples/latency_probe.cpp:
// 128 frames measured p50 ~4.6 ms real trigger latency.
inline constexpr uint32_t kMixFrames     = 64;
inline constexpr uint32_t kMaxEffectSlots = 4;   // effect chain depth per voice/group
inline constexpr uint32_t kMaxMarkers    = 8;   // position markers per voice
inline constexpr uint32_t kInvalidGroup  = 0xFFFFFFFFu;
inline constexpr uint32_t kInvalidVoice  = 0u;  // 0 is never a valid stamped id

} // namespace libZFork::engine
