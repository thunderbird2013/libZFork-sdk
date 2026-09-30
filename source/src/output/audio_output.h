#pragma once
#include <libzfork/libzfork.h>
#include <cstdint>
#include <memory>

namespace libZFork {

// Which IAudioOutput backend CreatePlatformAudioOutput() should use.
// Auto (the default): try WASAPI exclusive/event-driven first (lowest
// latency), fall back to WaveOut/MME transparently if that fails to
// initialise (device already held exclusively by another app, format not
// supported in exclusive mode, virtual/remote audio device, ...).
// Wasapi: force WASAPI; Init() fails (no fallback) if it can't be used —
// pick this when the caller wants to know explicitly rather than silently
// downgrade. WaveOut: force the classic MME backend (max compatibility).
enum class AudioBackendPreference : int {
    Auto    = 0,
    Wasapi  = 1,
    WaveOut = 2,
};

// Platform-neutral audio output interface.
// Owns the OS audio handle and the output buffer pool.
// All methods (except WriteBlock/Drain) may be called from the API thread.
// WriteBlock and Drain are called exclusively from the decode/engine thread.
class IAudioOutput {
public:
    virtual ~IAudioOutput() = default;

    // Open and configure the audio device.
    // Must be called before Start(). Returns false on failure.
    [[nodiscard]] virtual bool Init(int sampleRate, int channels,
                                    int bitsPerSample, uint32_t bufferMs) = 0;

    // Begin playback (device starts consuming submitted blocks).
    [[nodiscard]] virtual bool Start() = 0;

    // Pause/resume output (does not close the device).
    virtual void Pause()  = 0;
    virtual void Resume() = 0;

    // Signal stop: unblocks any waiting WriteBlock, resets queued buffers.
    // Safe to call from any thread.
    virtual void Stop() = 0;

    // Close the device handle. Call after the decode thread has exited.
    virtual void Close() = 0;

    // Wait until all submitted blocks have finished playing.
    // Returns early if Stop() was called.
    virtual void Drain() = 0;

    // Submit interleaved int16 PCM. Blocks until all frames are queued or
    // Stop() is called. Returns false if stopped mid-write.
    // Called exclusively from the decode/engine thread.
    [[nodiscard]] virtual bool WriteBlock(const int16_t* pcm, uint32_t frames) = 0;

    // Set hardware/OS output volume. [0, 100] per channel.
    virtual void SetMasterVolume(int left, int right) = 0;

    // Device enumeration (may be called from any thread).
    [[nodiscard]] virtual int  EnumerateDevices() const = 0;
    [[nodiscard]] virtual bool GetDeviceInfo(int id, WaveDeviceInfo& out) const = 0;
    [[nodiscard]] virtual bool SetDevice(int id) = 0;

    // Redirect output to a WAV file instead of hardware (optional feature).
    [[nodiscard]] virtual bool SetOutputFile(const wchar_t* path) = 0;
};

// Factory — returns the platform-appropriate implementation.
// On Windows: AutoOutput (WASAPI-exclusive with WaveOut fallback), honouring
// `pref`. On Linux/Unix: PortAudioOutput when LIBZFORK_WITH_PORTAUDIO is set,
// otherwise NullOutput (`pref` is ignored).
[[nodiscard]] std::unique_ptr<IAudioOutput> CreatePlatformAudioOutput(
    AudioBackendPreference pref = AudioBackendPreference::Auto);

} // namespace libZFork
