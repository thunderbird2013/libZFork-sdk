#include "soundtouch_proc.h"
#include <algorithm>
#include <cmath>

namespace libZFork {

SoundTouchProc::SoundTouchProc() {
    // Alle SoundTouch-Einstellungen werden erst in Configure() gesetzt,
    // wenn sampleRate und channels bekannt sind. SoundTouch unterstützt
    // nur Mono (1) und Stereo (2) — alle anderen Kanalzahlen werfen
    // std::runtime_error. Channels werden in Configure() auf 1–2 geclampt.
}

void SoundTouchProc::Configure(int sampleRate, int channels) {
    // SoundTouch akzeptiert nur Mono (1) und Stereo (2).
    // Mehrkanalige Quellen (AAC 5.1, OGG Surround, …) werden auf Stereo geclampt.
    channels_ = std::clamp(channels, 1, 2);
    st_.setSampleRate(sampleRate);
    st_.setChannels(channels_);
    // Einstellungen die von sampleRate abhängen — sicher erst hier setzen
    st_.setSetting(SETTING_USE_QUICKSEEK, 0);
    st_.setSetting(SETTING_USE_AA_FILTER, 1);
    st_.setSetting(SETTING_SEQUENCE_MS, 40);
    st_.setSetting(SETTING_OVERLAP_MS,  12);
    st_.setRate(rate_);
    st_.setPitchSemiTones(semitones_);
    st_.setTempo(tempo_);
}

bool SoundTouchProc::SetRate(float rate) {
    if (rate <= 0.f) return false;
    rate_ = rate;
    st_.setRate(rate);
    clearPending_.store(true, std::memory_order_release);
    return true;
}

bool SoundTouchProc::SetPitch(float semitones) {
    semitones_ = semitones;
    st_.setPitchSemiTones(semitones);
    clearPending_.store(true, std::memory_order_release);
    return true;
}

bool SoundTouchProc::SetTempo(float tempo) {
    if (tempo < 0.05f) return false;
    tempo_ = tempo;
    st_.setTempo(tempo);
    clearPending_.store(true, std::memory_order_release);
    return true;
}

bool SoundTouchProc::IsActive() const noexcept {
    return std::fabs(rate_ - 1.f) > 0.001f
        || std::fabs(semitones_) > 0.001f
        || std::fabs(tempo_ - 1.f) > 0.001f;
}

void SoundTouchProc::DrainSoundTouch() {
    const uint32_t ch = static_cast<uint32_t>(channels_);
    for (;;) {
        pullBuf_.resize(4096 * ch);
        const uint32_t got = st_.receiveSamples(pullBuf_.data(), 4096);
        if (got == 0) break;
        const size_t old = pending_.size();
        pending_.resize(old + static_cast<size_t>(got) * ch);
        std::copy(pullBuf_.begin(), pullBuf_.begin() + static_cast<size_t>(got) * ch,
                  pending_.begin() + static_cast<ptrdiff_t>(old));
    }
}

void SoundTouchProc::PutSamples(const float* data, uint32_t numFrames, int channels) {
    if (!data || numFrames == 0) return;
    if (!IsActive()) {
        const size_t samples = static_cast<size_t>(numFrames) * static_cast<size_t>(channels);
        const size_t old = pending_.size();
        pending_.resize(old + samples);
        std::copy(data, data + samples, pending_.begin() + static_cast<ptrdiff_t>(old));
        return;
    }
    st_.putSamples(data, numFrames);
    DrainSoundTouch();
}

uint32_t SoundTouchProc::Receive(float* dest, uint32_t maxFrames, int channels) {
    if (!dest || maxFrames == 0) return 0;

    const uint32_t ch = static_cast<uint32_t>(channels);
    uint32_t written = 0;

    while (written < maxFrames && pending_.size() >= ch) {
        const size_t frameStart = static_cast<size_t>(written) * ch;
        std::copy(pending_.begin(), pending_.begin() + ch, dest + frameStart);
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<ptrdiff_t>(ch));
        ++written;
    }

    if (written >= maxFrames || !IsActive()) return written;

    while (written < maxFrames) {
        const uint32_t need = maxFrames - written;
        pullBuf_.resize(static_cast<size_t>(need) * ch);
        const uint32_t got = st_.receiveSamples(pullBuf_.data(), need);
        if (got == 0) break;

        std::copy(pullBuf_.begin(), pullBuf_.begin() + static_cast<size_t>(got) * ch,
                  dest + static_cast<size_t>(written) * ch);
        written += got;

        if (got < need && got > 0) {
            const size_t old = pending_.size();
            const size_t extra = static_cast<size_t>(got - need) * ch; // won't happen, got <= need
            (void)old; (void)extra;
        }
    }

    return written;
}

uint32_t SoundTouchProc::AvailableFrames() const {
    const uint32_t ch = static_cast<uint32_t>(std::max(channels_, 1));
    return static_cast<uint32_t>(pending_.size() / ch) + st_.numSamples();
}

void SoundTouchProc::Clear() {
    st_.clear();
    pending_.clear();
    pullBuf_.clear();
}

void SoundTouchProc::Process(std::span<float> pcm, int channels) {
    // Parameter haben sich geändert → internen Buffer leeren (kein Klicken)
    if (clearPending_.exchange(false, std::memory_order_acq_rel)) {
        st_.clear();
        pending_.clear();
        pullBuf_.clear();
    }
    if (!IsActive()) return;

    const uint32_t frames = static_cast<uint32_t>(pcm.size() / static_cast<size_t>(channels));
    PutSamples(pcm.data(), frames, channels);

    const uint32_t got = Receive(pcm.data(), frames, channels);
    if (got < frames)
        std::fill(pcm.begin() + static_cast<size_t>(got) * static_cast<size_t>(channels),
                  pcm.end(), 0.f);
}

} // namespace libZFork
