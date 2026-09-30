#pragma once
#include <libzfork/libzfork.h>
#include <vector>
#include <complex>
#include <span>
#include <numbers>
#include <cmath>

namespace libZFork {

class FFTAnalyzer {
public:
    explicit FFTAnalyzer(uint32_t size = 2048);

    void SetSize(uint32_t size);
    void SetWindow(FFTWindow window);

    // Input: real-valued samples (one channel). Output: magnitude spectrum.
    // outMagnitude must have size/2 elements.
    void Compute(std::span<const float> input, std::span<float> outMagnitude);

private:
    void BuildWindow();
    void FFTInPlace(std::vector<std::complex<float>>& x);

    uint32_t                    size_    = 2048;
    FFTWindow                   window_  = FFTWindow::Hann;
    std::vector<float>          winCoeff_;
    std::vector<std::complex<float>> buf_;
};

} // namespace libZFork
