#pragma once
#include "decoder_base.h"
#include <neaacdec.h>
#include <vector>

struct Mp4ReadContext;

namespace libZFork {

class M4aDecoder final : public DecoderBase {
public:
    M4aDecoder();
    ~M4aDecoder() override;

    bool OpenFile(const wchar_t* path) override;
    bool OpenMemory(const uint8_t* data, uint32_t size) override;
    void CloseStream() override;

    uint32_t   DecodeFloat(float* outPCM, uint32_t maxFrames) override;
    bool       Seek(SeekOrigin origin, const StreamTime& time) override;
    StreamTime GetPosition() const override;
    StreamTime GetLength()   const override;

    int  SampleRate()  const override { return sampleRate_; }
    int  Channels()    const override { return channels_;   }
    int  Bitrate()     const override { return bitrate_;    }
    bool IsVBR()       const override { return true;        }

private:
    bool InitDecoder();

    Mp4ReadContext*     mp4_        = nullptr;
    NeAACDecHandle      hDecoder_   = nullptr;
    int                 sampleRate_ = 44100;
    int                 channels_   = 2;
    int                 bitrate_    = 0;
    uint64_t            position_   = 0;
    uint64_t            totalSamples_ = 0;

    std::vector<float>  leftover_;
    size_t              leftoverPos_ = 0;
};

} // namespace libZFork
