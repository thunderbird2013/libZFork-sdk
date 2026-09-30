#include "opus_decoder.h"
#include <filesystem>
#include <algorithm>

namespace libZFork {

OpusDecoder::OpusDecoder()  = default;
OpusDecoder::~OpusDecoder() { CloseStream(); }

bool OpusDecoder::OpenFile(const wchar_t* path) {
    CloseStream();
    int err = 0;
    std::filesystem::path p(path);
    auto utf8 = p.string();
    of_ = op_open_file(utf8.c_str(), &err);
    if (!of_) { SetError("Opus: op_open_file failed"); return false; }
    channels_     = op_channel_count(of_, -1);
    bitrate_      = static_cast<int>(op_bitrate(of_, -1) / 1000);
    totalSamples_ = op_pcm_total(of_, -1);
    return true;
}

bool OpusDecoder::OpenMemory(const uint8_t* data, uint32_t size) {
    CloseStream();
    int err = 0;
    of_ = op_open_memory(data, static_cast<size_t>(size), &err);
    if (!of_) { SetError("Opus: op_open_memory failed"); return false; }
    channels_     = op_channel_count(of_, -1);
    bitrate_      = static_cast<int>(op_bitrate(of_, -1) / 1000);
    totalSamples_ = op_pcm_total(of_, -1);
    return true;
}

void OpusDecoder::CloseStream() {
    if (of_) { op_free(of_); of_ = nullptr; }
    position_     = 0;
    totalSamples_ = 0;
}

uint32_t OpusDecoder::DecodeFloat(float* outPCM, uint32_t maxFrames) {
    if (!of_) return 0;
    uint32_t framesDecoded = 0;
    float*   dst           = outPCM;
    uint32_t remaining     = maxFrames;

    while (remaining > 0) {
        // op_read_float returns number of samples per channel decoded
        int ret = op_read_float(of_, dst, static_cast<int>(remaining) * channels_, nullptr);
        if (ret <= 0) break;
        uint32_t frames = static_cast<uint32_t>(ret);
        dst            += frames * channels_;
        framesDecoded  += frames;
        remaining      -= frames;
        position_      += frames;
    }
    return framesDecoded;
}

bool OpusDecoder::Seek(SeekOrigin origin, const StreamTime& time) {
    if (!of_) return false;
    int64_t targetSample = 0;
    switch (origin) {
        case SeekOrigin::Begin:
            targetSample = static_cast<int64_t>(TimeToSamplePos(time, 48000));
            break;
        case SeekOrigin::Current:
            targetSample = position_ + static_cast<int64_t>(time.samples);
            break;
        case SeekOrigin::End:
            targetSample = totalSamples_;
            break;
    }
    targetSample = std::max<int64_t>(0, std::min(targetSample, totalSamples_));
    if (op_pcm_seek(of_, targetSample) != 0) return false;
    position_ = targetSample;
    return true;
}

StreamTime OpusDecoder::GetPosition() const {
    return SamplesToTime(static_cast<uint64_t>(std::max<int64_t>(0, position_)),     48000);
}
StreamTime OpusDecoder::GetLength() const {
    return SamplesToTime(static_cast<uint64_t>(std::max<int64_t>(0, totalSamples_)), 48000);
}

} // namespace libZFork
