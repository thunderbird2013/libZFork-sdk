#pragma once
#include <cstdint>

namespace libZFork::engine {

// Immutable listener description — updated via SetListenerPosition commands.
// All coordinates are world-space.
struct ListenerState {
    float x   = 0.f, y   = 0.f, z   = 0.f;  // position
    float dirX = 0.f, dirY = 1.f, dirZ = 0.f; // forward direction (normalized, default +Y)
    float minDist = 1.f;    // distance at which gain = 1.0
    float maxDist = 100.f;  // distance beyond which gain = 0.0
    float rolloff = 1.f;    // exponent for inverse-distance law (1=linear, 2=quadratic)
};

// Result of a spatial computation — directly feeds the mixer's pan/gain.
struct SpatialResult {
    float pan  = 0.f;  // [-1, +1] (left … right)
    float gain = 1.f;  // [0, 1]
};

// 2D positional audio (x/y plane, listener's z is ignored).
// pan  = projection of emitter direction onto listener's right vector
// gain = inverse-distance law (rolloff exponent)
[[nodiscard]] SpatialResult Compute2D(float emX, float emY,
                                      const ListenerState& listener) noexcept;

// 3D positional audio.
// Azimuth-based pan derived from the horizontal component of emitter direction.
// Distance computed in full 3D space.
[[nodiscard]] SpatialResult Compute3D(float emX, float emY, float emZ,
                                      const ListenerState& listener) noexcept;

} // namespace libZFork::engine
