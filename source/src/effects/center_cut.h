#pragma once
#include <span>

namespace libZFork {

// Center-cut (vocal isolation / removal) for stereo signals.
// Vocal cut: removes center channel (common in both L and R) → stereo minus center.
// Side cut:  isolates the center channel (M/S decode).
class CenterCutProc {
public:
    void ProcessVocalCut(std::span<float> pcm, int channels);
    void ProcessSideCut(std::span<float> pcm, int channels);
};

} // namespace libZFork
