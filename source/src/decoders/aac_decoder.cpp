#include "aac_decoder.h"
#include "utils/win_compat.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace libZFork {
namespace {

constexpr int kAdtsSampleRates[] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350,
};

bool ParseAdtsFrameHeader(const uint8_t* data, size_t avail, uint32_t& frameLen, int& sampleRateOut) {
    if (avail < 7) return false;
    if (data[0] != 0xFF || (data[1] & 0xF6) != 0xF0) return false;

    frameLen = ((data[3] & 0x03) << 11)
             | (static_cast<uint32_t>(data[4]) << 3)
             | ((data[5] & 0xE0) >> 5);

    const int srIndex = (data[2] >> 2) & 0x0F;
    if (srIndex >= 0 && srIndex < 13 && kAdtsSampleRates[srIndex] > 0) {
        sampleRateOut = kAdtsSampleRates[srIndex];
    }

    return frameLen >= 7;
}

} // namespace

AacDecoder::AacDecoder()  = default;
AacDecoder::~AacDecoder() { CloseStream(); }

bool AacDecoder::FindSyncWord(size_t& offset) const {
    for (size_t i = offset; i + 1 < fileData_.size(); ++i) {
        if (fileData_[i] == 0xFF && (fileData_[i + 1] & 0xF6) == 0xF0) {
            offset = i;
            return true;
        }
    }
    return false;
}

bool AacDecoder::ConfigureDecoder(NeAACDecHandle handle) const {
    if (!handle) return false;
    NeAACDecConfigurationPtr cfg = NeAACDecGetCurrentConfiguration(handle);
    cfg->outputFormat = FAAD_FMT_FLOAT;
    cfg->downMatrix   = 0;
    NeAACDecSetConfiguration(handle, cfg);
    return true;
}

bool AacDecoder::ReinitDecoderAt(size_t syncOffset) {
    if (hDecoder_) {
        NeAACDecClose(hDecoder_);
        hDecoder_ = nullptr;
    }

    hDecoder_ = NeAACDecOpen();
    if (!hDecoder_) {
        SetError("NeAACDecOpen failed");
        return false;
    }
    if (!ConfigureDecoder(hDecoder_)) {
        SetError("NeAACDecConfigure failed");
        return false;
    }

    unsigned long sr = 0;
    uint8_t ch = 0;
    const long initBytes = NeAACDecInit(
        hDecoder_,
        const_cast<uint8_t*>(fileData_.data() + syncOffset),
        static_cast<uint32_t>(fileData_.size() - syncOffset),
        &sr,
        &ch);
    if (initBytes < 0) {
        SetError("NeAACDecInit failed");
        return false;
    }

    if (sr > 0) sampleRate_ = static_cast<int>(sr);
    if (ch > 0) channels_ = static_cast<int>(ch);
    syncOffset_ = syncOffset;
    dataOffset_ = syncOffset + static_cast<size_t>(initBytes);
    leftover_.clear();
    leftoverPos_ = 0;
    return true;
}

uint32_t AacDecoder::CalibrateSamplesPerFrame() {
    if (!hDecoder_ || dataOffset_ >= fileData_.size()) {
        return 1024;
    }

    NeAACDecFrameInfo fi{};
    void* samples = NeAACDecDecode(
        hDecoder_,
        &fi,
        fileData_.data() + dataOffset_,
        static_cast<unsigned long>(fileData_.size() - dataOffset_));
    (void)samples;

    if (fi.error != 0 || fi.bytesconsumed == 0 || fi.samples == 0 || fi.channels == 0) {
        return 1024;
    }

    return static_cast<uint32_t>(fi.samples / fi.channels);
}

bool AacDecoder::BuildFrameIndexFast(uint32_t samplesPerFrame) {
    frameIndex_.clear();
    totalSamples_ = 0;
    bitrate_ = 0;
    samplesPerFrame_ = samplesPerFrame > 0 ? samplesPerFrame : 1024;

    if (syncOffset_ >= fileData_.size()) {
        SetError("Invalid AAC sync offset");
        return false;
    }

    size_t pos = syncOffset_;
    uint64_t samples = 0;
    int headerRate = sampleRate_;

    while (pos + 7 <= fileData_.size()) {
        if (fileData_[pos] != 0xFF || (fileData_[pos + 1] & 0xF6) != 0xF0) {
            ++pos;
            continue;
        }

        uint32_t frameLen = 0;
        int adtsRate = sampleRate_;
        if (!ParseAdtsFrameHeader(fileData_.data() + pos, fileData_.size() - pos, frameLen, adtsRate)) {
            ++pos;
            continue;
        }
        if (frameLen == 0 || pos + frameLen > fileData_.size()) {
            break;
        }

        frameIndex_.push_back({pos, samples});
        if (adtsRate > 0) {
            headerRate = adtsRate;
        }

        samples += samplesPerFrame_;
        pos += frameLen;
    }

    if (headerRate > 0) {
        sampleRate_ = headerRate;
    }

    totalSamples_ = samples;
    if (totalSamples_ > 0 && sampleRate_ > 0) {
        const double seconds = static_cast<double>(totalSamples_) / static_cast<double>(sampleRate_);
        if (seconds > 0.0 && !fileData_.empty()) {
            bitrate_ = static_cast<int>((fileData_.size() * 8.0) / seconds / 1000.0);
        }
    }

    return !frameIndex_.empty() && totalSamples_ > 0;
}

bool AacDecoder::InitFromBuffer(const uint8_t* data, uint32_t size) {
    constexpr uint32_t kMaxAacBytes = 512u * 1024u * 1024u;
    if (!data || size == 0 || size > kMaxAacBytes) {
        SetError("Invalid or too-large AAC buffer");
        return false;
    }
    fileData_.assign(data, data + size);
    frameIndex_.clear();
    totalSamples_ = 0;
    position_ = 0;
    dataOffset_ = 0;
    syncOffset_ = 0;
    samplesPerFrame_ = 1024;

    size_t offset = 0;
    if (!FindSyncWord(offset)) {
        SetError("No ADTS sync found");
        return false;
    }

    if (!ReinitDecoderAt(offset)) {
        return false;
    }

    const uint32_t samplesPerFrame = CalibrateSamplesPerFrame();
    if (!BuildFrameIndexFast(samplesPerFrame)) {
        SetError("Failed to index ADTS AAC stream");
        return false;
    }

    if (!ReinitDecoderAt(offset)) {
        return false;
    }

    position_ = 0;
    return true;
}

bool AacDecoder::OpenFile(const wchar_t* path) {
    CloseStream();
    HANDLE hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, 0, nullptr);
    if (hf == INVALID_HANDLE_VALUE) {
        SetError("Cannot open AAC file");
        return false;
    }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(hf, &sz) || sz.QuadPart <= 0) {
        CloseHandle(hf);
        SetError("Cannot read AAC file size");
        return false;
    }
    // Cap in-memory ADTS load — prevents OOM / allocator aborts on huge/corrupt paths.
    constexpr int64_t kMaxAacBytes = 512LL * 1024 * 1024;
    if (sz.QuadPart > kMaxAacBytes) {
        CloseHandle(hf);
        SetError("AAC file too large to map into memory");
        return false;
    }
    std::vector<uint8_t> buf(static_cast<size_t>(sz.QuadPart));
    DWORD read = 0;
    const BOOL ok = ReadFile(hf, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr);
    CloseHandle(hf);
    if (!ok || read == 0) {
        SetError("Cannot read AAC file");
        return false;
    }
    return InitFromBuffer(buf.data(), static_cast<uint32_t>(read));
}

bool AacDecoder::OpenMemory(const uint8_t* data, uint32_t size) {
    CloseStream();
    return InitFromBuffer(data, size);
}

void AacDecoder::CloseStream() {
    if (hDecoder_) {
        NeAACDecClose(hDecoder_);
        hDecoder_ = nullptr;
    }
    fileData_.clear();
    frameIndex_.clear();
    leftover_.clear();
    leftoverPos_ = 0;
    dataOffset_ = 0;
    syncOffset_ = 0;
    position_ = 0;
    totalSamples_ = 0;
    samplesPerFrame_ = 1024;
    bitrate_ = 0;
}

uint32_t AacDecoder::DecodeFloat(float* outPCM, uint32_t maxFrames) {
    if (!hDecoder_ || !outPCM || maxFrames == 0) return 0;
    const int ch = std::max(1, channels_);
    uint32_t framesDecoded = 0;
    float* dst = outPCM;
    uint32_t remaining = maxFrames;

    while (remaining > 0) {
        if (leftoverPos_ < leftover_.size()) {
            const size_t avail = leftover_.size() - leftoverPos_;
            const size_t consume = std::min(avail, static_cast<size_t>(remaining) * static_cast<size_t>(ch));
            std::copy(leftover_.begin() + leftoverPos_,
                      leftover_.begin() + leftoverPos_ + consume,
                      dst);
            dst += consume;
            leftoverPos_ += consume;
            framesDecoded += static_cast<uint32_t>(consume / static_cast<size_t>(ch));
            remaining -= static_cast<uint32_t>(consume / static_cast<size_t>(ch));
            if (leftoverPos_ >= leftover_.size()) {
                leftover_.clear();
                leftoverPos_ = 0;
            }
            continue;
        }

        if (dataOffset_ >= fileData_.size()) break;

        NeAACDecFrameInfo fi{};
        void* samples = NeAACDecDecode(
            hDecoder_,
            &fi,
            fileData_.data() + dataOffset_,
            static_cast<uint32_t>(fileData_.size() - dataOffset_));
        if (fi.error || !samples || fi.channels == 0 || fi.bytesconsumed == 0) break;
        dataOffset_ += fi.bytesconsumed;
        if (fi.channels > 0 && fi.channels != static_cast<unsigned>(channels_))
            channels_ = static_cast<int>(fi.channels);
        const int frameCh = std::max(1, static_cast<int>(fi.channels));
        position_ += fi.samples / static_cast<unsigned>(frameCh);

        auto* fsamples = static_cast<float*>(samples);
        const size_t total = fi.samples;
        const size_t consume = std::min(total, static_cast<size_t>(remaining) * static_cast<size_t>(frameCh));
        std::copy(fsamples, fsamples + consume, dst);
        dst += consume;
        framesDecoded += static_cast<uint32_t>(consume / static_cast<size_t>(frameCh));
        remaining -= static_cast<uint32_t>(consume / static_cast<size_t>(frameCh));
        if (consume < total) {
            leftover_.assign(fsamples + consume, fsamples + total);
            leftoverPos_ = 0;
        }
    }
    return framesDecoded;
}

uint32_t AacDecoder::DiscardFrames(uint64_t frameCount) {
    uint64_t left = frameCount;
    uint32_t discarded = 0;
    // Scratch must hold frames * channels floats — fixed 4096*2 overflowed for >2ch.
    const int ch = std::max(1, channels_);
    constexpr uint32_t kMaxFrames = 1024;
    std::vector<float> scratch(static_cast<size_t>(kMaxFrames) * static_cast<size_t>(ch));

    while (left > 0) {
        const uint32_t chunk = static_cast<uint32_t>(std::min<uint64_t>(left, kMaxFrames));
        const uint32_t got = DecodeFloat(scratch.data(), chunk);
        if (got == 0) break;
        discarded += got;
        left -= got;
    }
    return discarded;
}

bool AacDecoder::Seek(SeekOrigin origin, const StreamTime& time) {
    if (!hDecoder_ || frameIndex_.empty() || totalSamples_ == 0) {
        return false;
    }

    uint64_t target = 0;
    switch (origin) {
        case SeekOrigin::Begin:
            target = TimeToSamplePos(time, sampleRate_);
            break;
        case SeekOrigin::Current:
            target = position_ + time.samples;
            break;
        case SeekOrigin::End:
            target = totalSamples_;
            if (time.samples > 0 && time.samples < totalSamples_) {
                target = totalSamples_ - time.samples;
            }
            break;
    }

    if (target > totalSamples_) {
        target = totalSamples_;
    }

    size_t best = 0;
    for (size_t i = 0; i < frameIndex_.size(); ++i) {
        if (frameIndex_[i].samplePos <= target) {
            best = i;
        } else {
            break;
        }
    }

    if (!ReinitDecoderAt(frameIndex_[best].byteOffset)) {
        return false;
    }

    position_ = frameIndex_[best].samplePos;

    if (target > position_) {
        (void)DiscardFrames(target - position_);
    }

    return true;
}

StreamTime AacDecoder::GetPosition() const {
    return SamplesToTime(position_, sampleRate_);
}

StreamTime AacDecoder::GetLength() const {
    return SamplesToTime(totalSamples_, sampleRate_);
}

} // namespace libZFork
