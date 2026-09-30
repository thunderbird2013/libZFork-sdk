#include "center_cut.h"
#include <algorithm>
#include <cmath>

namespace libZFork {

void CenterCutProc::ProcessVocalCut(std::span<float> pcm, int channels) {
    if (channels < 2) return;
    for (size_t i = 0; i + 1 < pcm.size(); i += channels) {
        float l = pcm[i];
        float r = pcm[i + 1];
        float side = (l - r) * 0.5f;
        pcm[i]     = std::clamp( side, -1.f, 1.f);
        pcm[i + 1] = std::clamp(-side, -1.f, 1.f);
    }
}

void CenterCutProc::ProcessSideCut(std::span<float> pcm, int channels) {
    if (channels < 2) return;
    for (size_t i = 0; i + 1 < pcm.size(); i += channels) {
        float l = pcm[i];
        float r = pcm[i + 1];
        float mid = (l + r) * 0.5f;
        pcm[i]     = std::clamp(mid, -1.f, 1.f);
        pcm[i + 1] = std::clamp(mid, -1.f, 1.f);
    }
}

} // namespace libZFork
