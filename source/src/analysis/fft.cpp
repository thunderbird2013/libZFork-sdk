#include "fft.h"
#include <algorithm>
#include <stdexcept>
#include <bit>

namespace libZFork {

FFTAnalyzer::FFTAnalyzer(uint32_t size) {
    SetSize(size);
}

void FFTAnalyzer::SetSize(uint32_t size) {
    // Round up to next power of two
    size_ = std::bit_ceil(size);
    buf_.resize(size_);
    winCoeff_.resize(size_);
    BuildWindow();
}

void FFTAnalyzer::SetWindow(FFTWindow window) {
    window_ = window;
    BuildWindow();
}

void FFTAnalyzer::BuildWindow() {
    using std::numbers::pi_v;
    const float N = static_cast<float>(size_);
    const float N1 = N - 1.f;

    for (uint32_t n = 0; n < size_; ++n) {
        float fn = static_cast<float>(n);
        float w = 1.f;
        switch (window_) {
            case FFTWindow::Rectangular:
                w = 1.f; break;
            case FFTWindow::Hamming:
                w = 0.54f - 0.46f * std::cos(2.f * pi_v<float> * fn / N1); break;
            case FFTWindow::Hann:
                w = 0.5f * (1.f - std::cos(2.f * pi_v<float> * fn / N1)); break;
            case FFTWindow::Cosine:
                w = std::sin(pi_v<float> * fn / N1); break;
            case FFTWindow::Lanczos:
                { float x = 2.f * fn / N1 - 1.f;
                  w = (x == 0.f) ? 1.f : std::sin(pi_v<float> * x) / (pi_v<float> * x); break; }
            case FFTWindow::Bartlett:
                w = 1.f - std::abs(2.f * fn / N1 - 1.f); break;
            case FFTWindow::Triangular:
                w = 1.f - std::abs((2.f * fn - N1) / N); break;
            case FFTWindow::Gauss:
                { float sigma = 0.4f;
                  float x = (fn - N1 / 2.f) / (sigma * N1 / 2.f);
                  w = std::exp(-0.5f * x * x); break; }
            case FFTWindow::BartlettHann:
                w = 0.62f - 0.48f * std::abs(fn / N1 - 0.5f)
                  - 0.38f * std::cos(2.f * pi_v<float> * fn / N1); break;
            case FFTWindow::Blackman:
                w = 0.42f - 0.5f * std::cos(2.f * pi_v<float> * fn / N1)
                  + 0.08f * std::cos(4.f * pi_v<float> * fn / N1); break;
            case FFTWindow::Nuttall:
                w = 0.355768f - 0.487396f * std::cos(2.f * pi_v<float> * fn / N1)
                  + 0.144232f * std::cos(4.f * pi_v<float> * fn / N1)
                  - 0.012604f * std::cos(6.f * pi_v<float> * fn / N1); break;
            case FFTWindow::BlackmanHarris:
                w = 0.35875f - 0.48829f * std::cos(2.f * pi_v<float> * fn / N1)
                  + 0.14128f * std::cos(4.f * pi_v<float> * fn / N1)
                  - 0.01168f * std::cos(6.f * pi_v<float> * fn / N1); break;
            case FFTWindow::BlackmanNuttall:
                w = 0.3635819f - 0.4891775f * std::cos(2.f * pi_v<float> * fn / N1)
                  + 0.1365995f * std::cos(4.f * pi_v<float> * fn / N1)
                  - 0.0106411f * std::cos(6.f * pi_v<float> * fn / N1); break;
            case FFTWindow::FlatTop:
                w = 1.f - 1.93f * std::cos(2.f * pi_v<float> * fn / N1)
                  + 1.29f * std::cos(4.f * pi_v<float> * fn / N1)
                  - 0.388f * std::cos(6.f * pi_v<float> * fn / N1)
                  + 0.032f * std::cos(8.f * pi_v<float> * fn / N1); break;
        }
        winCoeff_[n] = w;
    }
}

void FFTAnalyzer::FFTInPlace(std::vector<std::complex<float>>& x) {
    uint32_t N = static_cast<uint32_t>(x.size());
    // Bit-reversal permutation
    for (uint32_t i = 1, j = 0; i < N; ++i) {
        uint32_t bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    // Cooley-Tukey iterative FFT
    for (uint32_t len = 2; len <= N; len <<= 1) {
        float ang = -2.f * std::numbers::pi_v<float> / static_cast<float>(len);
        std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (uint32_t i = 0; i < N; i += len) {
            std::complex<float> w(1.f, 0.f);
            for (uint32_t j = 0; j < len / 2; ++j) {
                auto u = x[i + j];
                auto v = x[i + j + len/2] * w;
                x[i + j]           = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

void FFTAnalyzer::Compute(std::span<const float> input, std::span<float> outMagnitude) {
    uint32_t n = std::min<uint32_t>(size_, static_cast<uint32_t>(input.size()));
    buf_.assign(size_, {0.f, 0.f});
    for (uint32_t i = 0; i < n; ++i)
        buf_[i] = { input[i] * winCoeff_[i], 0.f };

    FFTInPlace(buf_);

    uint32_t half = size_ / 2;
    float scale   = 2.f / static_cast<float>(size_);
    uint32_t outN = std::min<uint32_t>(half, static_cast<uint32_t>(outMagnitude.size()));
    for (uint32_t i = 0; i < outN; ++i)
        outMagnitude[i] = std::abs(buf_[i]) * scale;
}

} // namespace libZFork
