#include "engine/spatial.h"
#include <cmath>
#include <algorithm>

namespace libZFork::engine {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Gain from inverse-distance model with rolloff exponent.
static float DistanceGain(float dist, const ListenerState& ls) noexcept {
    if (dist <= ls.minDist) return 1.f;
    if (dist >= ls.maxDist) return 0.f;
    // Normalise to [0,1] range, then apply rolloff (0 = near, 1 = far).
    float t = (dist - ls.minDist) / (ls.maxDist - ls.minDist);
    // gain = 1 - t^rolloff  (0 at far edge, 1 at min distance)
    return 1.f - std::pow(t, ls.rolloff);
}

// Project normalised emitter direction onto listener's right vector → pan.
// listener.dir is the forward direction; right = rotate 90° clockwise in XY.
static float AzimuthPan2D(float dx, float dy, const ListenerState& ls) noexcept {
    // Right vector = perpendicular to forward in the XY plane (rotated +90°).
    float rx =  ls.dirY;
    float ry = -ls.dirX;

    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1e-6f) return 0.f;

    float ndx = dx / dist;
    float ndy = dy / dist;

    float pan = ndx * rx + ndy * ry;
    // clamp to [-1, +1] (should already be, but floating-point safety)
    return std::max(-1.f, std::min(1.f, pan));
}

// ---------------------------------------------------------------------------
// Compute2D
// ---------------------------------------------------------------------------

SpatialResult Compute2D(float emX, float emY, const ListenerState& ls) noexcept {
    float dx = emX - ls.x;
    float dy = emY - ls.y;
    float dist = std::sqrt(dx * dx + dy * dy);

    SpatialResult r;
    r.pan  = AzimuthPan2D(dx, dy, ls);
    r.gain = DistanceGain(dist, ls);
    return r;
}

// ---------------------------------------------------------------------------
// Compute3D
// ---------------------------------------------------------------------------

SpatialResult Compute3D(float emX, float emY, float emZ,
                         const ListenerState& ls) noexcept {
    float dx = emX - ls.x;
    float dy = emY - ls.y;
    float dz = emZ - ls.z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    // Pan uses only the horizontal (XY) component relative to listener forward.
    SpatialResult r;
    r.pan  = AzimuthPan2D(dx, dy, ls);
    r.gain = DistanceGain(dist, ls);
    return r;
}

} // namespace libZFork::engine
