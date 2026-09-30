#pragma once
#include "decoder_base.h"
#include <vector>
#include <string>

namespace libZFork {

class CdaDecoder final : public DecoderBase {
public:
    CdaDecoder();
    ~CdaDecoder() override;

    bool OpenFile(const wchar_t* path) override;
    bool OpenMemory(const uint8_t* data, uint32_t size) override;
    void CloseStream() override;

    uint32_t   DecodeFloat(float* outPCM, uint32_t maxFrames) override;
    bool       Seek(SeekOrigin origin, const StreamTime& time) override;
    StreamTime GetPosition() const override;
    StreamTime GetLength()   const override;

    int  SampleRate()  const override { return 44100; }
    int  Channels()    const override { return 2; }
    int  Bitrate()     const override { return 1411; }
    bool IsVBR()       const override { return false; }

private:
#ifdef _WIN32
    bool ParseTrackPath(const wchar_t* path, wchar_t& outDrive, int& outTrack) const;
    bool OpenTrack(wchar_t driveLetter, int trackNumber);
    bool ReadSectors(uint32_t lba, uint32_t sectorCount, std::vector<uint8_t>& outRaw) const;
#endif

    void*    hCdrom_ = nullptr;
    uint32_t trackStartLba_ = 0;
    uint32_t trackEndLba_ = 0;
    uint64_t framePos_ = 0;
    uint64_t totalFrames_ = 0;
};

} // namespace libZFork
