#include "flac_decoder.h"
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace libZFork {

namespace {
float MakeFlacScale(int bitDepth) {
    // bits_per_sample from STREAMINFO can be garbage on corrupt files;
    // 1 << (bitDepth-1) is UB for bitDepth < 1 or > 31.
    if (bitDepth < 4)  bitDepth = 16;
    if (bitDepth > 31) bitDepth = 31;
    return 1.0f / static_cast<float>(1u << (bitDepth - 1));
}
} // namespace

FlacDecoder::FlacDecoder()  = default;
FlacDecoder::~FlacDecoder() { CloseStream(); }

bool FlacDecoder::InitDecoder() {
    decoder_ = FLAC__stream_decoder_new();
    if (!decoder_) { SetError("FLAC__stream_decoder_new failed"); return false; }
    FLAC__stream_decoder_set_md5_checking(decoder_, false);
    return true;
}

void FlacDecoder::ClearPcmBuffer() {
    floatBuf_.clear();
    bufRead_ = 0;
}

bool FlacDecoder::OpenFile(const wchar_t* path) {
    CloseStream();
    if (!InitDecoder()) return false;

    std::filesystem::path p(path);
    auto utf8 = p.string();
    FLAC__StreamDecoderInitStatus st = FLAC__stream_decoder_init_file(
        decoder_, utf8.c_str(), WriteCallback, MetadataCallback, ErrorCallback, this);
    if (st != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        SetError("FLAC init_file failed");
        return false;
    }
    FLAC__stream_decoder_process_until_end_of_metadata(decoder_);
    scaleFactor_ = MakeFlacScale(bitDepth_);
    return true;
}

bool FlacDecoder::OpenMemory(const uint8_t* data, uint32_t size) {
    CloseStream();
    if (!InitDecoder()) return false;
    memData_.assign(data, data + size);
    memOffset_ = 0;

    FLAC__StreamDecoderInitStatus st = FLAC__stream_decoder_init_stream(
        decoder_,
        MemReadCb, MemSeekCb, MemTellCb, MemLengthCb, MemEofCb,
        WriteCallback, MetadataCallback, ErrorCallback, this);
    if (st != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        SetError("FLAC init_stream failed");
        return false;
    }
    FLAC__stream_decoder_process_until_end_of_metadata(decoder_);
    scaleFactor_ = MakeFlacScale(bitDepth_);
    return true;
}

void FlacDecoder::CloseStream() {
    std::lock_guard lock(mtx_);
    if (decoder_) {
        FLAC__stream_decoder_finish(decoder_);
        FLAC__stream_decoder_delete(decoder_);
        decoder_ = nullptr;
    }
    ClearPcmBuffer();
    memData_.clear();
    memOffset_ = 0;
    position_ = 0;
    totalSamples_ = 0;
    pendingSeekSample_ = UINT64_MAX;
}

uint32_t FlacDecoder::DecodeFloat(float* outPCM, uint32_t maxFrames) {
    std::lock_guard lock(mtx_);
    if (!decoder_ || !outPCM || maxFrames == 0 || channels_ <= 0) return 0;
    uint32_t framesDecoded = 0;
    float*   dst           = outPCM;
    uint32_t remaining     = maxFrames;

    while (remaining > 0) {
        // Drain internal buffer first
        if (bufRead_ > floatBuf_.size()) {
            ClearPcmBuffer();
        }
        size_t avail = floatBuf_.size() - bufRead_;
        if (avail > 0) {
            size_t toConsume = std::min<size_t>(avail,
                static_cast<size_t>(remaining) * static_cast<size_t>(channels_));
            // Keep whole frames only
            toConsume -= toConsume % static_cast<size_t>(channels_);
            if (toConsume == 0) break;
            std::copy(floatBuf_.begin() + static_cast<std::ptrdiff_t>(bufRead_),
                      floatBuf_.begin() + static_cast<std::ptrdiff_t>(bufRead_ + toConsume),
                      dst);
            dst            += toConsume;
            bufRead_        += toConsume;
            uint32_t frames = static_cast<uint32_t>(toConsume / static_cast<size_t>(channels_));
            framesDecoded  += frames;
            remaining      -= frames;
            if (bufRead_ >= floatBuf_.size()) {
                ClearPcmBuffer();
            }
            continue;
        }
        // Decode next FLAC block
        if (!FLAC__stream_decoder_process_single(decoder_)) break;
        const auto st = FLAC__stream_decoder_get_state(decoder_);
        if (st == FLAC__STREAM_DECODER_END_OF_STREAM ||
            st == FLAC__STREAM_DECODER_ABORTED ||
            st == FLAC__STREAM_DECODER_SEEK_ERROR) {
            break;
        }
    }
    return framesDecoded;
}

FLAC__StreamDecoderWriteStatus FlacDecoder::WriteCallback(
    const FLAC__StreamDecoder*, const FLAC__Frame* frame,
    const FLAC__int32* const buffer[], void* client)
{
    auto* self  = static_cast<FlacDecoder*>(client);
    uint32_t n  = frame->header.blocksize;
    int ch      = static_cast<int>(frame->header.channels);
    if (n == 0 || ch <= 0 || !buffer) {
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }

    uint32_t skip = 0;
    if (self->pendingSeekSample_ != UINT64_MAX) {
        FLAC__uint64 frameStart = 0;
        if (frame->header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER) {
            frameStart = frame->header.number.sample_number;
        } else {
            frameStart = static_cast<FLAC__uint64>(frame->header.number.frame_number) *
                         static_cast<FLAC__uint64>(n);
        }
        if (self->pendingSeekSample_ > frameStart) {
            const FLAC__uint64 delta = self->pendingSeekSample_ - frameStart;
            skip = static_cast<uint32_t>(std::min<FLAC__uint64>(delta, n));
        }
    }

    const uint32_t keep = n - skip;
    self->floatBuf_.resize(static_cast<size_t>(keep) * static_cast<size_t>(ch));
    for (uint32_t i = 0; i < keep; ++i)
        for (int c = 0; c < ch; ++c)
            self->floatBuf_[static_cast<size_t>(i) * static_cast<size_t>(ch) +
                            static_cast<size_t>(c)] =
                buffer[c][skip + i] * self->scaleFactor_;
    self->bufRead_ = 0;

    // During seek, Seek() sets position_ to the target; do not double-count.
    if (self->pendingSeekSample_ == UINT64_MAX) {
        self->position_ += keep;
    }
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void FlacDecoder::MetadataCallback(
    const FLAC__StreamDecoder*, const FLAC__StreamMetadata* meta, void* client)
{
    auto* self = static_cast<FlacDecoder*>(client);
    if (meta->type == FLAC__METADATA_TYPE_STREAMINFO) {
        self->sampleRate_    = static_cast<int>(meta->data.stream_info.sample_rate);
        self->channels_      = static_cast<int>(meta->data.stream_info.channels);
        self->bitDepth_      = static_cast<int>(meta->data.stream_info.bits_per_sample);
        self->totalSamples_  = meta->data.stream_info.total_samples;
    }
}

void FlacDecoder::ErrorCallback(
    const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void*) {}

// Memory stream callbacks
FLAC__StreamDecoderReadStatus FlacDecoder::MemReadCb(
    const FLAC__StreamDecoder*, FLAC__byte buf[], size_t* bytes, void* client)
{
    auto* self = static_cast<FlacDecoder*>(client);
    size_t rem = self->memData_.size() - self->memOffset_;
    if (rem == 0) { *bytes = 0; return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM; }
    size_t toRead = std::min(*bytes, rem);
    std::memcpy(buf, self->memData_.data() + self->memOffset_, toRead);
    self->memOffset_ += toRead;
    *bytes = toRead;
    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

FLAC__StreamDecoderSeekStatus FlacDecoder::MemSeekCb(
    const FLAC__StreamDecoder*, FLAC__uint64 offset, void* client)
{
    auto* self = static_cast<FlacDecoder*>(client);
    if (offset > self->memData_.size()) return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
    self->memOffset_ = static_cast<size_t>(offset);
    return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}

FLAC__StreamDecoderTellStatus FlacDecoder::MemTellCb(
    const FLAC__StreamDecoder*, FLAC__uint64* offset, void* client)
{
    auto* self = static_cast<FlacDecoder*>(client);
    *offset = static_cast<FLAC__uint64>(self->memOffset_);
    return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

FLAC__StreamDecoderLengthStatus FlacDecoder::MemLengthCb(
    const FLAC__StreamDecoder*, FLAC__uint64* length, void* client)
{
    auto* self = static_cast<FlacDecoder*>(client);
    *length = static_cast<FLAC__uint64>(self->memData_.size());
    return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
}

FLAC__bool FlacDecoder::MemEofCb(const FLAC__StreamDecoder*, void* client) {
    auto* self = static_cast<FlacDecoder*>(client);
    return self->memOffset_ >= self->memData_.size() ? 1 : 0;
}

bool FlacDecoder::Seek(SeekOrigin origin, const StreamTime& time) {
    std::lock_guard lock(mtx_);
    if (!decoder_) return false;

    uint64_t targetSample = 0;
    switch (origin) {
        case SeekOrigin::Begin:
            targetSample = TimeToSamplePos(time, sampleRate_);
            break;
        case SeekOrigin::Current:
            targetSample = position_ + time.samples;
            break;
        case SeekOrigin::End:
            targetSample = totalSamples_;
            break;
    }

    // libFLAC requires sample < total_samples; seeking past the end leaves
    // the decoder in SEEK_ERROR and breaks subsequent DecodeFloat calls.
    if (totalSamples_ > 0 && targetSample >= totalSamples_) {
        targetSample = totalSamples_ - 1;
    }

    ClearPcmBuffer();
    pendingSeekSample_ = targetSample;

    if (!FLAC__stream_decoder_seek_absolute(decoder_, targetSample)) {
        pendingSeekSample_ = UINT64_MAX;
        ClearPcmBuffer();
        // Recover from SEEK_ERROR so playback can continue / reseek.
        (void)FLAC__stream_decoder_flush(decoder_);
        SetError("FLAC seek failed");
        return false;
    }

    position_ = targetSample;
    pendingSeekSample_ = UINT64_MAX;
    return true;
}

StreamTime FlacDecoder::GetPosition() const {
    std::lock_guard lock(mtx_);
    return SamplesToTime(position_, sampleRate_);
}

StreamTime FlacDecoder::GetLength() const {
    std::lock_guard lock(mtx_);
    return SamplesToTime(totalSamples_, sampleRate_);
}

} // namespace libZFork
