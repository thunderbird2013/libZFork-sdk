#include "flac_encoder.h"
#include <filesystem>
#include <algorithm>
#include <cmath>

namespace libZFork {

bool FlacEncoder::Open(const wchar_t* path, int sampleRate, int channels) {
    Close();
    channels_ = channels;
    enc_ = FLAC__stream_encoder_new();
    if (!enc_) { SetError("FLAC__stream_encoder_new failed"); return false; }

    FLAC__stream_encoder_set_channels(enc_, channels);
    FLAC__stream_encoder_set_bits_per_sample(enc_, 16);
    FLAC__stream_encoder_set_sample_rate(enc_, sampleRate);
    FLAC__stream_encoder_set_compression_level(enc_, 5);

    std::filesystem::path p(path);
    auto utf8 = p.string();
    if (FLAC__stream_encoder_init_file(enc_, utf8.c_str(), nullptr, nullptr) !=
        FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
        SetError("FLAC init_file failed");
        return false;
    }
    return true;
}

bool FlacEncoder::Write(std::span<const float> pcm) {
    if (!enc_) return false;
    size_t frames = pcm.size() / channels_;
    intBuf_.resize(pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) {
        float s    = std::clamp(pcm[i], -1.0f, 1.0f);
        intBuf_[i] = static_cast<FLAC__int32>(s * 32767.0f);
    }
    return FLAC__stream_encoder_process_interleaved(enc_, intBuf_.data(),
                                                    static_cast<uint32_t>(frames)) != 0;
}

void FlacEncoder::Close() {
    if (enc_) {
        FLAC__stream_encoder_finish(enc_);
        FLAC__stream_encoder_delete(enc_);
        enc_ = nullptr;
    }
}

} // namespace libZFork
