#include "ogg_decoder.h"
#include <filesystem>
#include <cstring>
#include <algorithm>

namespace libZFork {

struct OggMemDS {
    const uint8_t* data;
    size_t         size;
    size_t         pos;
};

size_t OggDecoder::MemRead(void* ptr, size_t size, size_t nmemb, void* ds) {
    auto* s   = static_cast<OggMemDS*>(ds);
    size_t n  = size * nmemb;
    size_t rem = s->size - s->pos;
    size_t toCopy = std::min(n, rem);
    std::memcpy(ptr, s->data + s->pos, toCopy);
    s->pos += toCopy;
    return toCopy / size;
}

int OggDecoder::MemSeek(void* ds, ogg_int64_t offset, int whence) {
    auto* s = static_cast<OggMemDS*>(ds);
    size_t newPos = 0;
    switch (whence) {
        case SEEK_SET: newPos = static_cast<size_t>(offset); break;
        case SEEK_CUR: newPos = s->pos + static_cast<size_t>(offset); break;
        case SEEK_END: newPos = s->size + static_cast<size_t>(offset); break;
        default: return -1;
    }
    if (newPos > s->size) return -1;
    s->pos = newPos;
    return 0;
}

int  OggDecoder::MemClose(void*) { return 0; }
long OggDecoder::MemTell(void* ds) { return static_cast<long>(static_cast<OggMemDS*>(ds)->pos); }

OggDecoder::OggDecoder()  = default;
OggDecoder::~OggDecoder() { CloseStream(); }

bool OggDecoder::ReadInfo() {
    vorbis_info* vi = ov_info(&vf_, -1);
    if (!vi) { SetError("ov_info failed"); return false; }
    sampleRate_ = static_cast<int>(vi->rate);
    channels_   = vi->channels;
    bitrate_    = static_cast<int>(ov_bitrate(&vf_, -1) / 1000);
    return true;
}

bool OggDecoder::OpenFile(const wchar_t* path) {
    CloseStream();
    std::filesystem::path p(path);
    auto utf8 = p.string();
    if (ov_fopen(utf8.c_str(), &vf_) != 0) {
        SetError("ov_fopen failed");
        return false;
    }
    opened_ = true;
    return ReadInfo();
}

bool OggDecoder::OpenMemory(const uint8_t* data, uint32_t size) {
    CloseStream();
    memData_.assign(data, data + size);

    auto* ds  = new OggMemDS{ memData_.data(), memData_.size(), 0 };
    ov_callbacks cbs{ MemRead, MemSeek, MemClose, MemTell };
    if (ov_open_callbacks(ds, &vf_, nullptr, 0, cbs) != 0) {
        delete ds;
        SetError("ov_open_callbacks failed");
        return false;
    }
    opened_ = true;
    return ReadInfo();
}

void OggDecoder::CloseStream() {
    if (opened_) {
        ov_clear(&vf_);
        opened_ = false;
    }
    memData_.clear();
    memOffset_ = 0;
}

uint32_t OggDecoder::DecodeFloat(float* outPCM, uint32_t maxFrames) {
    if (!opened_) return 0;
    uint32_t framesDecoded = 0;
    float*   dst           = outPCM;
    uint32_t remaining     = maxFrames;

    while (remaining > 0) {
        float** pcmChannels = nullptr;
        long ret = ov_read_float(&vf_, &pcmChannels,
                                 static_cast<int>(remaining), &currentSection_);
        if (ret <= 0) break;
        uint32_t frames = static_cast<uint32_t>(ret);
        // Interleave channels
        for (uint32_t f = 0; f < frames; ++f) {
            for (int c = 0; c < channels_; ++c)
                *dst++ = pcmChannels[c][f];
        }
        framesDecoded += frames;
        remaining     -= frames;
    }
    return framesDecoded;
}

bool OggDecoder::Seek(SeekOrigin origin, const StreamTime& time) {
    if (!opened_) return false;
    switch (origin) {
        case SeekOrigin::Begin: {
            const ogg_int64_t sample = static_cast<ogg_int64_t>(TimeToSamplePos(time, sampleRate_));
            return ov_pcm_seek(&vf_, sample) == 0;
        }
        case SeekOrigin::End:
            return ov_pcm_seek(&vf_, ov_pcm_total(&vf_, -1)) == 0;
        case SeekOrigin::Current: {
            ogg_int64_t cur = ov_pcm_tell(&vf_);
            return ov_pcm_seek(&vf_, cur + static_cast<ogg_int64_t>(time.samples)) == 0;
        }
    }
    return false;
}

StreamTime OggDecoder::GetPosition() const {
    if (!opened_) return {};
    ogg_int64_t pos = ov_pcm_tell(const_cast<OggVorbis_File*>(&vf_));
    return SamplesToTime(static_cast<uint64_t>(pos), sampleRate_);
}

StreamTime OggDecoder::GetLength() const {
    if (!opened_) return {};
    ogg_int64_t total = ov_pcm_total(const_cast<OggVorbis_File*>(&vf_), -1);
    return SamplesToTime(static_cast<uint64_t>(total), sampleRate_);
}

} // namespace libZFork
