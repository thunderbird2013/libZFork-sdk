#pragma once
#include <span>
#include <vector>
#include <cstdint>

namespace libZFork {

// Discrete Haar Wavelet Transform — used by advanced BPM detector
class WaveletTransform {
public:
    // In-place forward DWT (Haar). Input length must be a power of 2.
    static void Forward(std::span<float> data);
    // In-place inverse DWT
    static void Inverse(std::span<float> data);
};

} // namespace libZFork
