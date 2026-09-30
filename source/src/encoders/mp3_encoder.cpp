#include "mp3_encoder.h"
#include <algorithm>
#include <cstring>

namespace libZFork {

bool Mp3Encoder::Open(const wchar_t* path, int sampleRate, int channels) {
    Close();
    channels_ = channels;
    gfp_ = lame_init();
    if (!gfp_) { SetError("lame_init failed"); return false; }

    lame_set_in_samplerate(gfp_, sampleRate);
    lame_set_num_channels(gfp_, channels);
    lame_set_out_samplerate(gfp_, sampleRate);
    lame_set_quality(gfp_, 2);       // 0=best, 9=worst
    lame_set_VBR(gfp_, vbr_default);
    lame_set_VBR_q(gfp_, 4);        // quality level
    if (lame_init_params(gfp_) < 0) { SetError("lame_init_params failed"); return false; }

    hFile_ = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile_ == INVALID_HANDLE_VALUE) { SetError("Cannot create MP3 file"); return false; }

    mp3buf_.resize(8192);
    return true;
}

bool Mp3Encoder::Write(std::span<const float> pcm) {
    if (!gfp_ || hFile_ == INVALID_HANDLE_VALUE) return false;
    size_t frames = pcm.size() / channels_;

    std::vector<float> left(frames), right(frames);
    if (channels_ == 2) {
        for (size_t i = 0; i < frames; ++i) {
            left[i]  = pcm[i * 2];
            right[i] = pcm[i * 2 + 1];
        }
    } else {
        for (size_t i = 0; i < frames; ++i)
            left[i] = right[i] = pcm[i];
    }

    int needed = static_cast<int>(frames * 5 / 4 + 7200);
    mp3buf_.resize(needed);

    int bytes = lame_encode_buffer_ieee_float(
        gfp_,
        left.data(), right.data(),
        static_cast<int>(frames),
        mp3buf_.data(), needed);

    if (bytes < 0) { SetError("lame encode error"); return false; }
    if (bytes > 0) {
        DWORD written;
        WriteFile(hFile_, mp3buf_.data(), bytes, &written, nullptr);
    }
    return true;
}

void Mp3Encoder::Close() {
    if (gfp_) {
        if (hFile_ != INVALID_HANDLE_VALUE) {
            mp3buf_.resize(7200);
            int bytes = lame_encode_flush(gfp_, mp3buf_.data(), 7200);
            if (bytes > 0) {
                DWORD written;
                WriteFile(hFile_, mp3buf_.data(), bytes, &written, nullptr);
            }
        }
        lame_close(gfp_);
        gfp_ = nullptr;
    }
    if (hFile_ != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile_);
        hFile_ = INVALID_HANDLE_VALUE;
    }
}

} // namespace libZFork
