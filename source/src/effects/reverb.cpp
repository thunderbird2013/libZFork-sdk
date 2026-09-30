#include "reverb.h"
#include <algorithm>
#include <cstdint>

namespace libZFork {

namespace {
// Freeverb's classic tunings, in samples @ 44.1 kHz. Right channel adds a
// small offset per tap for stereo decorrelation (avoids a mono-sounding tank).
constexpr int kCombTuningL[4]      = { 1116, 1188, 1277, 1356 };
constexpr int kCombTuningR[4]      = { 1139, 1211, 1300, 1379 };
constexpr int kAllpassTuningL[2]   = { 556, 441 };
constexpr int kAllpassTuningR[2]   = { 579, 464 };

int ScaleToSampleRate(int samplesAt44k, int sampleRate) {
    return std::max(1, static_cast<int>(
        static_cast<int64_t>(samplesAt44k) * sampleRate / 44100));
}
} // namespace

void ReverbEffect::SetParams(const ReverbParams& p) {
    params_ = p;
}

void ReverbEffect::Reconfigure(int sampleRate, int channels) {
    for (int ch = 0; ch < 2; ++ch) {
        const int* combT = (ch == 0) ? kCombTuningL : kCombTuningR;
        for (int i = 0; i < kNumCombs; ++i) {
            combs_[ch][i].buf.assign(ScaleToSampleRate(combT[i], sampleRate), 0.f);
            combs_[ch][i].pos         = 0;
            combs_[ch][i].filterStore = 0.f;
        }
        const int* apT = (ch == 0) ? kAllpassTuningL : kAllpassTuningR;
        for (int i = 0; i < kNumAllpass; ++i) {
            allpass_[ch][i].buf.assign(ScaleToSampleRate(apT[i], sampleRate), 0.f);
            allpass_[ch][i].pos = 0;
        }
    }
    lastSampleRate_ = sampleRate;
    lastChannels_   = channels;
}

void ReverbEffect::Reset() {
    for (auto& tank : combs_)
        for (auto& c : tank) { std::fill(c.buf.begin(), c.buf.end(), 0.f); c.pos = 0; c.filterStore = 0.f; }
    for (auto& tank : allpass_)
        for (auto& a : tank) { std::fill(a.buf.begin(), a.buf.end(), 0.f); a.pos = 0; }
}

float ReverbEffect::ProcessSample(int tank, float input) {
    // Map roomSize/damping (0..1, user-facing) into stable filter coefficients.
    // feedback must stay < 1 to avoid runaway comb resonance.
    const float feedback = 0.28f + std::clamp(params_.roomSize, 0.f, 1.f) * 0.7f;
    const float damp     = std::clamp(params_.damping, 0.f, 1.f) * 0.4f;

    float sum = 0.f;
    for (auto& c : combs_[tank]) {
        const float y = c.buf[c.pos];
        c.filterStore = y * (1.f - damp) + c.filterStore * damp; // one-pole LPF in the feedback path
        c.buf[c.pos]  = input + c.filterStore * feedback;
        if (++c.pos >= static_cast<int>(c.buf.size())) c.pos = 0;
        sum += y;
    }
    sum *= 0.25f; // average the 4 parallel combs

    // Series allpass filters (unity-gain, g = 0.5) diffuse the comb output.
    float x = sum;
    for (auto& a : allpass_[tank]) {
        const float bufOut = a.buf[a.pos];
        const float y      = bufOut - x;
        a.buf[a.pos] = x + bufOut * 0.5f;
        if (++a.pos >= static_cast<int>(a.buf.size())) a.pos = 0;
        x = y;
    }
    return x;
}

void ReverbEffect::Process(std::span<float> pcm, int channels, int sampleRate) {
    if (channels <= 0 || sampleRate <= 0) return;
    if (sampleRate != lastSampleRate_ || channels != lastChannels_)
        Reconfigure(sampleRate, channels);

    const float wet = std::clamp(params_.wet, 0.f, 1.f);
    const float dry = std::clamp(params_.dry, 0.f, 1.f);

    for (size_t i = 0; i < pcm.size(); i += static_cast<size_t>(channels)) {
        for (int c = 0; c < channels; ++c) {
            const int   tank = std::min(c, 1); // channels beyond stereo reuse the right-channel tank
            const float in   = pcm[i + static_cast<size_t>(c)];
            const float w    = ProcessSample(tank, in);
            pcm[i + static_cast<size_t>(c)] = std::clamp(dry * in + wet * w, -1.f, 1.f);
        }
    }
}

} // namespace libZFork
