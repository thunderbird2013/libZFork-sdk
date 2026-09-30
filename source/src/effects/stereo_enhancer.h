#pragma once
#include <span>

namespace libZFork {

// Adjustable stereo width via M/S processing.
// width = 1.0 → unchanged, > 1.0 → wider, < 1.0 → narrower (toward mono at 0).
class StereoEnhancer {
public:
    void SetWidth(float width);
    float GetWidth() const { return width_; }

    void Process(std::span<float> pcm, int channels);

private:
    float width_ = 1.0f;
};

} // namespace libZFork
