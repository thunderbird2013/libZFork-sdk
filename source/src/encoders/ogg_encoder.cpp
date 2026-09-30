#include "ogg_encoder.h"
#include <cstring>
#include <algorithm>
#include <ctime>

namespace libZFork {

bool OggEncoder::Open(const wchar_t* path, int sampleRate, int channels) {
    Close();
    channels_ = channels;

    vorbis_info_init(&vi_);
    if (vorbis_encode_init_vbr(&vi_, channels, sampleRate, 0.4f) != 0) {
        SetError("vorbis_encode_init_vbr failed");
        return false;
    }
    vorbis_analysis_init(&vd_, &vi_);
    vorbis_block_init(&vd_, &vb_);

    srand(static_cast<unsigned int>(time(nullptr)));
    ogg_stream_init(&os_, rand());

    // Build headers
    ogg_packet header, header_comm, header_code;
    vorbis_comment vc; vorbis_comment_init(&vc);
    vorbis_analysis_headerout(&vd_, &vc, &header, &header_comm, &header_code);
    vorbis_comment_clear(&vc);

    hFile_ = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile_ == INVALID_HANDLE_VALUE) { SetError("Cannot create OGG file"); return false; }

    ogg_stream_packetin(&os_, &header);
    ogg_stream_packetin(&os_, &header_comm);
    ogg_stream_packetin(&os_, &header_code);

    while (ogg_stream_flush(&os_, &og_)) {
        DWORD w;
        WriteFile(hFile_, og_.header, og_.header_len, &w, nullptr);
        WriteFile(hFile_, og_.body,   og_.body_len,   &w, nullptr);
    }
    opened_ = true;
    return true;
}

bool OggEncoder::Write(std::span<const float> pcm) {
    if (!opened_) return false;
    size_t frames = pcm.size() / channels_;
    float** buffer = vorbis_analysis_buffer(&vd_, static_cast<int>(frames));

    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < channels_; ++c)
            buffer[c][i] = pcm[i * channels_ + c];

    vorbis_analysis_wrote(&vd_, static_cast<int>(frames));
    Flush();
    return true;
}

void OggEncoder::Flush() {
    while (vorbis_analysis_blockout(&vd_, &vb_) == 1) {
        vorbis_analysis(&vb_, nullptr);
        vorbis_bitrate_addblock(&vb_);
        while (vorbis_bitrate_flushpacket(&vd_, &op_)) {
            ogg_stream_packetin(&os_, &op_);
            while (!op_.e_o_s) {
                if (ogg_stream_pageout(&os_, &og_) == 0) break;
                DWORD w;
                WriteFile(hFile_, og_.header, og_.header_len, &w, nullptr);
                WriteFile(hFile_, og_.body,   og_.body_len,   &w, nullptr);
            }
        }
    }
}

void OggEncoder::Close() {
    if (opened_) {
        vorbis_analysis_wrote(&vd_, 0);
        Flush();
        ogg_stream_clear(&os_);
        vorbis_block_clear(&vb_);
        vorbis_dsp_clear(&vd_);
        vorbis_info_clear(&vi_);
        opened_ = false;
    }
    if (hFile_ != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile_);
        hFile_ = INVALID_HANDLE_VALUE;
    }
}

} // namespace libZFork
