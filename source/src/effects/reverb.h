#pragma once
#include <array>
#include <span>
#include <vector>

namespace libZFork {

struct ReverbParams {
    float roomSize = 0.5f; // 0..1 — larger = longer decay (feedback amount)
    float damping   = 0.5f; // 0..1 — larger = darker/more high-frequency absorption
    float wet       = 0.3f; // 0..1 — reverberated signal level
    float dry       = 0.7f; // 0..1 — unprocessed signal level
};

// Freeverb-style Schroeder/Moorer reverb: 4 parallel comb filters (each with
// a one-pole lowpass in its feedback path for damping) followed by 2 series
// allpass filters, per channel. Classic, CPU-cheap real-time algorithm —
// O(1) per sample per filter, no per-sample allocation, buffers sized once
// on first Process()/parameter change. Left/right channels use slightly
// different (Freeverb-tuned) delay lengths for stereo decorrelation.
class ReverbEffect {
public:
    ReverbEffect() = default;

    void SetParams(const ReverbParams& p);
    ReverbParams GetParams() const { return params_; }

    void Process(std::span<float> pcm, int channels, int sampleRate);
    void Reset();

private:
    static constexpr int kNumCombs   = 4;
    static constexpr int kNumAllpass = 2;

    struct Comb {
        std::vector<float> buf;
        int   pos         = 0;
        float filterStore = 0.f;
    };
    struct Allpass {
        std::vector<float> buf;
        int pos = 0;
    };

    std::array<std::array<Comb, kNumCombs>, 2>       combs_;
    std::array<std::array<Allpass, kNumAllpass>, 2>  allpass_;

    ReverbParams params_{};
    int lastSampleRate_ = 0;
    int lastChannels_   = 0;

    void  Reconfigure(int sampleRate, int channels);
    float ProcessSample(int tank, float input);
};

} // namespace libZFork
