#pragma once
#include "engine_defs.h"
#include "engine/sound.h"
#include "engine/effect_chain.h"
#include "effects/soundtouch_proc.h"
#include "decoders/decoder_base.h"
#include <atomic>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace libZFork::engine {

enum class VoiceState : uint8_t {
    Free,
    Starting,   // claimed by API thread, not yet initialized by engine
    Playing,
    Paused,
    Stopping,   // engine will finish current block then release
    Done,       // render returned 0 frames; engine will release next cycle
};

// Per-voice parameters written by the API thread, read by the engine thread.
// All fields are atomic so cross-thread access is race-free.
struct VoiceParams {
    std::atomic<float>   volume    { 1.0f };
    std::atomic<float>   pan       { 0.0f };    // -1 (left) .. +1 (right)
    std::atomic<float>   pitch     { 0.0f };    // semitones, pitch-shift (key)
    std::atomic<float>   tempo     { 1.0f };    // 0.5–2.0, tempo w/o pitch (key-lock)
    std::atomic<float>   posX      { 0.0f };    // world-space position
    std::atomic<float>   posY      { 0.0f };
    std::atomic<float>   posZ      { 0.0f };
    std::atomic<int32_t> loopCount { 0 };       // 0 = no loop, -1 = infinite
    std::atomic<int32_t> priority  { 0 };       // higher = survives stealing longer
    std::atomic<bool>    positional{ false };   // use spatial pan/attenuation
};

// A position marker on a voice — fires once when playback crosses the position.
struct VoiceMarker {
    uint32_t framePos  = 0;     // position in frames (sound's sample rate)
    uint32_t markerId  = 0;     // user-assigned identifier
    bool     fired     = false;
};

// A Voice is one active sound playback slot owned exclusively by the engine thread
// (except for the atomic VoiceParams members, which the API thread may write).
class Voice {
public:
    uint32_t       id         = 0;   // generation-stamped: (gen<<16)|slotIndex
    uint32_t       groupIndex = 0;   // index into MixerGraph::groups_

    // state is atomic so IsValid() can be called from the API thread.
    std::atomic<VoiceState> state{ VoiceState::Free };

    VoiceParams    params;

    // Sound asset — set by the engine thread when processing a PlayVoice command.
    std::shared_ptr<Sound>        sound_;
    std::unique_ptr<DecoderBase>  decoder_;      // non-null for Streaming voices
    uint32_t                      readCursor_ = 0; // frame index (PreDecoded)

    // Per-voice effect chain — engine thread only (no atomics needed).
    std::array<std::unique_ptr<IEffect>, kMaxEffectSlots> voiceEffects_{};

    // Position markers — filled by SetVoiceMarker commands.
    std::array<VoiceMarker, kMaxMarkers> markers_{};
    uint32_t                             markerCount_     = 0;
    uint32_t                             firedMarkerMask_ = 0; // bit N = markers_[N] fired

    // Pre-allocated scratch buffer (kMixFrames * channels * sizeof(float)).
    // Written by RenderChunk, read by MixerGraph::Render to accumulate into group bus.
    std::vector<float> mixBuf;

    // Time-stretch for tempo / sync (SoundTouch tempo-only).
    std::unique_ptr<::libZFork::SoundTouchProc> timeStretch_;
    float appliedTempo_ = 1.f;
    float appliedPitch_ = 0.f;
    std::vector<float> decodeScratch_;

    // Engine audio format — set in Init.
    uint32_t engineChannels_   = 2;
    uint32_t engineSampleRate_ = 48000;

    // Sample-rate correction state, used only when sound_->SampleRate() !=
    // engineSampleRate_ (e.g. a 48 kHz asset played through a 44.1 kHz
    // engine — without this it plays back pitched down/"muffled").
    //
    // PreDecoded: preciseCursor_ is a fractional frame index directly into
    // the full PCM buffer (random access, no cross-call state needed).
    //
    // Streaming: decoding is sequential/pull-based, so we keep the last two
    // decoded source frames (resampleCur_/resampleNext_) and a fractional
    // position between them (resampleFrac_ in [0,1)), advancing by
    // (srcRate / dstRate) per output frame and pulling a fresh source frame
    // whenever the fraction crosses 1.0.
    double              preciseCursor_   = 0.0;
    std::vector<float>  resampleCur_;
    std::vector<float>  resampleNext_;
    double              resampleFrac_    = 0.0;
    bool                resamplePrimed_  = false;
    // Batch buffer for DecodeStreamingResampled — avoids DecodeFloat(..., 1)
    // per source frame (virtual + leftover overhead on every pull).
    std::vector<float>  resamplePullBuf_;
    uint32_t            resamplePullPos_   = 0;
    uint32_t            resamplePullCount_ = 0;

    // Initialise the mix buffer for a context with given channel count and sample rate.
    void Init(uint32_t mixFrames, uint32_t channels, uint32_t sampleRate = 48000);

    // Called by the engine thread each mix cycle.
    // Writes up to `frames` interleaved frames into mixBuf.
    // Applies per-voice effect chain after decoding.
    // Sets bits in firedMarkerMask_ for any markers crossed.
    // Returns frames produced. 0 = voice finished (will be released).
    uint32_t RenderChunk(uint32_t frames);

    // Reset back to Free state (called by VoicePool::Release).
    void Reset();

private:
    uint32_t DecodeFrames(float* dst, uint32_t frames, uint32_t soundCh, uint32_t engineCh);
    uint32_t DecodeStreamingResampled(float* dst, uint32_t frames, uint32_t soundCh,
                                      uint32_t engineCh, int srcRate, int dstRate);
    uint32_t RenderWithTimeStretch(uint32_t frames, uint32_t engineCh,
                                   float tempo, float pitchSemitones);
    void ApplyVoiceEffects(std::span<float> pcm, uint32_t channels);
};

} // namespace libZFork::engine
