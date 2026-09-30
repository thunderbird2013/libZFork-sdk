#pragma once
#include <libzfork/libzfork.h>
#include "fft.h"
#include <array>
#include <vector>
#include <mutex>
#include <span>
#include <cstdint>
#ifdef _WIN32
#  include <windows.h>
#endif

namespace libZFork {

class DecoderBase;

class SpectrumAnalyzer {
public:
    SpectrumAnalyzer();

    void SetFFTSize(uint32_t points);
    void SetWindow(FFTWindow window);
    void SetGraphType(FFTGraphType type);
    void SetColor(int index, uint32_t colorRGB);

    // Feed new PCM into the internal accumulation buffer
    void Feed(std::span<const float> pcm, int channels);

    // Copy latest magnitude data
    [[nodiscard]] bool GetData(float* outLeft, float* outRight, uint32_t points) const;

#ifdef _WIN32
    // Draw onto a Windows GDI device context.
    [[nodiscard]] bool DrawGraph(HDC hdc, int x, int y, int w, int h) const;
#endif

    // BPM detection over a decoder's full stream
    [[nodiscard]] float DetectBPM(BPMAlgorithm algo, DecoderBase& decoder);

private:
    FFTAnalyzer            fft_;
    uint32_t               fftSize_  = 2048;
    FFTGraphType           graphType_= FFTGraphType::BarsLeftOnTop;
    std::array<uint32_t, 16> colors_{};

    mutable std::mutex     mtx_;
    std::vector<float>     accumL_;
    std::vector<float>     accumR_;
    std::vector<float>     magL_;
    std::vector<float>     magR_;
    int                    accumPos_ = 0;

    // BPM helpers
    float DetectBPMPeaks(DecoderBase& decoder);
    float DetectBPMAutoCorr(DecoderBase& decoder);
    float DetectBPMAdvanced(DecoderBase& decoder);
};

} // namespace libZFork
