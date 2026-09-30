#include "stereo_enhancer.h"
#include <algorithm>
#include <cmath>

namespace libZFork {

void StereoEnhancer::SetWidth(float width) {
    width_ = std::clamp(width, 0.f, 2.f);
}

void StereoEnhancer::Process(std::span<float> pcm, int channels) {
    if (channels < 2 || std::abs(width_ - 1.f) < 1e-5f) {
        return;
    }
    for (size_t i = 0; i + 1 < pcm.size(); i += static_cast<size_t>(channels)) {
        const float l = pcm[i];
        const float r = pcm[i + 1];
        const float mid = (l + r) * 0.5f;
        const float side = (l - r) * 0.5f * width_;
        pcm[i]     = std::clamp(mid + side, -1.f, 1.f);
        pcm[i + 1] = std::clamp(mid - side, -1.f, 1.f);
    }
}

} // namespace libZFork
