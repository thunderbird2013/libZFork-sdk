#include "wavelet.h"
#include <cmath>

namespace libZFork {

void WaveletTransform::Forward(std::span<float> data) {
    size_t n = data.size();
    while (n >= 2) {
        for (size_t i = 0; i < n / 2; ++i) {
            float a = data[2*i];
            float b = data[2*i + 1];
            data[i]       = (a + b) * 0.5f;
            data[n/2 + i] = (a - b) * 0.5f;
        }
        // In-place: rotate
        std::vector<float> tmp(data.begin(), data.begin() + n);
        for (size_t i = 0; i < n; ++i) data[i] = tmp[i];
        n /= 2;
    }
}

void WaveletTransform::Inverse(std::span<float> data) {
    size_t n = 2;
    while (n <= data.size()) {
        for (size_t i = 0; i < n / 2; ++i) {
            float avg  = data[i];
            float diff = data[n/2 + i];
            data[2*i]     = avg + diff;
            data[2*i + 1] = avg - diff;
        }
        n *= 2;
    }
}

} // namespace libZFork
