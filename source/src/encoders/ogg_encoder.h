#pragma once
#include "encoder_base.h"
#include <vorbis/vorbisenc.h>
#include "utils/win_compat.h"

namespace libZFork {

class OggEncoder final : public EncoderBase {
public:
    OggEncoder() = default;
    ~OggEncoder() override { Close(); }

    bool Open(const wchar_t* path, int sampleRate, int channels) override;
    bool Write(std::span<const float> pcm) override;
    void Close() override;

private:
    void Flush();

    ogg_stream_state os_{};
    vorbis_info      vi_{};
    vorbis_dsp_state vd_{};
    vorbis_block     vb_{};
    ogg_page         og_{};
    ogg_packet       op_{};
    HANDLE           hFile_    = INVALID_HANDLE_VALUE;
    int              channels_ = 2;
    bool             opened_   = false;
};

} // namespace libZFork
