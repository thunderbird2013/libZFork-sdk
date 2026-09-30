#pragma once
#include "encoder_base.h"
#include "utils/win_compat.h"

namespace libZFork {

class WavEncoder final : public EncoderBase {
public:
    WavEncoder() = default;
    ~WavEncoder() override { Close(); }

    bool Open(const wchar_t* path, int sampleRate, int channels) override;
    bool Write(std::span<const float> pcm) override;
    void Close() override;

private:
    HANDLE   hFile_      = INVALID_HANDLE_VALUE;
    uint32_t dataBytes_  = 0;
    int      sampleRate_ = 44100;
    int      channels_   = 2;
};

} // namespace libZFork
