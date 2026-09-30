#pragma once
#include "decoder_base.h"
#include <opusfile.h>
#include <vector>

namespace libZFork {

// Opus decoder via libopusfile.
// Opus always decodes at 48 kHz; channel count is from the stream header.
class OpusDecoder final : public DecoderBase {
public:
    OpusDecoder();
    ~OpusDecoder() override;

    bool OpenFile(const wchar_t* path) override;
    bool OpenMemory(const uint8_t* data, uint32_t size) override;
    void CloseStream() override;

    uint32_t   DecodeFloat(float* outPCM, uint32_t maxFrames) override;
    bool       Seek(SeekOrigin origin, const StreamTime& time) override;
    StreamTime GetPosition() const override;
    StreamTime GetLength()   const override;

    int  SampleRate()  const override { return 48000;     }
    int  Channels()    const override { return channels_;  }
    int  Bitrate()     const override { return bitrate_;   }
    bool IsVBR()       const override { return true;       }

private:
    OggOpusFile* of_          = nullptr;
    int          channels_    = 2;
    int          bitrate_     = 0;
    int64_t      totalSamples_= 0;
    int64_t      position_    = 0;
};

} // namespace libZFork
