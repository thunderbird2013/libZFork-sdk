#include "pcm_decoder.h"
#include "utils/win_compat.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace libZFork {

PcmDecoder::PcmDecoder() : queue_(1024 * 1024 * 4) {}
PcmDecoder::~PcmDecoder() { CloseStream(); }

void PcmDecoder::SetPCMFormat(const PCMFormat& fmt) {
    fmt_       = fmt;
    streaming_ = true;
    queue_.Clear();
    queue_.Reopen();
}

bool PcmDecoder::OpenFile(const wchar_t* path) {
    CloseStream();
    HANDLE hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, 0, nullptr);
    if (hf == INVALID_HANDLE_VALUE) { SetError("Cannot open PCM file"); return false; }
    LARGE_INTEGER sz{};
    GetFileSizeEx(hf, &sz);
    fileData_.resize(static_cast<size_t>(sz.QuadPart));
    DWORD read = 0;
    ReadFile(hf, fileData_.data(), static_cast<DWORD>(fileData_.size()), &read, nullptr);
    CloseHandle(hf);
    filePos_ = 0;
    int bpf  = (fmt_.bitsPerSample / 8) * fmt_.channels;
    frames_  = bpf > 0 ? fileData_.size() / bpf : 0;
    return true;
}

bool PcmDecoder::OpenMemory(const uint8_t* data, uint32_t size) {
    CloseStream();
    fileData_.assign(data, data + size);
    filePos_ = 0;
    int bpf  = (fmt_.bitsPerSample / 8) * fmt_.channels;
    frames_  = bpf > 0 ? fileData_.size() / bpf : 0;
    return true;
}

void PcmDecoder::CloseStream() {
    fileData_.clear();
    filePos_   = 0;
    frames_    = 0;
    streaming_ = false;
    queue_.Clear();
}

bool PcmDecoder::Push(const void* data, uint32_t byteCount) {
    if (!streaming_) return false;
    auto bytes = std::span(static_cast<const std::byte*>(data), byteCount);
    return queue_.Write(bytes, false) > 0;
}

float PcmDecoder::SampleToFloat(const uint8_t* src) const {
    if (fmt_.isFloat && fmt_.bitsPerSample == 32) {
        float f; std::memcpy(&f, src, 4); return f;
    }
    switch (fmt_.bitsPerSample) {
        case 8:  return (*src - 128) / 128.0f;
        case 16: return *reinterpret_cast<const int16_t*>(src) / 32768.0f;
        case 24: {
            int32_t v = src[0] | (src[1] << 8) | (src[2] << 16);
            if (v & 0x800000) v |= ~0xFFFFFF;
            return v / 8388608.0f;
        }
        case 32: return *reinterpret_cast<const int32_t*>(src) / 2147483648.0f;
    }
    return 0.f;
}

uint32_t PcmDecoder::DecodeFloat(float* outPCM, uint32_t maxFrames) {
    int bps    = fmt_.bitsPerSample / 8;
    int bpf    = bps * fmt_.channels;
    uint32_t n = 0;

    if (streaming_) {
        std::vector<std::byte> raw(static_cast<size_t>(maxFrames) * bpf);
        uint32_t got = queue_.Read(std::span(raw), false);
        n = got / bpf;
        for (uint32_t i = 0; i < n * fmt_.channels; ++i)
            outPCM[i] = SampleToFloat(reinterpret_cast<const uint8_t*>(raw.data()) + i * bps);
    } else {
        size_t rem = fileData_.size() - filePos_;
        n = std::min<uint32_t>(maxFrames, static_cast<uint32_t>(rem / bpf));
        for (uint32_t i = 0; i < n * fmt_.channels; ++i)
            outPCM[i] = SampleToFloat(fileData_.data() + filePos_ + i * bps);
        filePos_ += n * bpf;
    }
    return n;
}

bool PcmDecoder::Seek(SeekOrigin origin, const StreamTime& time) {
    if (streaming_) { queue_.Clear(); return true; }
    int bpf = (fmt_.bitsPerSample / 8) * fmt_.channels;
    uint64_t targetFrame = 0;
    switch (origin) {
        case SeekOrigin::Begin:
            targetFrame = TimeToSamplePos(time, fmt_.sampleRate);
            break;
        case SeekOrigin::Current:
            targetFrame = filePos_ / bpf + time.samples;
            break;
        case SeekOrigin::End:
            targetFrame = frames_;
            break;
    }
    targetFrame = std::min(targetFrame, frames_);
    filePos_    = static_cast<size_t>(targetFrame * bpf);
    return true;
}

StreamTime PcmDecoder::GetPosition() const {
    int bpf = (fmt_.bitsPerSample / 8) * fmt_.channels;
    uint64_t f = bpf > 0 ? filePos_ / bpf : 0;
    return SamplesToTime(f, fmt_.sampleRate);
}

StreamTime PcmDecoder::GetLength() const {
    return SamplesToTime(frames_, fmt_.sampleRate);
}

} // namespace libZFork
