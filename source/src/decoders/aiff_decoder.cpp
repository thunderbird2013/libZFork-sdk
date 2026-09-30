#include "aiff_decoder.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace libZFork {

AiffDecoder::AiffDecoder()  = default;
AiffDecoder::~AiffDecoder() { CloseStream(); }

uint32_t AiffDecoder::ReadBytes(void* buf, uint32_t count) {
    if (useMemory_) {
        size_t rem = memData_.size() - memOffset_;
        size_t n   = std::min<size_t>(count, rem);
        std::memcpy(buf, memData_.data() + memOffset_, n);
        memOffset_ += n;
        return static_cast<uint32_t>(n);
    } else {
        DWORD read = 0;
        ReadFile(hFile_, buf, count, &read, nullptr);
        return read;
    }
}

bool AiffDecoder::SeekBytes(int64_t offset, int origin) {
    if (useMemory_) {
        size_t newPos = 0;
        if      (origin == FILE_BEGIN)   newPos = static_cast<size_t>(offset);
        else if (origin == FILE_CURRENT) newPos = memOffset_ + static_cast<size_t>(offset);
        else if (origin == FILE_END)     newPos = memData_.size() + static_cast<size_t>(offset);
        if (newPos > memData_.size()) return false;
        memOffset_ = newPos;
    } else {
        LARGE_INTEGER li; li.QuadPart = offset;
        if (!SetFilePointerEx(hFile_, li, nullptr, origin)) return false;
    }
    return true;
}

uint64_t AiffDecoder::TellBytes() const {
    if (useMemory_) return memOffset_;
    LARGE_INTEGER li{}; LARGE_INTEGER out{};
    SetFilePointerEx(hFile_, li, &out, FILE_CURRENT);
    return static_cast<uint64_t>(out.QuadPart);
}

bool AiffDecoder::OpenFile(const wchar_t* path) {
    CloseStream();
    hFile_ = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile_ == INVALID_HANDLE_VALUE) { SetError("Cannot open AIFF file"); return false; }
    useMemory_ = false;
    return ParseHeader();
}

bool AiffDecoder::OpenMemory(const uint8_t* data, uint32_t size) {
    CloseStream();
    memData_.assign(data, data + size);
    memOffset_ = 0;
    useMemory_ = true;
    return ParseHeader();
}

void AiffDecoder::CloseStream() {
    if (hFile_ != INVALID_HANDLE_VALUE) { CloseHandle(hFile_); hFile_ = INVALID_HANDLE_VALUE; }
    memData_.clear();
    memOffset_   = 0;
    framePos_    = 0;
    dataOffset_  = 0;
    dataSize_    = 0;
    totalFrames_ = 0;
}

// Big-endian read helpers
static uint16_t BE16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
static uint32_t BE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) <<  8) |  static_cast<uint32_t>(p[3]);
}
// Convert IEEE 754 80-bit extended to double (used for AIFF sample rate field)
static double Extended80(const uint8_t* p) {
    int      exp  = ((p[0] & 0x7F) << 8) | p[1];
    uint64_t mant = ((uint64_t)p[2] << 56) | ((uint64_t)p[3] << 48) |
                    ((uint64_t)p[4] << 40) | ((uint64_t)p[5] << 32) |
                    ((uint64_t)p[6] << 24) | ((uint64_t)p[7] << 16) |
                    ((uint64_t)p[8] <<  8) |  (uint64_t)p[9];
    double f = std::ldexp(static_cast<double>(mant), exp - 16383 - 63);
    return (p[0] & 0x80) ? -f : f;
}

bool AiffDecoder::ParseHeader() {
    uint8_t hdr[12];
    if (ReadBytes(hdr, 12) < 12)               { SetError("AIFF: short header"); return false; }
    if (std::memcmp(hdr, "FORM", 4) != 0)      { SetError("AIFF: not a FORM file"); return false; }
    if (std::memcmp(hdr + 8, "AIFF", 4) != 0 &&
        std::memcmp(hdr + 8, "AIFC", 4) != 0) { SetError("AIFF: not AIFF/AIFC type"); return false; }

    bool hasCOMM = false;
    while (true) {
        uint8_t chunkId[4], szBuf[4];
        if (ReadBytes(chunkId, 4) < 4 || ReadBytes(szBuf, 4) < 4) break;
        uint32_t chunkSize = BE32(szBuf);

        if (std::memcmp(chunkId, "COMM", 4) == 0) {
            uint8_t comm[26]{};
            uint32_t toRead = std::min(chunkSize, 26u);
            ReadBytes(comm, toRead);
            if (chunkSize > toRead) SeekBytes(chunkSize - toRead, FILE_CURRENT);
            channels_    = static_cast<int>(static_cast<int16_t>(BE16(comm)));
            totalFrames_ = BE32(comm + 2);
            bitDepth_    = static_cast<int>(static_cast<int16_t>(BE16(comm + 6)));
            sampleRate_  = static_cast<int>(Extended80(comm + 8));
            bitrate_     = sampleRate_ * channels_ * bitDepth_ / 1000;
            hasCOMM      = true;
        } else if (std::memcmp(chunkId, "SSND", 4) == 0) {
            uint8_t ssndHdr[8];
            ReadBytes(ssndHdr, 8);
            uint32_t ssndOffset = BE32(ssndHdr); // usually 0; blockSize at [4] is ignored
            dataOffset_ = static_cast<uint32_t>(TellBytes()) + ssndOffset;
            dataSize_   = chunkSize - 8;
            SeekBytes(static_cast<int64_t>(dataOffset_), FILE_BEGIN);
            break;
        } else {
            SeekBytes(chunkSize + (chunkSize & 1), FILE_CURRENT);
        }
    }

    if (!hasCOMM || dataOffset_ == 0) { SetError("AIFF: missing COMM or SSND chunk"); return false; }
    return true;
}

uint32_t AiffDecoder::DecodeFloat(float* outPCM, uint32_t maxFrames) {
    if (dataOffset_ == 0) return 0;
    uint64_t remaining = totalFrames_ - framePos_;
    uint32_t toRead    = static_cast<uint32_t>(std::min<uint64_t>(remaining, maxFrames));
    if (toRead == 0) return 0;

    int      bytesPerSample = bitDepth_ / 8;
    int      bytesPerFrame  = bytesPerSample * channels_;
    uint32_t byteCount      = toRead * static_cast<uint32_t>(bytesPerFrame);

    std::vector<uint8_t> raw(byteCount);
    uint32_t got       = ReadBytes(raw.data(), byteCount);
    uint32_t gotFrames = got / static_cast<uint32_t>(bytesPerFrame);

    for (uint32_t i = 0; i < gotFrames * static_cast<uint32_t>(channels_); ++i) {
        const uint8_t* src = raw.data() + i * static_cast<size_t>(bytesPerSample);
        float sample = 0.f;
        // AIFF samples are big-endian signed PCM
        switch (bitDepth_) {
            case 8:
                sample = static_cast<int8_t>(*src) / 128.0f;
                break;
            case 16: {
                int16_t v = static_cast<int16_t>((src[0] << 8) | src[1]);
                sample = v / 32768.0f;
                break;
            }
            case 24: {
                int32_t v = (static_cast<int32_t>(static_cast<int8_t>(src[0])) << 16) |
                            (src[1] << 8) | src[2];
                sample = v / 8388608.0f;
                break;
            }
            case 32: {
                int32_t v = (static_cast<int32_t>(src[0]) << 24) |
                            (static_cast<int32_t>(src[1]) << 16) |
                            (static_cast<int32_t>(src[2]) <<  8) |
                             static_cast<int32_t>(src[3]);
                sample = v / 2147483648.0f;
                break;
            }
        }
        outPCM[i] = sample;
    }
    framePos_ += gotFrames;
    return gotFrames;
}

bool AiffDecoder::Seek(SeekOrigin origin, const StreamTime& time) {
    if (dataOffset_ == 0) return false;
    int64_t targetFrame = 0;
    switch (origin) {
        case SeekOrigin::Begin:
            targetFrame = static_cast<int64_t>(TimeToSamplePos(time, sampleRate_));
            break;
        case SeekOrigin::Current:
            targetFrame = static_cast<int64_t>(framePos_) + static_cast<int64_t>(time.samples);
            break;
        case SeekOrigin::End:
            targetFrame = static_cast<int64_t>(totalFrames_);
            break;
    }
    targetFrame = std::clamp<int64_t>(targetFrame, 0, static_cast<int64_t>(totalFrames_));
    int64_t byteOff = static_cast<int64_t>(dataOffset_) + targetFrame * (bitDepth_ / 8) * channels_;
    if (!SeekBytes(byteOff, FILE_BEGIN)) return false;
    framePos_ = static_cast<uint64_t>(targetFrame);
    return true;
}

StreamTime AiffDecoder::GetPosition() const { return SamplesToTime(framePos_,    sampleRate_); }
StreamTime AiffDecoder::GetLength()   const { return SamplesToTime(totalFrames_,  sampleRate_); }

} // namespace libZFork
