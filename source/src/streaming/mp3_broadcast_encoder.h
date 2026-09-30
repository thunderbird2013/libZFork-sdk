#pragma once
#include <cstdint>
#include <functional>
#include <libzfork/libzfork.h>
#include <span>
#include <string>
#include <vector>
#include <lame/lame.h>

namespace libZFork {

class Mp3BroadcastEncoder {
public:
    using ByteSink = std::function<bool(std::span<const uint8_t>)>;

    ~Mp3BroadcastEncoder();

    [[nodiscard]] bool Open(int sampleRate, int inputChannels, int bitrateKbps, int stereoMode);
    [[nodiscard]] bool Encode(std::span<const float> interleavedPcm, const ByteSink& sink);
    [[nodiscard]] bool Flush(const ByteSink& sink);
    void Close();
    [[nodiscard]] const char* GetError() const { return error_.c_str(); }

private:
    void SetError(const char* msg) { error_ = msg ? msg : ""; }

    lame_global_flags* gfp_ = nullptr;
    int inputChannels_ = 2;
    int channels_ = 2;
    std::vector<uint8_t> mp3buf_;
    std::string error_;
};

} // namespace libZFork

