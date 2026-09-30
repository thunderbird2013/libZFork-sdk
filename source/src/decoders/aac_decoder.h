#pragma once
#include "decoder_base.h"
#include <neaacdec.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace libZFork {

class AacDecoder final : public DecoderBase {
public:
    AacDecoder();
    ~AacDecoder() override;

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
    struct FrameIndexEntry {
        size_t   byteOffset;
        uint64_t samplePos;
    };

    bool InitFromBuffer(const uint8_t* data, uint32_t size);
    bool FindSyncWord(size_t& offset) const;
    bool ConfigureDecoder(NeAACDecHandle handle) const;
    bool ReinitDecoderAt(size_t syncOffset);
    uint32_t CalibrateSamplesPerFrame();
    bool BuildFrameIndexFast(uint32_t samplesPerFrame);
    uint32_t DiscardFrames(uint64_t frameCount);

    NeAACDecHandle              hDecoder_     = nullptr;
    std::vector<uint8_t>        fileData_;
    size_t                      dataOffset_   = 0;
    size_t                      syncOffset_     = 0;
    int                         sampleRate_   = 44100;
    int                         channels_     = 2;
    int                         bitrate_      = 0;
    uint64_t                    position_     = 0;
    uint64_t                    totalSamples_ = 0;
    uint32_t                    samplesPerFrame_ = 1024;

    std::vector<FrameIndexEntry> frameIndex_;
    std::vector<float>           leftover_;
    size_t                       leftoverPos_ = 0;
};

} // namespace libZFork
