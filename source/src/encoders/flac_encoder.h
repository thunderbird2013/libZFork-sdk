#pragma once
#include "encoder_base.h"
#include <FLAC/stream_encoder.h>
#include <vector>

namespace libZFork {

class FlacEncoder final : public EncoderBase {
public:
    FlacEncoder() = default;
    ~FlacEncoder() override { Close(); }

    bool Open(const wchar_t* path, int sampleRate, int channels) override;
    bool Write(std::span<const float> pcm) override;
    void Close() override;

private:
    FLAC__StreamEncoder* enc_      = nullptr;
    int                  channels_ = 2;
    std::vector<FLAC__int32> intBuf_;
};

} // namespace libZFork
