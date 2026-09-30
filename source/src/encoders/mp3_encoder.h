#pragma once
#include "encoder_base.h"
#include <lame/lame.h>
#include "utils/win_compat.h"
#include <vector>

namespace libZFork {

class Mp3Encoder final : public EncoderBase {
public:
    Mp3Encoder() = default;
    ~Mp3Encoder() override { Close(); }

    bool Open(const wchar_t* path, int sampleRate, int channels) override;
    bool Write(std::span<const float> pcm) override;
    void Close() override;

private:
    lame_global_flags* gfp_   = nullptr;
    HANDLE             hFile_ = INVALID_HANDLE_VALUE;
    int                channels_ = 2;
    std::vector<uint8_t> mp3buf_;
};

} // namespace libZFork
