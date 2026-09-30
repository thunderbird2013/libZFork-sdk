#include "spectrum.h"
#include "decoders/decoder_base.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numeric>
#include <numbers>

namespace libZFork {

SpectrumAnalyzer::SpectrumAnalyzer() : fft_(2048) {
    colors_[0]  = 0x000000; // background
    colors_[1]  = 0x00CC00; // left bar
    colors_[2]  = 0x0000CC; // right bar
    colors_[3]  = 0xFFFFFF; // peak
    accumL_.resize(fftSize_);
    accumR_.resize(fftSize_);
    magL_.resize(fftSize_ / 2);
    magR_.resize(fftSize_ / 2);
}

void SpectrumAnalyzer::SetFFTSize(uint32_t points) {
    std::lock_guard lock(mtx_);
    fftSize_ = std::bit_ceil(points);
    fft_.SetSize(fftSize_);
    accumL_.assign(fftSize_, 0.f);
    accumR_.assign(fftSize_, 0.f);
    magL_.resize(fftSize_ / 2);
    magR_.resize(fftSize_ / 2);
    accumPos_ = 0;
}

void SpectrumAnalyzer::SetWindow(FFTWindow w)     { fft_.SetWindow(w); }
void SpectrumAnalyzer::SetGraphType(FFTGraphType t){ graphType_ = t; }
void SpectrumAnalyzer::SetColor(int i, uint32_t c){
    if (i >= 0 && i < 16) colors_[i] = c;
}

void SpectrumAnalyzer::Feed(std::span<const float> pcm, int channels) {
    std::lock_guard lock(mtx_);
    for (size_t i = 0; i < pcm.size(); i += channels) {
        accumL_[accumPos_] = pcm[i];
        accumR_[accumPos_] = (channels > 1) ? pcm[i + 1] : pcm[i];
        ++accumPos_;
        if (accumPos_ >= static_cast<int>(fftSize_)) {
            fft_.Compute(accumL_, magL_);
            fft_.Compute(accumR_, magR_);
            accumPos_ = 0;
        }
    }
}

bool SpectrumAnalyzer::GetData(float* outLeft, float* outRight, uint32_t points) const {
    std::lock_guard lock(mtx_);
    uint32_t n = std::min(points, static_cast<uint32_t>(magL_.size()));
    std::copy_n(magL_.data(), n, outLeft);
    if (outRight) std::copy_n(magR_.data(), n, outRight);
    return true;
}

#ifdef _WIN32
bool SpectrumAnalyzer::DrawGraph(HDC hdc, int x, int y, int w, int h) const {
    if (!hdc || w <= 0 || h <= 0) return false;
    std::lock_guard lock(mtx_);

    uint32_t bins  = static_cast<uint32_t>(magL_.size());
    float    binW  = static_cast<float>(w) / std::max(1u, bins);

    HBRUSH bgBrush  = CreateSolidBrush(colors_[0] & 0xFFFFFF);
    RECT   rc { x, y, x + w, y + h };
    FillRect(hdc, &rc, bgBrush);
    DeleteObject(bgBrush);

    for (uint32_t b = 0; b < bins; ++b) {
        float magL = std::clamp(magL_[b], 0.f, 1.f);
        float magR = std::clamp(magR_[b], 0.f, 1.f);

        int bx = x + static_cast<int>(b * binW);
        int bw = std::max(1, static_cast<int>(binW) - 1);

        // Left bar (top half)
        int barH_L = static_cast<int>(magL * (h / 2));
        RECT rL { bx, y + h/2 - barH_L, bx + bw, y + h/2 };
        HBRUSH brL = CreateSolidBrush(colors_[1] & 0xFFFFFF);
        FillRect(hdc, &rL, brL);
        DeleteObject(brL);

        // Right bar (bottom half)
        int barH_R = static_cast<int>(magR * (h / 2));
        RECT rR { bx, y + h/2, bx + bw, y + h/2 + barH_R };
        HBRUSH brR = CreateSolidBrush(colors_[2] & 0xFFFFFF);
        FillRect(hdc, &rR, brR);
        DeleteObject(brR);
    }
    return true;
}
#endif // _WIN32

// ── BPM detection ──────────────────────────────────────────────────────────────

float SpectrumAnalyzer::DetectBPM(BPMAlgorithm algo, DecoderBase& decoder) {
    switch (algo) {
        case BPMAlgorithm::Peaks:          return DetectBPMPeaks(decoder);
        case BPMAlgorithm::AutoCorrelation: return DetectBPMAutoCorr(decoder);
        case BPMAlgorithm::Advanced:        return DetectBPMAdvanced(decoder);
    }
    return 0.f;
}

static std::vector<float> ReadEnergy(DecoderBase& decoder, int& sampleRate, int& channels) {
    decoder.Seek(SeekOrigin::Begin, {});
    sampleRate = decoder.SampleRate();
    channels   = decoder.Channels();
    constexpr uint32_t kFrames = 1024;
    std::vector<float> pcm(kFrames * channels);
    std::vector<float> energy;
    energy.reserve(16384);

    while (true) {
        uint32_t got = decoder.DecodeFloat(pcm.data(), kFrames);
        if (!got) break;
        float e = 0.f;
        for (uint32_t i = 0; i < got * channels; ++i) e += pcm[i] * pcm[i];
        energy.push_back(e / (got * channels));
    }
    decoder.Seek(SeekOrigin::Begin, {});
    return energy;
}

float SpectrumAnalyzer::DetectBPMPeaks(DecoderBase& decoder) {
    int sr = 44100, ch = 2;
    auto energy = ReadEnergy(decoder, sr, ch);
    if (energy.size() < 10) return 0.f;

    // Low-pass: running average over 4 blocks (~185ms at 44100/1024)
    constexpr int kWin = 4;
    float threshold = *std::max_element(energy.begin(), energy.end()) * 0.35f;
    std::vector<int> peaks;
    for (size_t i = kWin; i + kWin < energy.size(); ++i) {
        if (energy[i] > threshold) {
            bool isPeak = true;
            for (int k = -kWin; k <= kWin; ++k)
                if (energy[i + k] > energy[i]) { isPeak = false; break; }
            if (isPeak) peaks.push_back(static_cast<int>(i));
        }
    }
    if (peaks.size() < 4) return 0.f;

    float framesPerBlock = 1024.f;
    float secondsPerBlock = framesPerBlock / sr;
    float avgInterval = 0.f;
    for (size_t i = 1; i < peaks.size(); ++i)
        avgInterval += static_cast<float>(peaks[i] - peaks[i-1]);
    avgInterval /= static_cast<float>(peaks.size() - 1);

    float bpm = 60.f / (avgInterval * secondsPerBlock);
    while (bpm < 80.f) bpm *= 2.f;
    while (bpm > 200.f) bpm /= 2.f;
    return bpm;
}

float SpectrumAnalyzer::DetectBPMAutoCorr(DecoderBase& decoder) {
    int sr = 44100, ch = 2;
    auto energy = ReadEnergy(decoder, sr, ch);
    if (energy.size() < 256) return 0.f;

    size_t N = std::min(energy.size(), static_cast<size_t>(4096));
    std::vector<float> ac(N, 0.f);
    for (size_t lag = 0; lag < N; ++lag)
        for (size_t i = 0; i + lag < N; ++i)
            ac[lag] += energy[i] * energy[i + lag];

    float framesPerBlock = 1024.f;
    float secondsPerBlock = framesPerBlock / sr;

    // Search for dominant lag between 0.25 s (240 bpm) and 1.5 s (40 bpm)
    size_t lagMin = static_cast<size_t>(0.25f / secondsPerBlock);
    size_t lagMax = std::min(N - 1, static_cast<size_t>(1.5f  / secondsPerBlock));

    size_t bestLag = lagMin;
    for (size_t lag = lagMin; lag <= lagMax; ++lag)
        if (ac[lag] > ac[bestLag]) bestLag = lag;

    float bpm = 60.f / (static_cast<float>(bestLag) * secondsPerBlock);
    while (bpm < 80.f)  bpm *= 2.f;
    while (bpm > 200.f) bpm /= 2.f;
    return bpm;
}

float SpectrumAnalyzer::DetectBPMAdvanced(DecoderBase& decoder) {
    // Average of both methods for better accuracy
    float bpm1 = DetectBPMPeaks(decoder);
    float bpm2 = DetectBPMAutoCorr(decoder);
    if (bpm1 <= 0.f) return bpm2;
    if (bpm2 <= 0.f) return bpm1;
    return (bpm1 + bpm2) * 0.5f;
}

} // namespace libZFork
