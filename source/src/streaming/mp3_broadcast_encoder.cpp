#include "mp3_broadcast_encoder.h"
#include <algorithm>

namespace libZFork {

Mp3BroadcastEncoder::~Mp3BroadcastEncoder() {
    Close();
}

bool Mp3BroadcastEncoder::Open(int sampleRate, int channels, int bitrateKbps, int stereoMode) {
    Close();
    if (channels <= 0 || channels > 2 || sampleRate <= 0) {
        SetError("invalid encoder format");
        return false;
    }
    inputChannels_ = channels;
    channels_ = channels;
    if (stereoMode == static_cast<int>(BroadcastStereoMode::Mono)) {
        channels_ = 1;
    }
    gfp_ = lame_init();
    if (!gfp_) {
        SetError("lame_init failed");
        return false;
    }
    lame_set_in_samplerate(gfp_, sampleRate);
    lame_set_num_channels(gfp_, channels_);
    lame_set_out_samplerate(gfp_, sampleRate);
    switch (static_cast<BroadcastStereoMode>(stereoMode)) {
    case BroadcastStereoMode::Stereo:
        lame_set_mode(gfp_, STEREO);
        break;
    case BroadcastStereoMode::JointStereo:
        lame_set_mode(gfp_, JOINT_STEREO);
        break;
    case BroadcastStereoMode::Mono:
        lame_set_mode(gfp_, MONO);
        break;
    case BroadcastStereoMode::Auto:
    default:
        if (channels_ == 1) lame_set_mode(gfp_, MONO);
        break;
    }
    if (bitrateKbps > 0) {
        lame_set_brate(gfp_, bitrateKbps);
        lame_set_VBR(gfp_, vbr_off);
    } else {
        lame_set_VBR(gfp_, vbr_default);
        lame_set_VBR_q(gfp_, 4);
    }
    lame_set_quality(gfp_, 2);
    if (lame_init_params(gfp_) < 0) {
        SetError("lame_init_params failed");
        Close();
        return false;
    }
    mp3buf_.resize(8192);
    return true;
}

bool Mp3BroadcastEncoder::Encode(std::span<const float> interleavedPcm, const ByteSink& sink) {
    if (!gfp_) {
        SetError("encoder not open");
        return false;
    }
    const size_t frames = interleavedPcm.size() / static_cast<size_t>(std::max(1, inputChannels_));
    if (frames == 0) return true;

    std::vector<float> left(frames), right(frames);
    if (inputChannels_ == 2) {
        if (channels_ == 1) {
            for (size_t i = 0; i < frames; ++i) {
                left[i] = (interleavedPcm[i * 2] + interleavedPcm[i * 2 + 1]) * 0.5f;
                right[i] = left[i];
            }
        } else {
            for (size_t i = 0; i < frames; ++i) {
                left[i] = interleavedPcm[i * 2];
                right[i] = interleavedPcm[i * 2 + 1];
            }
        }
    } else {
        for (size_t i = 0; i < frames; ++i) {
            left[i] = interleavedPcm[i];
            right[i] = interleavedPcm[i];
        }
    }

    const int needed = static_cast<int>(frames * 5 / 4 + 7200);
    mp3buf_.resize(static_cast<size_t>(needed));
    const int bytes = lame_encode_buffer_ieee_float(gfp_, left.data(), right.data(),
                                                    static_cast<int>(frames),
                                                    mp3buf_.data(), needed);
    if (bytes < 0) {
        SetError("lame encode failed");
        return false;
    }
    if (bytes == 0) return true;
    return sink(std::span<const uint8_t>(mp3buf_.data(), static_cast<size_t>(bytes)));
}

bool Mp3BroadcastEncoder::Flush(const ByteSink& sink) {
    if (!gfp_) return true;
    mp3buf_.resize(7200);
    const int bytes = lame_encode_flush(gfp_, mp3buf_.data(), static_cast<int>(mp3buf_.size()));
    if (bytes < 0) {
        SetError("lame flush failed");
        return false;
    }
    if (bytes == 0) return true;
    return sink(std::span<const uint8_t>(mp3buf_.data(), static_cast<size_t>(bytes)));
}

void Mp3BroadcastEncoder::Close() {
    if (gfp_) {
        lame_close(gfp_);
        gfp_ = nullptr;
    }
    mp3buf_.clear();
}

} // namespace libZFork

