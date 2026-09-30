#pragma once
#include "decoder_base.h"
#include "core/queue.h"
#include <vector>
#include <atomic>

namespace libZFork {

// Decoder for raw PCM — supports both file-based and push-mode streaming.
class PcmDecoder final : public DecoderBase {
public:
    PcmDecoder();
    ~PcmDecoder() override;

    void SetPCMFormat(const PCMFormat& fmt);

    bool OpenFile(const wchar_t* path) override;
    bool OpenMemory(const uint8_t* data, uint32_t size) override;
    void CloseStream() override;

    // Push raw PCM bytes (for streaming mode)
    bool Push(const void* data, uint32_t byteCount);

    uint32_t   DecodeFloat(float* outPCM, uint32_t maxFrames) override;
    bool       Seek(SeekOrigin origin, const StreamTime& time) override;
    StreamTime GetPosition() const override;
    StreamTime GetLength()   const override;

    int  SampleRate()  const override { return fmt_.sampleRate; }
    int  Channels()    const override { return fmt_.channels;   }
    int  Bitrate()     const override { return fmt_.sampleRate * fmt_.channels * fmt_.bitsPerSample / 1000; }
    bool IsVBR()       const override { return false; }

private:
    PCMFormat            fmt_      { 44100, 2, 16, false };
    std::vector<uint8_t> fileData_;
    size_t               filePos_  = 0;
    uint64_t             frames_   = 0;
    AudioQueue           queue_;
    bool                 streaming_= false;

    float SampleToFloat(const uint8_t* src) const;
};

} // namespace libZFork
