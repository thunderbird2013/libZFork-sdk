#include "echo.h"
#include <algorithm>
#include <cmath>

namespace libZFork {

EchoEffect::EchoEffect() {
    params_.leftDelay     = 300;
    params_.leftSourceVol = 100;
    params_.leftEchoVol   = 50;
    params_.rightDelay    = 300;
    params_.rightSourceVol= 100;
    params_.rightEchoVol  = 50;
}

void EchoEffect::SetParams(const EchoParams& p) {
    params_ = p;
    lastSampleRate_ = 0; // force reconfigure
}

void EchoEffect::Reconfigure(int sampleRate, int channels) {
    // Cap delay length — pathological params (huge ms * sampleRate) used to
    // allocate multi-GB delay lines and hang / OOM the audio thread.
    constexpr int kMaxDelaySamples = 10 * 48000; // 10 s @ 48 kHz
    const int sr = std::max(1, sampleRate);
    lenL_ = std::clamp(params_.leftDelay  * sr / 1000, 1, kMaxDelaySamples);
    lenR_ = std::clamp(params_.rightDelay * sr / 1000, 1, kMaxDelaySamples);
    delayBufL_.assign(static_cast<size_t>(lenL_), 0.f);
    delayBufR_.assign(static_cast<size_t>(lenR_), 0.f);
    posL_ = posR_ = 0;
    lastSampleRate_ = sampleRate;
    lastChannels_   = channels;
}

void EchoEffect::Reset() {
    delayBufL_.clear();
    delayBufR_.clear();
    posL_ = posR_ = 0;
    lastSampleRate_ = 0;
}

void EchoEffect::Process(std::span<float> pcm, int channels, int sampleRate) {
    if (sampleRate != lastSampleRate_ || channels != lastChannels_)
        Reconfigure(sampleRate, channels);

    float sv_l = params_.leftSourceVol  / 100.f;
    float ev_l = params_.leftEchoVol    / 100.f;
    float sv_r = params_.rightSourceVol / 100.f;
    float ev_r = params_.rightEchoVol   / 100.f;

    if (channels <= 0 || lenL_ <= 0 || lenR_ <= 0) return;

    for (size_t i = 0; i + static_cast<size_t>(channels) <= pcm.size(); i += static_cast<size_t>(channels)) {
        float inL = pcm[i];
        float echo_l = delayBufL_[static_cast<size_t>(posL_)];
        float outL   = sv_l * inL + ev_l * echo_l;
        delayBufL_[static_cast<size_t>(posL_)] = outL;
        // Branch wrap is cheaper than integer modulo on the hot path.
        if (++posL_ >= lenL_) posL_ = 0;
        pcm[i] = std::clamp(outL, -1.f, 1.f);

        if (channels > 1) {
            float inR = pcm[i + 1];
            float echo_r = delayBufR_[static_cast<size_t>(posR_)];
            float outR   = sv_r * inR + ev_r * echo_r;
            delayBufR_[static_cast<size_t>(posR_)] = outR;
            if (++posR_ >= lenR_) posR_ = 0;
            pcm[i + 1] = std::clamp(outR, -1.f, 1.f);
        }
    }
}

} // namespace libZFork
