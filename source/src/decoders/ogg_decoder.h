#pragma once
#include "decoder_base.h"
#include <vorbis/vorbisfile.h>
#include <vector>
#include <memory>

namespace libZFork {

class OggDecoder final : public DecoderBase {
public:
    OggDecoder();
    ~OggDecoder() override;

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
    bool ReadInfo();

    OggVorbis_File vf_{};
    bool           opened_     = false;
    int            sampleRate_ = 44100;
    int            channels_   = 2;
    int            bitrate_    = 0;
    int            currentSection_ = 0;

    std::vector<uint8_t> memData_;
    size_t               memOffset_ = 0;

    // Custom IO callbacks for memory reading
    static size_t MemRead(void* ptr, size_t size, size_t nmemb, void* ds);
    static int    MemSeek(void* ds, ogg_int64_t offset, int whence);
    static int    MemClose(void* ds);
    static long   MemTell(void* ds);
};

} // namespace libZFork
