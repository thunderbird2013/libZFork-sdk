#pragma once
#include "engine_defs.h"
#include "command_queue.h"
#include "event_dispatcher.h"
#include "mixer_graph.h"
#include "voice_pool.h"
#include "output/audio_output.h"
#include <memory>
#include <thread>
#include <cstdint>
#include <string_view>

namespace libZFork::engine {

// The top-level engine object.
//
// Owns: IAudioOutput, CommandQueue, VoicePool, MixerGraph, EventDispatcher,
//       and the engine jthread that drives the render loop.
//
// Threading contract:
//   RegisterGroup / FindGroup — call before Start(), single-threaded setup.
//   Post / Update / IsVoiceActive — any thread after Start().
//   Everything else — engine thread only (internal).
class AudioContext {
public:
    explicit AudioContext(std::unique_ptr<::libZFork::IAudioOutput> output,
                          uint32_t sampleRate = 48000,
                          uint32_t channels   = 2);
    ~AudioContext();

    // Setup (must be called before Start).
    [[nodiscard]] uint32_t RegisterGroup(std::string_view name);
    [[nodiscard]] uint32_t FindGroup(std::string_view name) const;

    // Starts the engine thread and audio output.
    // Returns false if the output fails to initialise.
    [[nodiscard]] bool Start();

    // Stops the engine thread and closes the audio output.
    // Blocks until the engine thread exits.
    void Stop();

    // Post a command from any API thread.
    [[nodiscard]] bool Post(const Command& cmd) noexcept;

    // API thread: pre-allocate a voice slot and return its stamped ID.
    // Thread-safe. Returns kInvalidVoice if the pool is full (no stealing).
    [[nodiscard]] uint32_t AllocVoice(int32_t priority = 0) noexcept;

    // Store a default event callback (used by Update()).
    void SetDefaultCallback(EventCallback cb, void* userData = nullptr) noexcept;

    // Control thread: dispatch queued engine events to the stored default callback.
    void Update() noexcept;

    // Control thread: dispatch to an explicit callback (overrides default for this call).
    void Update(EventCallback cb, void* userData = nullptr) noexcept;

    // Any thread: check if a voice is still active (uses only the atomic state).
    [[nodiscard]] bool IsVoiceActive(uint32_t voiceId) const noexcept;

    // Any thread: read post-fader stereo peak meters for a group (0..1).
    void GetGroupLevels(uint32_t groupId, float& outLeft, float& outRight) const noexcept;

    // API thread: return a pre-allocated voice that never started playback.
    void CancelAllocatedVoice(uint32_t voiceId) noexcept;

    // Any thread: install/replace an effect on a group's bus (ownership of
    // `effect` transfers to the engine thread — any existing effect in that
    // slot is deleted there). Takes effect from the next mix block. Frees
    // `effect` immediately (without installing it) if the command queue is
    // full rather than leaking it. `slot` must be < kMaxEffectSlots.
    void SetGroupEffect(uint32_t groupId, uint32_t slot, class IEffect* effect) noexcept;

    // Any thread: remove whatever effect currently occupies `slot` on `group`.
    void ClearGroupEffect(uint32_t groupId, uint32_t slot) noexcept;

    // Accessors for unit tests.
    [[nodiscard]] VoicePool&   Pool()   noexcept { return pool_; }
    [[nodiscard]] MixerGraph&  Graph()  noexcept { return graph_; }
    [[nodiscard]] uint32_t     SampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] uint32_t     Channels()   const noexcept { return channels_; }

    // Diagnostics: which IAudioOutput backend actually ended up active
    // (e.g. AutoOutput picks WASAPI-exclusive or falls back to WaveOut).
    [[nodiscard]] const ::libZFork::IAudioOutput* Output() const noexcept { return output_.get(); }

private:
    void EngineLoop(std::stop_token tok);
    void ProcessCommands();
    void HandleDoneVoices();

    uint32_t sampleRate_;
    uint32_t channels_;

    std::unique_ptr<::libZFork::IAudioOutput> output_;
    CommandQueue<1024>                    cmdQueue_;
    VoicePool                             pool_;
    MixerGraph                            graph_;
    EventDispatcher<256>                  events_;

    std::jthread  engineThread_;
    EventCallback defaultCb_     = nullptr;
    void*         defaultCbData_ = nullptr;
};

} // namespace libZFork::engine
