#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace libZFork {

class EncoderBase {
public:
    virtual ~EncoderBase() = default;

    [[nodiscard]] virtual bool Open(const wchar_t* path, int sampleRate, int channels) = 0;
    [[nodiscard]] virtual bool Write(std::span<const float> pcm) = 0;
    virtual void               Close() = 0;

    [[nodiscard]] const char* GetError() const { return error_.c_str(); }

protected:
    void SetError(std::string_view msg) { error_ = msg; }

private:
    std::string error_;
};

} // namespace libZFork
