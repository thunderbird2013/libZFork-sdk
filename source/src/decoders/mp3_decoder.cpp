#include "mp3_decoder.h"
#include <filesystem>
#include <cstring>
#ifdef _WIN32
#  include <io.h>      // _wopen, _close
#  include <fcntl.h>   // _O_RDONLY, _O_BINARY
#  include <share.h>   // _SH_DENYNO
#endif

namespace libZFork {

bool Mp3Decoder::s_libInitialized = false;

Mp3Decoder::Mp3Decoder() {
    if (!s_libInitialized) {
        mpg123_init();
        s_libInitialized = true;
    }
}

Mp3Decoder::~Mp3Decoder() {
    CloseStream();
}

bool Mp3Decoder::Init() {
    int err = MPG123_OK;
    mh_ = mpg123_new(nullptr, &err);
    if (!mh_) {
        SetError(mpg123_plain_strerror(err));
        return false;
    }
    mpg123_param(mh_, MPG123_FLAGS,     MPG123_FORCE_FLOAT | MPG123_GAPLESS, 0.0);
    mpg123_param(mh_, MPG123_ADD_FLAGS, MPG123_PICTURE, 0.0);
    return true;
}

bool Mp3Decoder::ConfigureHandle() {
    long rate = 0; int ch = 0, enc = 0;
    if (mpg123_getformat(mh_, &rate, &ch, &enc) != MPG123_OK) {
        SetError(mpg123_strerror(mh_));
        return false;
    }
    sampleRate_ = static_cast<int>(rate);
    channels_   = ch;
    mpg123_format_none(mh_);
    mpg123_format(mh_, rate, ch, MPG123_ENC_FLOAT_32);

    struct mpg123_frameinfo fi{};
    if (mpg123_info(mh_, &fi) == MPG123_OK) {
        bitrate_ = fi.bitrate;
        isVBR_   = (fi.vbr != MPG123_CBR);
    }
    return true;
}

bool Mp3Decoder::OpenFile(const wchar_t* path) {
    CloseStream();
    if (!Init()) return false;

#ifdef _WIN32
    // Datei via _wopen öffnen (volle Unicode-Pfad-Unterstützung),
    // damit keine Konvertierung nach ACP nötig ist (std::filesystem::path::string()
    // würde für Nicht-ACP-Zeichen eine std::system_error werfen).
    const int fd = _wsopen(path, _O_RDONLY | _O_BINARY, _SH_DENYNO);
    if (fd < 0) {
        SetError("Cannot open file (wide path)");
        return false;
    }
    if (mpg123_open_fd(mh_, fd) != MPG123_OK) {
        _close(fd);
        SetError(mpg123_strerror(mh_));
        return false;
    }
    // mpg123_open_fd übernimmt den fd — wir schließen ihn nicht selbst.
#else
    std::filesystem::path p(path);
    const auto narrow = p.string();   // POSIX-Pfade sind UTF-8
    if (mpg123_open(mh_, narrow.c_str()) != MPG123_OK) {
        SetError(mpg123_strerror(mh_));
        return false;
    }
#endif

    mpg123_scan(mh_);
    return ConfigureHandle();
}

bool Mp3Decoder::OpenMemory(const uint8_t* data, uint32_t size) {
    CloseStream();
    if (!Init()) return false;
    memData_.assign(data, data + size);
    memOffset_ = 0;
    if (mpg123_open_feed(mh_) != MPG123_OK) {
        SetError(mpg123_strerror(mh_));
        return false;
    }
    // Feed initial chunk so format is detected
    size_t done = 0;
    mpg123_feed(mh_, memData_.data(), std::min<size_t>(65536, memData_.size()));
    // Decode until format is known
    float tmp[4096];
    mpg123_read(mh_, reinterpret_cast<unsigned char*>(tmp), sizeof(tmp), &done);
    return ConfigureHandle();
}

void Mp3Decoder::CloseStream() {
    if (mh_) {
        mpg123_close(mh_);
        mpg123_delete(mh_);
        mh_ = nullptr;
    }
    memData_.clear();
    memOffset_ = 0;
}

uint32_t Mp3Decoder::DecodeFloat(float* outPCM, uint32_t maxFrames) {
    if (!mh_) return 0;
    size_t bytesWanted = static_cast<size_t>(maxFrames) * channels_ * sizeof(float);
    size_t bytesRead   = 0;
    int    ret;

    if (!memData_.empty() && memOffset_ < memData_.size()) {
        size_t remaining = memData_.size() - memOffset_;
        size_t toFeed    = std::min(remaining, static_cast<size_t>(65536));
        mpg123_feed(mh_, memData_.data() + memOffset_, toFeed);
        memOffset_ += toFeed;
    }

    ret = mpg123_read(mh_,
                      reinterpret_cast<unsigned char*>(outPCM),
                      bytesWanted,
                      &bytesRead);
    if (ret == MPG123_DONE || (ret != MPG123_OK && ret != MPG123_NEW_FORMAT)) {
        return 0;
    }
    return static_cast<uint32_t>(bytesRead / (channels_ * sizeof(float)));
}

bool Mp3Decoder::Seek(SeekOrigin origin, const StreamTime& time) {
    if (!mh_) return false;
    off_t targetSample = 0;
    switch (origin) {
        case SeekOrigin::Begin:
            targetSample = static_cast<off_t>(TimeToSamplePos(time, sampleRate_));
            return mpg123_seek(mh_, targetSample, SEEK_SET) >= 0;
        case SeekOrigin::Current:
            targetSample = static_cast<off_t>(time.samples);
            return mpg123_seek(mh_, targetSample, SEEK_CUR) >= 0;
        case SeekOrigin::End:
            return mpg123_seek(mh_, 0, SEEK_END) >= 0;
    }
    return false;
}

StreamTime Mp3Decoder::GetPosition() const {
    if (!mh_) return {};
    off_t pos = mpg123_tell(mh_);
    if (pos < 0) return {};
    return SamplesToTime(static_cast<uint64_t>(pos), sampleRate_);
}

StreamTime Mp3Decoder::GetLength() const {
    if (!mh_) return {};
    off_t len = mpg123_length(mh_);
    if (len < 0) return {};
    return SamplesToTime(static_cast<uint64_t>(len), sampleRate_);
}

} // namespace libZFork
