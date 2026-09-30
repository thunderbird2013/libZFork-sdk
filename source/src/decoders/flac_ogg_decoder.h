#pragma once
#include "decoder_base.h"
#include <FLAC/stream_decoder.h>
#include <cstdint>
#include <mutex>
#include <vector>

namespace libZFork {

// Decodes FLAC audio encapsulated in an OGG container (.oga, .ogg with FLAC).
// Uses FLAC__stream_decoder_init_ogg_* to handle the OGG framing transparently.
class FlacOggDecoder final : public DecoderBase {
public:
    FlacOggDecoder();
    ~FlacOggDecoder() override;

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

    // True once the FLAC STREAMINFO metadata block was actually parsed.
    // sampleRate_/channels_ default to plausible-looking (44100/2) values
    // even when the file isn't really FLAC (e.g. a plain Vorbis .ogg), so
    // those can't be used to detect a failed open — this flag can.
    [[nodiscard]] bool SawStreamInfo() const { return sawStreamInfo_; }

private:
    bool InitDecoder();
    void ClearPcmBuffer();

    static FLAC__StreamDecoderWriteStatus WriteCallback(
        const FLAC__StreamDecoder*, const FLAC__Frame*, const FLAC__int32* const[], void*);
    static void MetadataCallback(
        const FLAC__StreamDecoder*, const FLAC__StreamMetadata*, void*);
    static void ErrorCallback(
        const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void*);

    static FLAC__StreamDecoderReadStatus   MemReadCb(
        const FLAC__StreamDecoder*, FLAC__byte[], size_t*, void*);
    static FLAC__StreamDecoderSeekStatus   MemSeekCb(
        const FLAC__StreamDecoder*, FLAC__uint64, void*);
    static FLAC__StreamDecoderTellStatus   MemTellCb(
        const FLAC__StreamDecoder*, FLAC__uint64*, void*);
    static FLAC__StreamDecoderLengthStatus MemLengthCb(
        const FLAC__StreamDecoder*, FLAC__uint64*, void*);
    static FLAC__bool MemEofCb(const FLAC__StreamDecoder*, void*);

    FLAC__StreamDecoder* decoder_      = nullptr;
    int                  sampleRate_   = 44100;
    int                  channels_     = 2;
    int                  bitDepth_     = 16;
    int                  bitrate_      = 0;
    uint64_t             totalSamples_ = 0;
    uint64_t             position_     = 0;
    bool                 sawStreamInfo_ = false;
    uint64_t             pendingSeekSample_ = UINT64_MAX;

    std::vector<float>   floatBuf_;
    size_t               bufRead_      = 0;
    float                scaleFactor_  = 1.0f / 32768.0f;

    std::vector<uint8_t> memData_;
    size_t               memOffset_    = 0;

    mutable std::mutex   mtx_;
};

} // namespace libZFork
