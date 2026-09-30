#include "wav_decoder.h"
#include <algorithm>
#include <cstring>

namespace libZFork {

WavDecoder::WavDecoder()  = default;
WavDecoder::~WavDecoder() { CloseStream(); }

uint32_t WavDecoder::ReadBytes(void* buf, uint32_t count) {
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

bool WavDecoder::SeekBytes(int64_t offset, int origin) {
    if (useMemory_) {
        size_t newPos = 0;
        if      (origin == FILE_BEGIN)    newPos = static_cast<size_t>(offset);
        else if (origin == FILE_CURRENT)  newPos = memOffset_ + static_cast<size_t>(offset);
        else if (origin == FILE_END)      newPos = memData_.size() + static_cast<size_t>(offset);
        if (newPos > memData_.size()) return false;
        memOffset_ = newPos;
    } else {
        LARGE_INTEGER li; li.QuadPart = offset;
        if (!SetFilePointerEx(hFile_, li, nullptr, origin))
            return false;
    }
    return true;
}

uint64_t WavDecoder::TellBytes() const {
    if (useMemory_) return memOffset_;
    LARGE_INTEGER li{}; LARGE_INTEGER out{};
    SetFilePointerEx(hFile_, li, &out, FILE_CURRENT);
    return static_cast<uint64_t>(out.QuadPart);
}

bool WavDecoder::OpenFile(const wchar_t* path) {
    CloseStream();
    hFile_ = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile_ == INVALID_HANDLE_VALUE) { SetError("Cannot open WAV file"); return false; }
    useMemory_ = false;
    return ParseHeader();
}

bool WavDecoder::OpenMemory(const uint8_t* data, uint32_t size) {
    CloseStream();
    memData_.assign(data, data + size);
    memOffset_ = 0;
    useMemory_ = true;
    return ParseHeader();
}

void WavDecoder::CloseStream() {
    if (hFile_ != INVALID_HANDLE_VALUE) { CloseHandle(hFile_); hFile_ = INVALID_HANDLE_VALUE; }
    memData_.clear();
    memOffset_   = 0;
    framePos_    = 0;
    dataOffset_  = 0;
    dataSize_    = 0;
    totalFrames_ = 0;
}

bool WavDecoder::ParseHeader() {
    uint8_t buf[12];
    if (ReadBytes(buf, 12) < 12) { SetError("WAV: short header"); return false; }
    if (std::memcmp(buf, "RIFF", 4) != 0 || std::memcmp(buf + 8, "WAVE", 4) != 0) {
        SetError("WAV: not a RIFF/WAVE file");
        return false;
    }
    // Parse chunks
    while (true) {
        uint8_t chunkId[4];
        uint32_t chunkSize = 0;
        if (ReadBytes(chunkId, 4) < 4) break;
        if (ReadBytes(&chunkSize, 4) < 4) break;
        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            uint8_t fmt[40]{};
            uint32_t toRead = std::min(chunkSize, 40u);
            ReadBytes(fmt, toRead);
            if (chunkSize > 40) SeekBytes(chunkSize - 40, FILE_CURRENT);
            uint16_t tag  = *reinterpret_cast<uint16_t*>(fmt);
            channels_     = *reinterpret_cast<uint16_t*>(fmt + 2);
            sampleRate_   = *reinterpret_cast<uint32_t*>(fmt + 4);
            bitDepth_     = *reinterpret_cast<uint16_t*>(fmt + 14);
            bitrate_      = sampleRate_ * channels_ * bitDepth_ / 1000;
            isFloat_      = (tag == 3); // WAVE_FORMAT_IEEE_FLOAT
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            dataOffset_  = static_cast<uint32_t>(TellBytes());
            dataSize_    = chunkSize;
            int bytesPerFrame = (bitDepth_ / 8) * channels_;
            totalFrames_ = bytesPerFrame > 0 ? dataSize_ / bytesPerFrame : 0;
            break;
        } else {
            SeekBytes(chunkSize + (chunkSize & 1), FILE_CURRENT);
        }
    }
    if (dataOffset_ == 0) { SetError("WAV: no data chunk"); return false; }
    return true;
}

uint32_t WavDecoder::DecodeFloat(float* outPCM, uint32_t maxFrames) {
    if (dataOffset_ == 0) return 0;
    uint64_t remaining = totalFrames_ - framePos_;
    uint32_t toRead    = static_cast<uint32_t>(std::min<uint64_t>(remaining, maxFrames));
    if (toRead == 0) return 0;

    int bytesPerSample = bitDepth_ / 8;
    int bytesPerFrame  = bytesPerSample * channels_;
    uint32_t byteCount = toRead * bytesPerFrame;

    std::vector<uint8_t> raw(byteCount);
    uint32_t got = ReadBytes(raw.data(), byteCount);
    uint32_t gotFrames = got / bytesPerFrame;

    for (uint32_t i = 0; i < gotFrames * channels_; ++i) {
        const uint8_t* src = raw.data() + i * bytesPerSample;
        float sample = 0.f;
        if (isFloat_ && bitDepth_ == 32) {
            std::memcpy(&sample, src, 4);
        } else {
            switch (bitDepth_) {
                case 8:  sample = (*src - 128) / 128.0f;                      break;
                case 16: sample = *reinterpret_cast<const int16_t*>(src) / 32768.0f; break;
                case 24: {
                    int32_t v = (src[0] | (src[1] << 8) | (src[2] << 16));
                    if (v & 0x800000) v |= ~0xFFFFFF;
                    sample = v / 8388608.0f;
                    break;
                }
                case 32: sample = *reinterpret_cast<const int32_t*>(src) / 2147483648.0f; break;
            }
        }
        outPCM[i] = sample;
    }
    framePos_ += gotFrames;
    return gotFrames;
}

bool WavDecoder::Seek(SeekOrigin origin, const StreamTime& time) {
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
    int bytesPerFrame = (bitDepth_ / 8) * channels_;
    int64_t byteOff   = static_cast<int64_t>(dataOffset_) + targetFrame * bytesPerFrame;
    if (!SeekBytes(byteOff, FILE_BEGIN)) return false;
    framePos_ = static_cast<uint64_t>(targetFrame);
    return true;
}

StreamTime WavDecoder::GetPosition() const { return SamplesToTime(framePos_, sampleRate_); }
StreamTime WavDecoder::GetLength()   const { return SamplesToTime(totalFrames_, sampleRate_); }

} // namespace libZFork
