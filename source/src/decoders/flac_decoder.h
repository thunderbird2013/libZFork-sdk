#pragma once
#include "decoder_base.h"
#include <FLAC/stream_decoder.h>
#include <cstdint>
#include <mutex>
#include <vector>

namespace libZFork {

class FlacDecoder final : public DecoderBase {
public:
    FlacDecoder();
    ~FlacDecoder() override;

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
    bool InitDecoder();
    void ClearPcmBuffer();

    // FLAC callbacks
    static FLAC__StreamDecoderWriteStatus WriteCallback(
        const FLAC__StreamDecoder*, const FLAC__Frame*, const FLAC__int32* const[], void*);
    static void MetadataCallback(
        const FLAC__StreamDecoder*, const FLAC__StreamMetadata*, void*);
    static void ErrorCallback(
        const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void*);
    // Memory feed callbacks
    static FLAC__StreamDecoderReadStatus MemReadCb(
        const FLAC__StreamDecoder*, FLAC__byte[], size_t*, void*);
    static FLAC__StreamDecoderSeekStatus MemSeekCb(
        const FLAC__StreamDecoder*, FLAC__uint64, void*);
    static FLAC__StreamDecoderTellStatus MemTellCb(
        const FLAC__StreamDecoder*, FLAC__uint64*, void*);
    static FLAC__StreamDecoderLengthStatus MemLengthCb(
        const FLAC__StreamDecoder*, FLAC__uint64*, void*);
    static FLAC__bool MemEofCb(const FLAC__StreamDecoder*, void*);

    FLAC__StreamDecoder* decoder_    = nullptr;
    int                  sampleRate_ = 44100;
    int                  channels_   = 2;
    int                  bitDepth_   = 16;
    int                  bitrate_    = 0;
    uint64_t             totalSamples_ = 0;
    uint64_t             position_   = 0;

    // When set, WriteCallback discards samples before this absolute sample
    // (libFLAC delivers the whole frame that contains the seek target).
    uint64_t             pendingSeekSample_ = UINT64_MAX;

    std::vector<float>   floatBuf_;
    size_t               bufRead_    = 0;
    float                scaleFactor_= 1.0f / 32768.0f;

    std::vector<uint8_t> memData_;
    size_t               memOffset_  = 0;

    // DecodeThread and UI Seek() share this decoder concurrently.
    mutable std::mutex   mtx_;
};

} // namespace libZFork
