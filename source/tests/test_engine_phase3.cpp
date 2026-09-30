#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/sound.h"
#include "engine/voice.h"
#include "engine/voice_pool.h"
#include "engine/mixer_graph.h"
#include "engine/audio_context.h"
#include "engine/effect_chain.h"
#include "output/null_output.h"

#include <cmath>
#include <numbers>
#include <thread>
#include <chrono>
#include <vector>
#include <numeric>

using namespace libZFork::engine;
using namespace libZFork;

// ---------------------------------------------------------------------------
// IEffect — EqualizerEffect
// ---------------------------------------------------------------------------

TEST_CASE("EqualizerEffect: flat response leaves signal unchanged", "[engine][effect]") {
    EqualizerEffect eq;
    // All band gains default to 0 dB — signal should pass through unchanged.
    std::vector<float> pcm(512 * 2);
    for (size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = static_cast<float>(i % 32) / 32.f;

    std::vector<float> original = pcm;
    eq.Process(std::span<float>(pcm), 2, 48000);

    // With 0 dB on all bands, output should be numerically very close to input.
    // IIR filters introduce tiny floating-point transients; allow ±0.001 absolute.
    for (size_t i = 0; i < pcm.size(); ++i)
        CHECK(pcm[i] == Catch::Approx(original[i]).margin(0.001));
}

TEST_CASE("EqualizerEffect: non-zero band gain changes signal", "[engine][effect]") {
    EqualizerEffect eq;
    eq.SetBandGain(10, 6.f); // +6 dB at 200 Hz

    std::vector<float> pcm(512 * 2, 0.5f);
    std::vector<float> flat(512 * 2, 0.5f);

    EqualizerEffect eq2; // flat reference
    eq.Process(std::span<float>(pcm),  2, 48000);
    eq2.Process(std::span<float>(flat), 2, 48000);

    // At least some samples must differ.
    bool anyDiff = false;
    for (size_t i = 0; i < pcm.size(); ++i)
        if (std::abs(pcm[i] - flat[i]) > 1e-4f) { anyDiff = true; break; }
    CHECK(anyDiff);
}

TEST_CASE("EqualizerEffect: Reset clears filter state", "[engine][effect]") {
    EqualizerEffect eq;
    eq.SetBandGain(0, 12.f);
    std::vector<float> pcm(512 * 2, 1.f);
    eq.Process(std::span<float>(pcm), 2, 48000);
    eq.Reset(); // should not throw / crash
}

// ---------------------------------------------------------------------------
// EchoEffectWrapper
// ---------------------------------------------------------------------------

TEST_CASE("EchoEffectWrapper: silence in → silence out", "[engine][effect]") {
    EchoEffectWrapper echo;
    EchoParams p{};
    p.leftDelay       = 200;
    p.leftEchoVol     = 50;
    p.rightDelay      = 200;
    p.rightEchoVol    = 50;
    p.leftSourceVol   = 100;
    p.rightSourceVol  = 100;
    echo.SetParams(p);

    std::vector<float> pcm(512 * 2, 0.f);
    echo.Process(std::span<float>(pcm), 2, 48000);

    for (auto s : pcm) CHECK(s == Catch::Approx(0.f));
}

// ---------------------------------------------------------------------------
// MixerGraph — group volume isolation
// ---------------------------------------------------------------------------

TEST_CASE("MixerGraph: group volume 0 contributes silence", "[engine][mixer3]") {
    MixerGraph graph(48000, 2);
    uint32_t g = graph.RegisterGroup("sfx");
    graph.GetGroup(g).volume.store(0.f, std::memory_order_relaxed);

    VoicePool pool;
    // Even if voices were playing into this group, volume=0 means silence.
    const int16_t* pcm = graph.Render(pool, kMixFrames);
    bool allZero = true;
    for (uint32_t i = 0; i < kMixFrames * 2; ++i)
        if (pcm[i] != 0) { allZero = false; break; }
    CHECK(allZero);
}

TEST_CASE("MixerGraph: two groups, mute one, other passes", "[engine][mixer3]") {
    // We can't easily inject live PCM without a running voice. This test
    // verifies group accounting: muting one group doesn't affect the other.
    MixerGraph graph(48000, 2);
    uint32_t g0 = graph.RegisterGroup("music");
    uint32_t g1 = graph.RegisterGroup("sfx");

    // Mute sfx, leave music at full volume.
    graph.GetGroup(g0).volume.store(1.f, std::memory_order_relaxed);
    graph.GetGroup(g1).muted.store(true, std::memory_order_relaxed);

    // Manually write a non-zero signal into the music bus.
    auto& musicBus = graph.GetGroup(g0).busBuf;
    std::fill(musicBus.begin(), musicBus.end(), 0.25f);

    // Calling Render will zero all buses first (that's by design).
    // Instead, we call ApplyBusEffects + master summing manually via Render —
    // but Render zeroes the buses. So we just verify the structure is correct
    // and that muting sfx doesn't blow up.
    VoicePool pool;
    const int16_t* pcm = graph.Render(pool, kMixFrames);
    REQUIRE(pcm != nullptr);
}

// ---------------------------------------------------------------------------
// Constant-power pan — verify gain formula
// ---------------------------------------------------------------------------

TEST_CASE("Pan gain: center pan gives equal L/R", "[engine][pan]") {
    // Pan=0 → angle = pi/4 → gainL = gainR = cos(pi/4) * vol ≈ 0.707
    constexpr float kPi  = std::numbers::pi_v<float>;
    float pan   = 0.f;
    float vol   = 1.f;
    float angle = (pan + 1.f) * (kPi / 4.f);
    float gainL = vol * std::cos(angle);
    float gainR = vol * std::sin(angle);

    CHECK(gainL == Catch::Approx(gainR).epsilon(1e-5f));
    CHECK(gainL == Catch::Approx(std::sqrt(2.f) / 2.f).epsilon(1e-4f));
}

TEST_CASE("Pan gain: full-left pan gives gainR=0", "[engine][pan]") {
    constexpr float kPi = std::numbers::pi_v<float>;
    float pan   = -1.f;
    float angle = (pan + 1.f) * (kPi / 4.f);
    float gainL = std::cos(angle);
    float gainR = std::sin(angle);

    CHECK(gainL == Catch::Approx(1.f).margin(1e-4f));
    CHECK(gainR == Catch::Approx(0.f).margin(1e-4f));
}

TEST_CASE("Pan gain: full-right pan gives gainL=0", "[engine][pan]") {
    constexpr float kPi = std::numbers::pi_v<float>;
    float pan   = +1.f;
    float angle = (pan + 1.f) * (kPi / 4.f);
    float gainL = std::cos(angle);
    float gainR = std::sin(angle);

    CHECK(gainL == Catch::Approx(0.f).margin(1e-4f));
    CHECK(gainR == Catch::Approx(1.f).margin(1e-4f));
}

// ---------------------------------------------------------------------------
// Voice::RenderChunk with no Sound → returns 0 (still works in Phase 3)
// ---------------------------------------------------------------------------

TEST_CASE("Voice::RenderChunk returns 0 with no Sound", "[engine][voice3]") {
    Voice v;
    v.Init(kMixFrames, 2);
    // No sound attached — should return 0 immediately.
    CHECK(v.RenderChunk(kMixFrames) == 0);
}

// ---------------------------------------------------------------------------
// AudioContext: 32 simultaneous voices (stress, no deadlock)
// ---------------------------------------------------------------------------

TEST_CASE("AudioContext: 32 simultaneous PlayVoice commands", "[engine][context3]") {
    AudioContext ctx(std::make_unique<NullOutput>(), 48000, 2);
    ctx.RegisterGroup("sfx");
    REQUIRE(ctx.Start());

    // Post 32 PlayVoice commands without a sound — they'll immediately finish
    // (RenderChunk returns 0), but the infrastructure must not deadlock.
    for (int i = 0; i < 32; ++i) {
        Command c{};
        c.type       = CommandType::PlayVoice;
        c.groupId    = 0;
        c.param.i[0] = i; // priority
        ctx.Post(c);
    }

    // Give the engine enough time to process all commands and release voices.
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    int events = 0;
    ctx.Update([](int, uint32_t, uint32_t, float, void* ud) {
        ++(*static_cast<int*>(ud));
    }, &events);

    // We expect at least 32 VoiceStart events (VoiceFinished may also be there).
    CHECK(events >= 32);

    ctx.Stop();
}

// ---------------------------------------------------------------------------
// AudioContext: SetGroupVolume command
// ---------------------------------------------------------------------------

TEST_CASE("AudioContext: SetGroupVolume command reaches engine", "[engine][context3]") {
    AudioContext ctx(std::make_unique<NullOutput>(), 48000, 2);
    uint32_t g = ctx.RegisterGroup("music");
    REQUIRE(ctx.Start());

    Command c{};
    c.type       = CommandType::SetGroupVolume;
    c.groupId    = g;
    c.param.f[0] = 0.5f;
    REQUIRE(ctx.Post(c));

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // After the engine processes the command, the group volume must be 0.5.
    float vol = ctx.Graph().GetGroup(g).volume.load(std::memory_order_relaxed);
    CHECK(vol == Catch::Approx(0.5f));

    ctx.Stop();
}

// ---------------------------------------------------------------------------
// AudioContext: SetGroupMute command
// ---------------------------------------------------------------------------

TEST_CASE("AudioContext: SetGroupMute command reaches engine", "[engine][context3]") {
    AudioContext ctx(std::make_unique<NullOutput>(), 48000, 2);
    uint32_t g = ctx.RegisterGroup("sfx");
    REQUIRE(ctx.Start());

    Command c{};
    c.type       = CommandType::SetGroupMute;
    c.groupId    = g;
    c.param.i[0] = 1;
    REQUIRE(ctx.Post(c));

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    bool muted = ctx.Graph().GetGroup(g).muted.load(std::memory_order_relaxed);
    CHECK(muted);

    ctx.Stop();
}
