#pragma once
#include <array>
#include <span>
#include <cstdint>

namespace libZFork {

// 31-band graphic equalizer using bi-quad IIR filters (one per band, stereo)
// Band centre frequencies follow 1/3-octave spacing from ~20 Hz to ~20 kHz
class Equalizer {
public:
    static constexpr int kBands = 31;

    Equalizer();

    bool SetBandGain(int band, float gainDb);
    bool GetBandGain(int band, float& gainDb) const;
    void Reset();

    // Process interleaved stereo (or mono) float PCM in-place
    void Process(std::span<float> pcm, int channels, int sampleRate);

private:
    struct BiQuad {
        float b0 = 1.f, b1 = 0.f, b2 = 0.f;
        float a1 = 0.f, a2 = 0.f;
        float x1 = 0.f, x2 = 0.f;  // delay for left
        float y1 = 0.f, y2 = 0.f;
        float x1r= 0.f, x2r= 0.f;  // delay for right
        float y1r= 0.f, y2r= 0.f;
    };

    void ComputeCoeffs(int band, int sampleRate);

    std::array<float,   kBands> gains_{};   // dB
    std::array<BiQuad,  kBands> filters_{};
    int   lastSampleRate_ = 0;

    static constexpr std::array<float, kBands> kCentreFreqs = {
        20.f, 25.f, 31.5f, 40.f, 50.f, 63.f, 80.f, 100.f, 125.f, 160.f,
        200.f, 250.f, 315.f, 400.f, 500.f, 630.f, 800.f, 1000.f, 1250.f, 1600.f,
        2000.f, 2500.f, 3150.f, 4000.f, 5000.f, 6300.f, 8000.f, 10000.f, 12500.f, 16000.f,
        20000.f
    };
};

} // namespace libZFork
