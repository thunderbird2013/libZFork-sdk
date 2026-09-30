#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace libZFork { class DecoderBase; }

namespace libZFork::engine {

enum class SoundType { PreDecoded, Streaming };

// A loaded audio asset.
//
// PreDecoded: entire PCM is decoded on load and cached in pcmData_.
//             Voices read directly from the buffer with a per-voice cursor.
//
// Streaming:  only file metadata is cached; each voice creates its own
//             DecoderBase instance via CreateDecoder().
//
// Threading: Sound is immutable after construction and is safe to share
//            across threads (shared_ptr ref-counting).
class Sound {
public:
    // Load from file. Returns nullptr on failure.
    [[nodiscard]] static std::shared_ptr<Sound> LoadFile(
        const wchar_t* path,
        SoundType type = SoundType::PreDecoded);

    // Load from an in-memory buffer (e.g. a blob embedded in a SoundBankV1
    // .bank file — see SoundBankV1::ConvertTextToBinary's embedAudioFiles
    // option). `extHint` picks the decoder exactly like a file extension
    // would ("ogg", ".wav", any case) since there's no path to infer it
    // from. For Streaming, the buffer is copied and kept alive so
    // CreateDecoder() can reopen it per voice; for PreDecoded it's only
    // needed transiently (decoded immediately, not retained).
    // Returns nullptr on failure.
    [[nodiscard]] static std::shared_ptr<Sound> LoadFromMemory(
        const uint8_t* data, size_t size,
        const wchar_t* extHint,
        SoundType type = SoundType::PreDecoded);

    SoundType Type()       const noexcept { return type_; }
    int       SampleRate() const noexcept { return sampleRate_; }
    int       Channels()   const noexcept { return channels_; }
    uint32_t  FrameCount() const noexcept { return frameCount_; }

    // PreDecoded: interleaved float samples (frameCount * channels floats).
    const std::vector<float>& PCMData() const noexcept { return pcmData_; }

    // Streaming: create a fresh DecoderBase seeked to the start.
    // Returns nullptr for PreDecoded sounds or if the file cannot be re-opened.
    [[nodiscard]] std::unique_ptr<libZFork::DecoderBase> CreateDecoder() const;

private:
    Sound() = default;

    SoundType          type_       = SoundType::PreDecoded;
    int                sampleRate_ = 0;
    int                channels_   = 0;
    uint32_t           frameCount_ = 0;
    std::vector<float> pcmData_;    // PreDecoded only
    std::wstring       filePath_;   // Streaming, disk-backed only
    bool               isMemory_   = false;
    std::vector<uint8_t> memData_; // Streaming, memory-backed only (LoadFromMemory)
    std::wstring       memExt_;    // decoder hint for memData_ (no dot, lowercase)
};

} // namespace libZFork::engine
