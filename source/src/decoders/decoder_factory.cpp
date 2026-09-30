#include "decoder_factory.h"
#include "wav_decoder.h"
#include "aiff_decoder.h"
#include "pcm_decoder.h"
#include "cda_decoder.h"
#ifdef LIBZFORK_WITH_MP3
#  include "mp3_decoder.h"
#endif
#ifdef LIBZFORK_WITH_OGG
#  include "ogg_decoder.h"
#endif
#ifdef LIBZFORK_WITH_FLAC
#  include "flac_decoder.h"
#  include "flac_ogg_decoder.h"
#endif
#ifdef LIBZFORK_WITH_AAC
#  include "aac_decoder.h"
#  include "m4a_decoder.h"
#endif
#ifdef LIBZFORK_WITH_OPUS
#  include "opus_decoder.h"
#endif
#include <filesystem>

namespace libZFork {

std::unique_ptr<DecoderBase> CreateDecoderForFile(const wchar_t* path) {
    namespace fs = std::filesystem;
    auto ext = fs::path(path).extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(::towlower(c));

#ifdef LIBZFORK_WITH_MP3
    if (ext == L".mp3")                    return std::make_unique<Mp3Decoder>();
#endif
    if (ext == L".cda")                    return std::make_unique<CdaDecoder>();
    if (ext == L".wav" || ext == L".wave") return std::make_unique<WavDecoder>();
    if (ext == L".aif" || ext == L".aiff") return std::make_unique<AiffDecoder>();
    if (ext == L".pcm" || ext == L".raw")  return std::make_unique<PcmDecoder>();
#ifdef LIBZFORK_WITH_OGG
    if (ext == L".ogg")                    return std::make_unique<OggDecoder>();
#endif
#ifdef LIBZFORK_WITH_FLAC
    if (ext == L".flac")                   return std::make_unique<FlacDecoder>();
    if (ext == L".oga")                    return std::make_unique<FlacOggDecoder>();
#endif
#ifdef LIBZFORK_WITH_AAC
    if (ext == L".aac")                    return std::make_unique<AacDecoder>();
    if (ext == L".m4a" || ext == L".mp4")  return std::make_unique<M4aDecoder>();
#endif
#ifdef LIBZFORK_WITH_OPUS
    if (ext == L".opus")                   return std::make_unique<OpusDecoder>();
#endif
    return nullptr;
}

} // namespace libZFork
