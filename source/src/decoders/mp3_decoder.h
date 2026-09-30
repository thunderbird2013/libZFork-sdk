#pragma once
#include "decoder_base.h"
#include <mpg123.h>
#include <memory>
#include <vector>

namespace libZFork {

class Mp3Decoder final : public DecoderBase {
public:
    Mp3Decoder();
    ~Mp3Decoder() override;

    bool OpenFile(const wchar_t* path) override;
    bool OpenMemory(const uint8_t* data, uint32_t size) override;
    void CloseStream() override;

    uint32_t DecodeFloat(float* outPCM, uint32_t maxFrames) override;
    bool     Seek(SeekOrigin origin, const StreamTime& time) override;
    StreamTime GetPosition() const override;
    StreamTime GetLength()   const override;

    int  SampleRate()  const override { return sampleRate_;  }
    int  Channels()    const override { return channels_;    }
    int  Bitrate()     const override { return bitrate_;     }
    bool IsVBR()       const override { return isVBR_;       }

private:
    bool Init();
    bool ConfigureHandle();

    mpg123_handle*        mh_         = nullptr;
    int                   sampleRate_ = 44100;
    int                   channels_   = 2;
    int                   bitrate_    = 0;
    bool                  isVBR_      = false;
    std::vector<uint8_t>  memData_;
    size_t                memOffset_  = 0;

    static bool s_libInitialized;
};

} // namespace libZFork
