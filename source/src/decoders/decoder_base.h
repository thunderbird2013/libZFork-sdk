#pragma once
#include <libzfork/libzfork.h>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace libZFork {

class DecoderBase {
public:
    virtual ~DecoderBase() = default;

    [[nodiscard]] virtual bool OpenFile(const wchar_t* path) = 0;
    [[nodiscard]] virtual bool OpenMemory(const uint8_t* data, uint32_t size) = 0;
    virtual void               CloseStream() = 0;

    // Decode up to maxFrames interleaved float samples. Returns frames decoded.
    [[nodiscard]] virtual uint32_t DecodeFloat(float* outPCM, uint32_t maxFrames) = 0;

    [[nodiscard]] virtual bool     Seek(SeekOrigin origin, const StreamTime& time) = 0;
    [[nodiscard]] virtual StreamTime GetPosition() const = 0;
    [[nodiscard]] virtual StreamTime GetLength()   const = 0;

    [[nodiscard]] virtual int  SampleRate()  const = 0;
    [[nodiscard]] virtual int  Channels()    const = 0;
    [[nodiscard]] virtual int  Bitrate()     const = 0;
    [[nodiscard]] virtual bool IsVBR()       const = 0;

    [[nodiscard]] const char* GetError() const { return error_.c_str(); }

protected:
    void SetError(std::string_view msg) { error_ = msg; }

    // Helper: build StreamTime from sample position
    static StreamTime SamplesToTime(uint64_t samples, int sampleRate) {
        StreamTime t{};
        t.samples = samples;
        if (sampleRate > 0) {
            t.ms  = static_cast<uint32_t>((samples * 1000ULL) / sampleRate);
            t.sec = t.ms / 1000;
            t.hms.millisecond = t.ms % 1000;
            t.hms.second      = t.sec % 60;
            t.hms.minute      = (t.sec / 60) % 60;
            t.hms.hour        = t.sec / 3600;
        }
        return t;
    }

    // zfork_SeekMs and SamplesToTime store total elapsed milliseconds in time.ms.
    static uint64_t TimeToTotalMs(const StreamTime& time, int sampleRate) {
        if (time.samples > 0 && sampleRate > 0) {
            return (time.samples * 1000ULL) / static_cast<uint64_t>(sampleRate);
        }
        return static_cast<uint64_t>(time.ms);
    }

    static uint64_t TimeToSamplePos(const StreamTime& time, int sampleRate) {
        if (sampleRate <= 0) return 0;
        if (time.samples > 0) return time.samples;
        return (TimeToTotalMs(time, sampleRate) * static_cast<uint64_t>(sampleRate)) / 1000ULL;
    }

private:
    std::string error_;
};

} // namespace libZFork
