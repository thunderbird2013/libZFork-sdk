#include "engine/sound.h"
#include "decoders/decoder_base.h"
#include "decoders/wav_decoder.h"
#include "decoders/pcm_decoder.h"
#include "decoders/aiff_decoder.h"
#ifdef LIBZFORK_WITH_MP3
#  include "decoders/mp3_decoder.h"
#endif
#ifdef LIBZFORK_WITH_OGG
#  include "decoders/ogg_decoder.h"
#endif
#ifdef LIBZFORK_WITH_FLAC
#  include "decoders/flac_decoder.h"
#  include "decoders/flac_ogg_decoder.h"
#endif
#ifdef LIBZFORK_WITH_OPUS
#  include "decoders/opus_decoder.h"
#endif
#ifdef LIBZFORK_WITH_AAC
#  include "decoders/aac_decoder.h"
#endif

#include <algorithm>
#include <cwctype>
#include <string>

namespace libZFork::engine {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::wstring ExtensionLower(const wchar_t* path) {
    std::wstring s(path);
    auto pos = s.rfind(L'.');
    if (pos == std::wstring::npos) return {};
    std::wstring ext = s.substr(pos + 1);
    for (auto& c : ext) c = static_cast<wchar_t>(::towlower(c));
    return ext;
}

static std::unique_ptr<DecoderBase> MakeDecoder(const std::wstring& ext) {
    if (ext == L"wav")                           return std::make_unique<WavDecoder>();
    if (ext == L"pcm" || ext == L"raw")          return std::make_unique<PcmDecoder>();
    if (ext == L"aif" || ext == L"aiff")         return std::make_unique<AiffDecoder>();
#ifdef LIBZFORK_WITH_MP3
    if (ext == L"mp3")                           return std::make_unique<Mp3Decoder>();
#endif
#ifdef LIBZFORK_WITH_FLAC
    if (ext == L"flac")                          return std::make_unique<FlacDecoder>();
#endif
#ifdef LIBZFORK_WITH_OPUS
    if (ext == L"opus")                          return std::make_unique<OpusDecoder>();
#endif
#ifdef LIBZFORK_WITH_AAC
    if (ext == L"aac" || ext == L"m4a")         return std::make_unique<AacDecoder>();
#endif
    // .ogg/.oga is handled by OpenDecoderForFile(): the container may hold
    // either Vorbis or FLAC, which can only be told apart by actually trying
    // to open it.
    return nullptr;
}

// .ogg/.oga containers can hold either Vorbis or FLAC-in-Ogg audio. Trying to
// force every .ogg through FlacOggDecoder silently "succeeds" (libFLAC opens
// the file handle fine) but never finds valid STREAMINFO metadata for a
// plain Vorbis stream, leaving channels/sampleRate at 0 and DecodeFloat()
// producing zero frames forever — the bank loads (EntryCount is correct)
// but every such sound is completely silent. So: try FlacOgg first and only
// keep it if it actually parsed real stream metadata; otherwise fall back to
// the Vorbis decoder.
static std::unique_ptr<DecoderBase> OpenOggFile(const wchar_t* path) {
#ifdef LIBZFORK_WITH_FLAC
    auto flacOgg = std::make_unique<FlacOggDecoder>();
    if (flacOgg->OpenFile(path) && flacOgg->SawStreamInfo())
        return flacOgg;
#endif
#ifdef LIBZFORK_WITH_OGG
    auto vorbis = std::make_unique<OggDecoder>();
    if (vorbis->OpenFile(path)) return vorbis;
#endif
    return nullptr;
}

// Builds and opens the right decoder for `path`, handling the .ogg/.oga
// ambiguity above. Returns nullptr if the extension is unsupported or the
// file could not be opened.
static std::unique_ptr<DecoderBase> OpenDecoderForFile(const wchar_t* path, const std::wstring& ext) {
#if defined(LIBZFORK_WITH_OGG) || defined(LIBZFORK_WITH_FLAC)
    if (ext == L"ogg" || ext == L"oga") return OpenOggFile(path);
#endif
    auto dec = MakeDecoder(ext);
    if (!dec || !dec->OpenFile(path)) return nullptr;
    return dec;
}

// Memory-buffer counterparts, for Sound::LoadFromMemory() / re-opening a
// memory-backed Streaming sound in CreateDecoder(). Same .ogg/.oga ambiguity
// handling as the file path above.
static std::unique_ptr<DecoderBase> OpenOggMemory(const uint8_t* data, uint32_t size) {
#ifdef LIBZFORK_WITH_FLAC
    auto flacOgg = std::make_unique<FlacOggDecoder>();
    if (flacOgg->OpenMemory(data, size) && flacOgg->SawStreamInfo())
        return flacOgg;
#endif
#ifdef LIBZFORK_WITH_OGG
    auto vorbis = std::make_unique<OggDecoder>();
    if (vorbis->OpenMemory(data, size)) return vorbis;
#endif
    return nullptr;
}

static std::unique_ptr<DecoderBase> OpenDecoderForMemory(const uint8_t* data, uint32_t size,
                                                         const std::wstring& ext) {
#if defined(LIBZFORK_WITH_OGG) || defined(LIBZFORK_WITH_FLAC)
    if (ext == L"ogg" || ext == L"oga") return OpenOggMemory(data, size);
#endif
    auto dec = MakeDecoder(ext);
    if (!dec || !dec->OpenMemory(data, size)) return nullptr;
    return dec;
}

// ---------------------------------------------------------------------------
// Sound::LoadFile
// ---------------------------------------------------------------------------

std::shared_ptr<Sound> Sound::LoadFile(const wchar_t* path, SoundType type) {
    std::wstring ext = ExtensionLower(path);
    auto dec = OpenDecoderForFile(path, ext);
    if (!dec) return nullptr;

    auto snd = std::shared_ptr<Sound>(new Sound());
    snd->type_       = type;
    snd->sampleRate_ = dec->SampleRate();
    snd->channels_   = dec->Channels();
    snd->filePath_   = path;

    if (type == SoundType::PreDecoded) {
        // Drain the decoder into pcmData_.
        constexpr uint32_t kBatchFrames = 4096;
        const int ch = dec->Channels();
        std::vector<float> batch(static_cast<size_t>(kBatchFrames) * ch);
        uint32_t frames = 0;
        for (;;) {
            uint32_t got = dec->DecodeFloat(batch.data(), kBatchFrames);
            if (got == 0) break;
            snd->pcmData_.insert(snd->pcmData_.end(),
                                 batch.begin(),
                                 batch.begin() + got * ch);
            frames += got;
        }
        snd->frameCount_ = frames;
        dec->CloseStream();
    } else {
        // Streaming: store only metadata; the decoder is closed and re-opened per voice.
        snd->frameCount_ = 0; // unknown until streamed
        dec->CloseStream();
    }

    return snd;
}

// ---------------------------------------------------------------------------
// Sound::LoadFromMemory
// ---------------------------------------------------------------------------

std::shared_ptr<Sound> Sound::LoadFromMemory(const uint8_t* data, size_t size,
                                             const wchar_t* extHint, SoundType type) {
    if (!data || size == 0) return nullptr;

    std::wstring ext = extHint ? extHint : L"";
    if (!ext.empty() && ext.front() == L'.') ext.erase(0, 1);
    for (auto& c : ext) c = static_cast<wchar_t>(::towlower(c));

    auto dec = OpenDecoderForMemory(data, static_cast<uint32_t>(size), ext);
    if (!dec) return nullptr;

    auto snd = std::shared_ptr<Sound>(new Sound());
    snd->type_       = type;
    snd->sampleRate_ = dec->SampleRate();
    snd->channels_   = dec->Channels();

    if (type == SoundType::PreDecoded) {
        constexpr uint32_t kBatchFrames = 4096;
        const int ch = dec->Channels();
        std::vector<float> batch(static_cast<size_t>(kBatchFrames) * ch);
        uint32_t frames = 0;
        for (;;) {
            uint32_t got = dec->DecodeFloat(batch.data(), kBatchFrames);
            if (got == 0) break;
            snd->pcmData_.insert(snd->pcmData_.end(),
                                 batch.begin(),
                                 batch.begin() + got * ch);
            frames += got;
        }
        snd->frameCount_ = frames;
        dec->CloseStream();
    } else {
        // Streaming: keep our own copy of the bytes so CreateDecoder() can
        // reopen a fresh decoder per voice, same as a disk path would.
        snd->frameCount_ = 0;
        snd->isMemory_   = true;
        snd->memData_.assign(data, data + size);
        snd->memExt_     = ext;
        dec->CloseStream();
    }

    return snd;
}

// ---------------------------------------------------------------------------
// Sound::CreateDecoder (Streaming)
// ---------------------------------------------------------------------------

std::unique_ptr<DecoderBase> Sound::CreateDecoder() const {
    if (type_ != SoundType::Streaming) return nullptr;
    if (isMemory_)
        return OpenDecoderForMemory(memData_.data(), static_cast<uint32_t>(memData_.size()), memExt_);
    std::wstring ext = ExtensionLower(filePath_.c_str());
    return OpenDecoderForFile(filePath_.c_str(), ext);
}

} // namespace libZFork::engine
