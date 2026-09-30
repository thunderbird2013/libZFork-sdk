#include "m4a_decoder.h"
#include "../third_party/mp4read/mp4read.h"
#include "utils/win_compat.h"
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace libZFork {

M4aDecoder::M4aDecoder()  = default;
M4aDecoder::~M4aDecoder() { CloseStream(); }

bool M4aDecoder::InitDecoder() {
    const mp4config_t* cfg = mp4read_config(mp4_);
    if (!cfg || cfg->asc.size == 0 || cfg->frame.nsamples == 0) {
        SetError("No AAC track in MP4 container");
        return false;
    }

    hDecoder_ = NeAACDecOpen();
    if (!hDecoder_) { SetError("NeAACDecOpen failed"); return false; }

    NeAACDecConfigurationPtr decCfg = NeAACDecGetCurrentConfiguration(hDecoder_);
    decCfg->outputFormat = FAAD_FMT_FLOAT;
    decCfg->downMatrix   = 0;
    NeAACDecSetConfiguration(hDecoder_, decCfg);

    unsigned long sr = 0;
    uint8_t ch = 0;
    if (NeAACDecInit2(hDecoder_,
                      const_cast<unsigned char*>(cfg->asc.buf),
                      static_cast<unsigned long>(cfg->asc.size),
                      &sr, &ch) < 0) {
        SetError("NeAACDecInit2 failed");
        return false;
    }

    // mdhd timescale can be the AAC core rate (e.g. 22050 for HE-AAC) while faad2
    // outputs SBR-upsampled PCM (e.g. 48000). Always use the decoder output rate.
    const uint32_t mediaRate = cfg->samplerate;
    sampleRate_   = static_cast<int>(sr);
    channels_     = ch ? static_cast<int>(ch)
                       : static_cast<int>(cfg->channels ? cfg->channels : 2);
    if (mediaRate && mediaRate != sr) {
        totalSamples_ = (static_cast<uint64_t>(cfg->samples) * sr) / mediaRate;
    } else {
        totalSamples_ = cfg->samples;
    }
    bitrate_       = static_cast<int>(cfg->bitrateavg);
    position_      = 0;
    leftover_.clear();
    leftoverPos_   = 0;
    return true;
}

bool M4aDecoder::OpenFile(const wchar_t* path) {
    CloseStream();
    if (!path) { SetError("null path"); return false; }

    const std::string nativePath = std::filesystem::path(path).string();

    mp4_ = mp4read_create();
    if (!mp4_) { SetError("mp4read_create failed"); return false; }

    if (mp4read_open(mp4_, nativePath.c_str()) != 0) {
        SetError("Cannot open M4A/MP4 file");
        mp4read_destroy(mp4_);
        mp4_ = nullptr;
        return false;
    }

    if (!InitDecoder()) {
        CloseStream();
        return false;
    }
    return true;
}

bool M4aDecoder::OpenMemory(const uint8_t*, uint32_t) {
    SetError("M4A from memory not supported");
    return false;
}

void M4aDecoder::CloseStream() {
    if (hDecoder_) { NeAACDecClose(hDecoder_); hDecoder_ = nullptr; }
    if (mp4_) { mp4read_destroy(mp4_); mp4_ = nullptr; }
    leftover_.clear();
    leftoverPos_ = 0;
    position_    = 0;
    totalSamples_ = 0;
}

uint32_t M4aDecoder::DecodeFloat(float* outPCM, uint32_t maxFrames) {
    if (!hDecoder_ || !mp4_ || !outPCM || maxFrames == 0) return 0;
    const int ch = std::max(1, channels_);

    uint32_t framesDecoded = 0;
    float*   dst           = outPCM;
    uint32_t remaining     = maxFrames;

    while (remaining > 0) {
        if (leftoverPos_ < leftover_.size()) {
            const size_t avail   = leftover_.size() - leftoverPos_;
            const size_t consume = std::min(avail, static_cast<size_t>(remaining) * static_cast<size_t>(ch));
            std::copy(leftover_.begin() + leftoverPos_,
                      leftover_.begin() + leftoverPos_ + consume, dst);
            dst           += consume;
            leftoverPos_  += consume;
            framesDecoded += static_cast<uint32_t>(consume / static_cast<size_t>(ch));
            remaining     -= static_cast<uint32_t>(consume / static_cast<size_t>(ch));
            if (leftoverPos_ >= leftover_.size()) {
                leftover_.clear();
                leftoverPos_ = 0;
            }
            continue;
        }

        const mp4config_t* cfg = mp4read_config(mp4_);
        if (!cfg || cfg->frame.current >= cfg->frame.nsamples)
            break;

        if (mp4read_frame(mp4_) != 0)
            break;

        cfg = mp4read_config(mp4_);
        if (!cfg || cfg->bitbuf.size == 0)
            break;

        NeAACDecFrameInfo fi{};
        void* samples = NeAACDecDecode(hDecoder_, &fi,
                                       cfg->bitbuf.data,
                                       cfg->bitbuf.size);
        if (fi.error || !samples || fi.channels == 0)
            break;

        if (fi.samplerate && static_cast<int>(fi.samplerate) != sampleRate_) {
            sampleRate_ = static_cast<int>(fi.samplerate);
        }
        if (static_cast<int>(fi.channels) != channels_)
            channels_ = static_cast<int>(fi.channels);
        const int frameCh = std::max(1, static_cast<int>(fi.channels));

        position_ += fi.samples / static_cast<unsigned>(frameCh);

        auto* fsamples = static_cast<float*>(samples);
        const size_t total   = fi.samples;
        const size_t consume = std::min(total, static_cast<size_t>(remaining) * static_cast<size_t>(frameCh));
        std::copy(fsamples, fsamples + consume, dst);
        dst           += consume;
        framesDecoded += static_cast<uint32_t>(consume / static_cast<size_t>(frameCh));
        remaining     -= static_cast<uint32_t>(consume / static_cast<size_t>(frameCh));
        if (consume < total) {
            leftover_.assign(fsamples + consume, fsamples + total);
            leftoverPos_ = 0;
        }
    }

    return framesDecoded;
}

bool M4aDecoder::Seek(SeekOrigin origin, const StreamTime& time) {
    if (!mp4_ || !hDecoder_) return false;

    const mp4config_t* cfg = mp4read_config(mp4_);
    if (!cfg || cfg->frame.nsamples == 0 || sampleRate_ <= 0)
        return false;

    uint64_t targetSamples = TimeToSamplePos(time, sampleRate_);

    if (origin == SeekOrigin::End && totalSamples_ > 0) {
        if (targetSamples > totalSamples_) targetSamples = 0;
        targetSamples = totalSamples_ - targetSamples;
    } else if (origin == SeekOrigin::Current) {
        targetSamples = position_ + targetSamples;
    }

    if (totalSamples_ > 0 && targetSamples > totalSamples_)
        targetSamples = totalSamples_;

    uint32_t frameIdx = 0;
    if (totalSamples_ > 0 && cfg->frame.nsamples > 0) {
        // Map sample position → AAC frame index; clamp to last valid frame
        // (mp4read_seek rejects framenum >= nsamples).
        const uint64_t idx64 = (targetSamples * cfg->frame.nsamples) / totalSamples_;
        frameIdx = static_cast<uint32_t>(std::min<uint64_t>(idx64, cfg->frame.nsamples - 1));
    }

    if (mp4read_seek(mp4_, frameIdx) != 0)
        return false;

    NeAACDecClose(hDecoder_);
    hDecoder_ = nullptr;
    if (!InitDecoder()) return false;

    position_ = (totalSamples_ * frameIdx) / cfg->frame.nsamples;
    leftover_.clear();
    leftoverPos_ = 0;
    return true;
}

StreamTime M4aDecoder::GetPosition() const { return SamplesToTime(position_, sampleRate_); }
StreamTime M4aDecoder::GetLength()   const { return SamplesToTime(totalSamples_, sampleRate_); }

} // namespace libZFork
