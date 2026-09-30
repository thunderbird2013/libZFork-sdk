#include "equalizer.h"
#include <cmath>
#include <numbers>
#include <algorithm>

namespace libZFork {

Equalizer::Equalizer() { gains_.fill(0.f); }

bool Equalizer::SetBandGain(int band, float gainDb) {
    if (band < 0 || band >= kBands) return false;
    gains_[band]   = gainDb;
    lastSampleRate_= 0; // force recompute next Process()
    return true;
}

bool Equalizer::GetBandGain(int band, float& gainDb) const {
    if (band < 0 || band >= kBands) return false;
    gainDb = gains_[band];
    return true;
}

void Equalizer::Reset() {
    gains_.fill(0.f);
    for (auto& f : filters_) f = {};
    lastSampleRate_ = 0;
}

void Equalizer::ComputeCoeffs(int band, int sampleRate) {
    // Peak EQ bi-quad (Robert Bristow-Johnson cookbook)
    float f0  = kCentreFreqs[band];
    float Q   = 1.41421356f; // 1/3-octave width
    float A   = std::pow(10.f, gains_[band] / 40.f);
    float w0  = 2.f * std::numbers::pi_v<float> * f0 / static_cast<float>(sampleRate);
    float cw0 = std::cos(w0);
    float alpha = std::sin(w0) / (2.f * Q);

    auto& bq = filters_[band];
    float b0 =  1.f + alpha * A;
    float b1 = -2.f * cw0;
    float b2 =  1.f - alpha * A;
    float a0 =  1.f + alpha / A;
    float a1 = -2.f * cw0;
    float a2 =  1.f - alpha / A;

    bq.b0 = b0 / a0;
    bq.b1 = b1 / a0;
    bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;
    bq.a2 = a2 / a0;
}

void Equalizer::Process(std::span<float> pcm, int channels, int sampleRate) {
    if (sampleRate != lastSampleRate_) {
        for (int b = 0; b < kBands; ++b) ComputeCoeffs(b, sampleRate);
        lastSampleRate_ = sampleRate;
    }

    for (auto& bq : filters_) {
        for (size_t i = 0; i < pcm.size(); i += channels) {
            // Left / mono
            float xL = pcm[i];
            float yL = bq.b0 * xL + bq.b1 * bq.x1 + bq.b2 * bq.x2
                     - bq.a1 * bq.y1 - bq.a2 * bq.y2;
            bq.x2 = bq.x1; bq.x1 = xL;
            bq.y2 = bq.y1; bq.y1 = yL;
            pcm[i] = yL;

            if (channels > 1) {
                float xR = pcm[i + 1];
                float yR = bq.b0 * xR + bq.b1 * bq.x1r + bq.b2 * bq.x2r
                         - bq.a1 * bq.y1r - bq.a2 * bq.y2r;
                bq.x2r = bq.x1r; bq.x1r = xR;
                bq.y2r = bq.y1r; bq.y1r = yR;
                pcm[i + 1] = yR;
            }
        }
    }
}

} // namespace libZFork
