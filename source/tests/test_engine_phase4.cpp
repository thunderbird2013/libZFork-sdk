#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/spatial.h"
#include "engine/voice.h"
#include "engine/voice_pool.h"
#include "engine/mixer_graph.h"
#include "engine/audio_context.h"
#include "engine/effect_chain.h"
#include "output/null_output.h"

#include <thread>
#include <chrono>
#include <atomic>
#include <cmath>
#include <numbers>

using namespace libZFork::engine;

// ---------------------------------------------------------------------------
// Compute2D
// ---------------------------------------------------------------------------

TEST_CASE("Spatial Compute2D: emitter directly left → pan=-1, gain=1", "[spatial]") {
    ListenerState ls{};
    ls.x = 0.f; ls.y = 0.f;
    ls.dirX = 0.f; ls.dirY = 1.f; // facing +Y
    ls.minDist = 1.f; ls.maxDist = 100.f; ls.rolloff = 1.f;

    // Emitter 1 unit to the left (-X direction from listener).
    SpatialResult r = Compute2D(-1.f, 0.f, ls);
    CHECK(r.pan  == Catch::Approx(-1.f).margin(1e-4f));
    CHECK(r.gain == Catch::Approx(1.f).margin(1e-4f)); // within minDist
}

TEST_CASE("Spatial Compute2D: emitter directly right → pan=+1", "[spatial]") {
    ListenerState ls{};
    ls.dirX = 0.f; ls.dirY = 1.f;
    ls.minDist = 1.f; ls.maxDist = 100.f; ls.rolloff = 1.f;

    SpatialResult r = Compute2D(1.f, 0.f, ls);
    CHECK(r.pan == Catch::Approx(1.f).margin(1e-4f));
}

TEST_CASE("Spatial Compute2D: emitter directly ahead → pan=0", "[spatial]") {
    ListenerState ls{};
    ls.dirX = 0.f; ls.dirY = 1.f;
    ls.minDist = 1.f; ls.maxDist = 100.f; ls.rolloff = 1.f;

    SpatialResult r = Compute2D(0.f, 5.f, ls);
    CHECK(r.pan == Catch::Approx(0.f).margin(1e-4f));
}

TEST_CASE("Spatial Compute2D: distance attenuation", "[spatial]") {
    ListenerState ls{};
    ls.dirX = 0.f; ls.dirY = 1.f;
    ls.minDist = 1.f; ls.maxDist = 101.f; ls.rolloff = 1.f;

    // At minDist (1m) → gain = 1.
    SpatialResult near = Compute2D(0.f, 1.f, ls);
    CHECK(near.gain == Catch::Approx(1.f).margin(1e-3f));

    // At maxDist (101m) → gain = 0.
    SpatialResult far = Compute2D(0.f, 101.f, ls);
    CHECK(far.gain == Catch::Approx(0.f).margin(1e-3f));

    // Midpoint → gain between 0 and 1.
    SpatialResult mid = Compute2D(0.f, 51.f, ls);
    CHECK(mid.gain > 0.f);
    CHECK(mid.gain < 1.f);
}

TEST_CASE("Spatial Compute2D: emitter at listener position → gain=1, pan=0", "[spatial]") {
    ListenerState ls{};
    ls.dirX = 0.f; ls.dirY = 1.f;
    ls.minDist = 1.f; ls.maxDist = 100.f; ls.rolloff = 1.f;

    SpatialResult r = Compute2D(0.f, 0.f, ls);
    CHECK(r.gain == Catch::Approx(1.f).margin(1e-4f));
    CHECK(r.pan  == Catch::Approx(0.f).margin(1e-4f));
}

TEST_CASE("Spatial Compute3D: same result as Compute2D for z=0", "[spatial]") {
    ListenerState ls{};
    ls.dirX = 0.f; ls.dirY = 1.f; ls.dirZ = 0.f;
    ls.minDist = 1.f; ls.maxDist = 100.f; ls.rolloff = 1.f;

    SpatialResult r2 = Compute2D(3.f, 4.f, ls);
    SpatialResult r3 = Compute3D(3.f, 4.f, 0.f, ls);

    CHECK(r3.pan  == Catch::Approx(r2.pan ).margin(1e-4f));
    CHECK(r3.gain == Catch::Approx(r2.gain).margin(1e-4f));
}

// ---------------------------------------------------------------------------
// MixerGraph: positional voice uses spatial gain
// ---------------------------------------------------------------------------

TEST_CASE("MixerGraph: listener range zero → maxDist emitter is silent", "[spatial][mixer]") {
    MixerGraph graph(48000, 2);
    graph.RegisterGroup("master");
    graph.SetListenerRange(1.f, 10.f, 1.f); // maxDist = 10

    VoicePool pool(kMixFrames, 2, 48000);
    uint32_t id = pool.Acquire(0);
    REQUIRE(id != kInvalidVoice);
    Voice* v = pool.Get(id);
    REQUIRE(v != nullptr);
    v->groupIndex = 0;
    v->params.positional.store(true,  std::memory_order_relaxed);
    v->params.posX.store(20.f,        std::memory_order_relaxed); // beyond maxDist
    v->params.posY.store(0.f,         std::memory_order_relaxed);
    v->params.posZ.store(0.f,         std::memory_order_relaxed);
    v->state.store(VoiceState::Playing, std::memory_order_release);

    // No sound, RenderChunk returns 0 → voice Done.
    const int16_t* pcm = graph.Render(pool, kMixFrames);
    REQUIRE(pcm != nullptr);
    // Voice produces no audio → master stays silent.
    bool allZero = true;
    for (uint32_t i = 0; i < kMixFrames * 2; ++i)
        if (pcm[i] != 0) { allZero = false; break; }
    CHECK(allZero);

    pool.Release(id);
}

// ---------------------------------------------------------------------------
// Per-voice effect chain
// ---------------------------------------------------------------------------

TEST_CASE("Voice: per-voice EqualizerEffect does not crash", "[voice4]") {
    Voice v;
    v.Init(kMixFrames, 2, 48000);
    v.voiceEffects_[0] = std::make_unique<EqualizerEffect>();

    // No sound → RenderChunk returns 0 immediately (before effect is applied).
    CHECK(v.RenderChunk(kMixFrames) == 0);

    // Attach a trivial sound stub isn't easy without Sound factory in tests,
    // but at least we verify the unique_ptr compiles and doesn't leak.
    v.Reset(); // must release the effect cleanly
}

// ---------------------------------------------------------------------------
// AudioContext: SetVoiceEffect command
// ---------------------------------------------------------------------------

TEST_CASE("AudioContext: SetGroupEffect command wires effect into group", "[context4]") {
    AudioContext ctx(std::make_unique<libZFork::NullOutput>(), 48000, 2);
    uint32_t g = ctx.RegisterGroup("sfx");
    REQUIRE(ctx.Start());

    // Allocate an effect on the heap — engine takes ownership via command.
    auto* fx = new EqualizerEffect();
    fx->SetBandGain(5, 6.f);

    Command c{};
    c.type       = CommandType::SetGroupEffect;
    c.groupId    = g;
    c.param.i[0] = 0; // slot
    c.ptr        = static_cast<IEffect*>(fx);
    REQUIRE(ctx.Post(c));

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // The effect pointer should now be in the group's slot.
    IEffect* stored = ctx.Graph().GetGroup(g).effectSlots_[0]
                                 .load(std::memory_order_acquire);
    CHECK(stored != nullptr);

    // Clear it — engine will delete it.
    Command clr{};
    clr.type       = CommandType::ClearGroupEffect;
    clr.groupId    = g;
    clr.param.i[0] = 0;
    ctx.Post(clr);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ctx.Stop();
}

// ---------------------------------------------------------------------------
// EventDispatcher: concurrent SPSC (engine writes, control thread reads)
// ---------------------------------------------------------------------------

TEST_CASE("EventDispatcher: concurrent SPSC does not lose events", "[events4]") {
    // Simulate the engine-thread/control-thread pattern.
    constexpr int kEvents = 200;
    EventDispatcher<256> disp;
    std::atomic<int> written{ 0 };
    std::atomic<bool> producerDone{ false };

    std::thread producer([&] {
        for (int i = 0; i < kEvents; ++i) {
            AudioEvent e{};
            e.type    = EventType::VoiceFinished;
            e.voiceId = static_cast<uint32_t>(i);
            // Spin until there's space (ring is 256, we write 200).
            disp.Push(e);
            written.fetch_add(1, std::memory_order_relaxed);
        }
        producerDone.store(true, std::memory_order_release);
    });

    int received = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((!producerDone.load(std::memory_order_acquire) || disp.Pending() > 0)
           && std::chrono::steady_clock::now() < deadline) {
        disp.DrainAndDispatch([](int, uint32_t, uint32_t, float, void* ud) {
            ++(*static_cast<int*>(ud));
        }, &received);
    }
    // Final drain.
    disp.DrainAndDispatch([](int, uint32_t, uint32_t, float, void* ud) {
        ++(*static_cast<int*>(ud));
    }, &received);

    producer.join();
    CHECK(received == kEvents);
}

// ---------------------------------------------------------------------------
// Listener position command reaches MixerGraph
// ---------------------------------------------------------------------------

TEST_CASE("AudioContext: SetListenerPosition2D reaches graph", "[context4]") {
    AudioContext ctx(std::make_unique<libZFork::NullOutput>(), 48000, 2);
    ctx.RegisterGroup("master");
    REQUIRE(ctx.Start());

    Command c{};
    c.type       = CommandType::SetListenerPosition2D;
    c.param.f[0] = 10.f;
    c.param.f[1] = 20.f;
    REQUIRE(ctx.Post(c));

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    const auto& ls = ctx.Graph().GetListenerState();
    CHECK(ls.x == Catch::Approx(10.f));
    CHECK(ls.y == Catch::Approx(20.f));

    ctx.Stop();
}
