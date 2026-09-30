#pragma once
#include <libzfork/libzfork.h>
#include <vector>
#include <span>

namespace libZFork {

class EchoEffect {
public:
    EchoEffect();

    void SetParams(const EchoParams& p);
    EchoParams GetParams() const { return params_; }

    void Process(std::span<float> pcm, int channels, int sampleRate);
    void Reset();

private:
    EchoParams         params_{};
    std::vector<float> delayBufL_;
    std::vector<float> delayBufR_;
    int                posL_ = 0;
    int                posR_ = 0;
    int                lenL_ = 0;
    int                lenR_ = 0;
    int                lastSampleRate_ = 0;
    int                lastChannels_   = 0;

    void Reconfigure(int sampleRate, int channels);
};

} // namespace libZFork
