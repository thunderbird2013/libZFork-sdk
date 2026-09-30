#include "engine/voice.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace libZFork::engine {

void Voice::Init(uint32_t mixFrames, uint32_t channels, uint32_t sampleRate) {
    engineChannels_   = channels;
    engineSampleRate_ = sampleRate;
    mixBuf.assign(mixFrames * channels, 0.f);
    decodeScratch_.assign(mixFrames * channels, 0.f);
}

// ---------------------------------------------------------------------------
// Channel-count conversion
// ---------------------------------------------------------------------------

static void Upmix(const float* src, float* dst,
                  uint32_t frames, uint32_t srcCh, uint32_t dstCh) noexcept {
    if (srcCh == dstCh) {
        std::memcpy(dst, src, frames * dstCh * sizeof(float));
        return;
    }
    if (srcCh == 1 && dstCh == 2) {
        for (uint32_t f = 0; f < frames; ++f) {
            dst[f * 2]     = src[f];
            dst[f * 2 + 1] = src[f];
        }
        return;
    }
    if (srcCh == 2 && dstCh == 1) {
        for (uint32_t f = 0; f < frames; ++f)
            dst[f] = (src[f * 2] + src[f * 2 + 1]) * 0.5f;
        return;
    }
    uint32_t cp = std::min(srcCh, dstCh);
    for (uint32_t f = 0; f < frames; ++f) {
        for (uint32_t c = 0; c < cp; ++c)
            dst[f * dstCh + c] = src[f * srcCh + c];
        for (uint32_t c = cp; c < dstCh; ++c)
            dst[f * dstCh + c] = 0.f;
    }
}

void Voice::ApplyVoiceEffects(std::span<float> pcm, uint32_t channels) {
    for (auto& fx : voiceEffects_)
        if (fx) fx->Process(pcm, static_cast<int>(channels), engineSampleRate_);
}

uint32_t Voice::DecodeFrames(float* dst, uint32_t frames,
                             uint32_t soundCh, uint32_t engineCh) {
    if (!sound_) return 0;

    const int  srcRate      = sound_->SampleRate();
    const int  dstRate      = static_cast<int>(engineSampleRate_);
    const bool needResample = srcRate > 0 && dstRate > 0 && srcRate != dstRate;

    if (sound_->Type() == SoundType::PreDecoded) {
        const auto&    pcm   = sound_->PCMData();
        const uint32_t total = sound_->FrameCount();
        if (total == 0) return 0;

        if (!needResample) {
            if (readCursor_ >= total) {
                int32_t loops = params.loopCount.load(std::memory_order_relaxed);
                if (loops == 0) return 0;
                if (loops > 0) params.loopCount.store(loops - 1, std::memory_order_relaxed);
                readCursor_ = 0;
            }

            const uint32_t available = total - readCursor_;
            const uint32_t toCopy    = std::min(frames, available);
            const float* src         = pcm.data() + readCursor_ * soundCh;

            if (soundCh == engineCh) {
                std::memcpy(dst, src, toCopy * engineCh * sizeof(float));
            } else {
                Upmix(src, dst, toCopy, soundCh, engineCh);
            }
            readCursor_ += toCopy;
            return toCopy;
        }

        // Resampled path: linear-interpolate directly from the full PCM
        // buffer using a fractional cursor. Random access to pcm makes this
        // simple — no state needs to survive between DecodeFrames() calls
        // beyond the cursor itself (unlike the Streaming case below).
        const double ratio = static_cast<double>(srcRate) / dstRate; // source frames per output frame
        uint32_t produced = 0;
        for (; produced < frames; ++produced) {
            if (preciseCursor_ >= total) {
                int32_t loops = params.loopCount.load(std::memory_order_relaxed);
                if (loops == 0) break;
                if (loops > 0) params.loopCount.store(loops - 1, std::memory_order_relaxed);
                preciseCursor_ -= total;
                if (preciseCursor_ < 0.0) preciseCursor_ = 0.0;
            }
            const uint32_t i0 = static_cast<uint32_t>(preciseCursor_);
            const uint32_t i1 = std::min(i0 + 1, total - 1);
            const float    t  = static_cast<float>(preciseCursor_ - i0);
            const float* a = pcm.data() + static_cast<size_t>(i0) * soundCh;
            const float* b = pcm.data() + static_cast<size_t>(i1) * soundCh;

            if (soundCh == engineCh) {
                float* o = dst + static_cast<size_t>(produced) * engineCh;
                for (uint32_t c = 0; c < soundCh; ++c) o[c] = a[c] + (b[c] - a[c]) * t;
            } else {
                decodeScratch_.resize(soundCh);
                for (uint32_t c = 0; c < soundCh; ++c) decodeScratch_[c] = a[c] + (b[c] - a[c]) * t;
                Upmix(decodeScratch_.data(), dst + static_cast<size_t>(produced) * engineCh, 1, soundCh, engineCh);
            }
            preciseCursor_ += ratio;
        }
        readCursor_ = static_cast<uint32_t>(preciseCursor_);
        return produced;
    }

    if (!decoder_) return 0;

    if (needResample)
        return DecodeStreamingResampled(dst, frames, soundCh, engineCh, srcRate, dstRate);

    uint32_t produced = 0;
    if (soundCh == engineCh) {
        produced = decoder_->DecodeFloat(dst, frames);
    } else {
        decodeScratch_.resize(frames * soundCh);
        produced = decoder_->DecodeFloat(decodeScratch_.data(), frames);
        if (produced > 0)
            Upmix(decodeScratch_.data(), dst, produced, soundCh, engineCh);
    }

    if (produced == 0) {
        int32_t loops = params.loopCount.load(std::memory_order_relaxed);
        if (loops == 0) return 0;
        if (loops > 0) params.loopCount.store(loops - 1, std::memory_order_relaxed);
        libZFork::StreamTime t{};
        (void)decoder_->Seek(libZFork::SeekOrigin::Begin, t);
        if (soundCh == engineCh) {
            produced = decoder_->DecodeFloat(dst, frames);
        } else {
            decodeScratch_.resize(frames * soundCh);
            produced = decoder_->DecodeFloat(decodeScratch_.data(), frames);
            if (produced > 0)
                Upmix(decodeScratch_.data(), dst, produced, soundCh, engineCh);
        }
    }

    return produced;
}

// Linear-interpolation resampler for Streaming voices whose native sample
// rate differs from the engine's. Decoding is sequential (no random access
// like PreDecoded's pcm buffer), so we keep the last two decoded source
// frames and a fractional position between them, advancing by
// (srcRate/dstRate) per output frame and pulling one fresh source frame
// whenever that fraction crosses 1.0.
uint32_t Voice::DecodeStreamingResampled(float* dst, uint32_t frames, uint32_t soundCh,
                                         uint32_t engineCh, int srcRate, int dstRate) {
    const double ratio = static_cast<double>(srcRate) / dstRate;
    constexpr uint32_t kPullBatch = 256;

    auto flushPull = [&]() {
        resamplePullPos_   = 0;
        resamplePullCount_ = 0;
    };
    auto pullOne = [&](float* out) -> bool {
        if (resamplePullPos_ >= resamplePullCount_) {
            resamplePullBuf_.resize(static_cast<size_t>(kPullBatch) * soundCh);
            resamplePullCount_ = decoder_->DecodeFloat(resamplePullBuf_.data(), kPullBatch);
            resamplePullPos_   = 0;
            if (resamplePullCount_ == 0) return false;
        }
        std::memcpy(out,
                    resamplePullBuf_.data() + static_cast<size_t>(resamplePullPos_) * soundCh,
                    static_cast<size_t>(soundCh) * sizeof(float));
        ++resamplePullPos_;
        return true;
    };
    auto restart = [&]() -> bool {
        int32_t loops = params.loopCount.load(std::memory_order_relaxed);
        if (loops == 0) return false;
        if (loops > 0) params.loopCount.store(loops - 1, std::memory_order_relaxed);
        libZFork::StreamTime t{};
        flushPull();
        return decoder_->Seek(libZFork::SeekOrigin::Begin, t);
    };
    auto pullNextOrLoop = [&](float* out) -> bool {
        if (pullOne(out)) return true;
        return restart() && pullOne(out);
    };

    if (resampleCur_.size()  != soundCh) resampleCur_.assign(soundCh, 0.f);
    if (resampleNext_.size() != soundCh) resampleNext_.assign(soundCh, 0.f);

    if (!resamplePrimed_) {
        flushPull();
        if (!pullNextOrLoop(resampleCur_.data()))  return 0;
        if (!pullNextOrLoop(resampleNext_.data())) return 0;
        resampleFrac_   = 0.0;
        resamplePrimed_ = true;
    }

    uint32_t produced = 0;
    for (; produced < frames; ++produced) {
        const float t = static_cast<float>(resampleFrac_);
        if (soundCh == engineCh) {
            float* o = dst + static_cast<size_t>(produced) * engineCh;
            for (uint32_t c = 0; c < soundCh; ++c)
                o[c] = resampleCur_[c] + (resampleNext_[c] - resampleCur_[c]) * t;
        } else {
            decodeScratch_.resize(soundCh);
            for (uint32_t c = 0; c < soundCh; ++c)
                decodeScratch_[c] = resampleCur_[c] + (resampleNext_[c] - resampleCur_[c]) * t;
            Upmix(decodeScratch_.data(), dst + static_cast<size_t>(produced) * engineCh, 1, soundCh, engineCh);
        }

        resampleFrac_ += ratio;
        while (resampleFrac_ >= 1.0) {
            resampleCur_.swap(resampleNext_);
            if (!pullNextOrLoop(resampleNext_.data())) {
                resamplePrimed_ = false; // re-prime from scratch on the next call
                flushPull();
                return produced + 1;     // this frame was still written
            }
            resampleFrac_ -= 1.0;
        }
    }
    return produced;
}

uint32_t Voice::RenderWithTimeStretch(uint32_t frames, uint32_t engineCh,
                                      float tempo, float pitchSemitones) {
    if (!timeStretch_) {
        timeStretch_ = std::make_unique<libZFork::SoundTouchProc>();
        timeStretch_->Configure(static_cast<int>(engineSampleRate_),
                               static_cast<int>(engineCh));
    }

    if (std::fabs(tempo - appliedTempo_) > 0.001f
        || std::fabs(pitchSemitones - appliedPitch_) > 0.001f) {
        timeStretch_->SetRate(1.f);
        timeStretch_->SetTempo(tempo);
        timeStretch_->SetPitch(pitchSemitones);
        appliedTempo_  = tempo;
        appliedPitch_  = pitchSemitones;
    }

    const uint32_t soundCh = sound_ ? static_cast<uint32_t>(sound_->Channels()) : engineCh;
    uint32_t outFrames = 0;
    constexpr uint32_t kMaxPasses = 12;

    for (uint32_t pass = 0; pass < kMaxPasses && outFrames < frames; ++pass) {
        uint32_t got = timeStretch_->Receive(mixBuf.data() + outFrames * engineCh,
                                             frames - outFrames, static_cast<int>(engineCh));
        outFrames += got;
        if (outFrames >= frames) break;

        const uint32_t decoded = DecodeFrames(decodeScratch_.data(), frames, soundCh, engineCh);
        if (decoded == 0) {
            if (timeStretch_->AvailableFrames() == 0) break;
            continue;
        }

        ApplyVoiceEffects(std::span<float>(decodeScratch_.data(), decoded * engineCh), engineCh);
        timeStretch_->PutSamples(decodeScratch_.data(), decoded, static_cast<int>(engineCh));
    }

    if (outFrames == 0) return 0;

    if (outFrames < frames)
        std::memset(mixBuf.data() + outFrames * engineCh, 0,
                    (frames - outFrames) * engineCh * sizeof(float));

    if (sound_->Type() == SoundType::PreDecoded && markerCount_ > 0) {
        for (uint32_t m = 0; m < markerCount_; ++m) {
            auto& mk = markers_[m];
            if (!mk.fired && readCursor_ >= mk.framePos) {
                mk.fired = true;
                firedMarkerMask_ |= (1u << m);
            }
        }
    }

    return frames;
}

uint32_t Voice::RenderChunk(uint32_t frames) {
    if (!sound_) return 0;

    const uint32_t soundCh  = static_cast<uint32_t>(sound_->Channels());
    const uint32_t engineCh = engineChannels_;

    const float semitones = params.pitch.load(std::memory_order_relaxed);
    const float tempo     = params.tempo.load(std::memory_order_relaxed);
    if (std::fabs(tempo - 1.f) > 0.002f || std::fabs(semitones) > 0.002f)
        return RenderWithTimeStretch(frames, engineCh, tempo, semitones);

    if (timeStretch_) {
        timeStretch_->Clear();
        timeStretch_.reset();
        appliedTempo_ = 1.f;
        appliedPitch_ = 0.f;
    }

    const uint32_t produced = DecodeFrames(mixBuf.data(), frames, soundCh, engineCh);
    if (produced == 0) return 0;

    if (produced < frames)
        std::memset(mixBuf.data() + produced * engineCh, 0,
                    (frames - produced) * engineCh * sizeof(float));

    ApplyVoiceEffects(std::span<float>(mixBuf.data(), produced * engineCh), engineCh);

    if (sound_->Type() == SoundType::PreDecoded && markerCount_ > 0) {
        for (uint32_t m = 0; m < markerCount_; ++m) {
            auto& mk = markers_[m];
            if (!mk.fired && readCursor_ >= mk.framePos) {
                mk.fired = true;
                firedMarkerMask_ |= (1u << m);
            }
        }
    }

    return (sound_->Type() == SoundType::PreDecoded) ? frames : produced;
}

void Voice::Reset() {
    id           = 0;
    groupIndex   = 0;
    readCursor_  = 0;
    markerCount_ = 0;
    firedMarkerMask_ = 0;
    sound_.reset();
    decoder_.reset();
    for (auto& fx : voiceEffects_) fx.reset();
    for (auto& mk : markers_) mk = {};
    state.store(VoiceState::Free, std::memory_order_release);
    params.volume.store(1.f, std::memory_order_relaxed);
    params.pan.store(0.f, std::memory_order_relaxed);
    params.pitch.store(0.f, std::memory_order_relaxed);
    params.tempo.store(1.f, std::memory_order_relaxed);
    params.posX.store(0.f, std::memory_order_relaxed);
    params.posY.store(0.f, std::memory_order_relaxed);
    params.posZ.store(0.f, std::memory_order_relaxed);
    params.loopCount.store(0, std::memory_order_relaxed);
    params.priority.store(0, std::memory_order_relaxed);
    params.positional.store(false, std::memory_order_relaxed);
    timeStretch_.reset();
    appliedTempo_ = 1.f;
    appliedPitch_ = 0.f;
    std::fill(mixBuf.begin(), mixBuf.end(), 0.f);

    preciseCursor_  = 0.0;
    resampleFrac_   = 0.0;
    resamplePrimed_ = false;
    resampleCur_.clear();
    resampleNext_.clear();
    resamplePullBuf_.clear();
    resamplePullPos_   = 0;
    resamplePullCount_ = 0;
}

} // namespace libZFork::engine
