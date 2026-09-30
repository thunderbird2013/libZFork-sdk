#pragma once
#include "decoder_base.h"
#include "utils/win_compat.h"
#include <vector>

namespace libZFork {

class AiffDecoder final : public DecoderBase {
public:
    AiffDecoder();
    ~AiffDecoder() override;

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
    bool IsVBR()       const override { return false;       }

private:
    bool ParseHeader();

    HANDLE              hFile_       = INVALID_HANDLE_VALUE;
    std::vector<uint8_t> memData_;
    size_t              memOffset_   = 0;
    bool                useMemory_   = false;

    int      sampleRate_  = 44100;
    int      channels_    = 2;
    int      bitDepth_    = 16;
    int      bitrate_     = 0;
    uint32_t dataOffset_  = 0;
    uint32_t dataSize_    = 0;
    uint64_t framePos_    = 0;
    uint64_t totalFrames_ = 0;

    uint32_t ReadBytes(void* buf, uint32_t count);
    bool     SeekBytes(int64_t offset, int origin);
    uint64_t TellBytes() const;
};

} // namespace libZFork
