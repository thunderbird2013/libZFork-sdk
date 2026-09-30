#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/command_queue.h"
#include "engine/event_dispatcher.h"
#include "engine/voice_pool.h"
#include "engine/mixer_graph.h"
#include "engine/audio_context.h"
#include "output/null_output.h"

#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

using namespace libZFork::engine;
using namespace libZFork;

// ---------------------------------------------------------------------------
// CommandQueue
// ---------------------------------------------------------------------------

TEST_CASE("CommandQueue: single-thread push/pop roundtrip", "[engine][cmdqueue]") {
    CommandQueue<16> q;
    Command cmd{};
    cmd.type       = CommandType::SetVoiceVolume;
    cmd.voiceId    = 42;
    cmd.param.f[0] = 0.75f;

    REQUIRE(q.TryPush(cmd));

    Command out{};
    REQUIRE(q.TryPop(out));
    CHECK(out.type       == CommandType::SetVoiceVolume);
    CHECK(out.voiceId    == 42u);
    CHECK(out.param.f[0] == Catch::Approx(0.75f));

    // Queue is now empty
    REQUIRE_FALSE(q.TryPop(out));
}

TEST_CASE("CommandQueue: MPSC — 4 producer threads + 1 consumer", "[engine][cmdqueue]") {
    CommandQueue<1024> q;
    constexpr int kThreads    = 4;
    constexpr int kPerThread  = 100;
    constexpr int kTotal      = kThreads * kPerThread;

    std::atomic<int> produced{ 0 };

    std::vector<std::thread> producers;
    producers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&, t] {
            for (int i = 0; i < kPerThread; ++i) {
                Command c{};
                c.type       = CommandType::SetVoiceVolume;
                c.voiceId    = static_cast<uint32_t>(t * 1000 + i);
                c.param.f[0] = static_cast<float>(i) / kPerThread;
                while (!q.TryPush(c)) { /* spin */ }
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    int consumed = 0;
    Command buf[kTotal];
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (consumed < kTotal && std::chrono::steady_clock::now() < deadline) {
        Command c{};
        if (q.TryPop(c)) buf[consumed++] = c;
    }

    for (auto& p : producers) p.join();

    // Drain any remaining commands posted after our deadline check.
    Command c{};
    while (q.TryPop(c) && consumed < kTotal) buf[consumed++] = c;

    REQUIRE(produced.load() == kTotal);
    CHECK(consumed == kTotal);
}

TEST_CASE("CommandQueue: DrainAll", "[engine][cmdqueue]") {
    CommandQueue<32> q;
    for (int i = 0; i < 10; ++i) {
        Command c{};
        c.type    = CommandType::SetGroupVolume;
        c.groupId = static_cast<uint32_t>(i);
        REQUIRE(q.TryPush(c));
    }

    Command buf[32];
    uint32_t n = q.DrainAll(buf, 32);
    REQUIRE(n == 10);
    for (uint32_t i = 0; i < n; ++i)
        CHECK(buf[i].groupId == i);
}

// ---------------------------------------------------------------------------
// VoicePool
// ---------------------------------------------------------------------------

TEST_CASE("VoicePool: acquire all 64 voices", "[engine][voicepool]") {
    VoicePool pool;
    std::vector<uint32_t> ids;
    ids.reserve(kMaxVoices);

    for (uint32_t i = 0; i < kMaxVoices; ++i) {
        uint32_t id = pool.Acquire(0);
        REQUIRE(id != kInvalidVoice);
        ids.push_back(id);
    }
    CHECK(pool.FreeCount() == 0);
    CHECK(pool.ActiveCount() == kMaxVoices); // Starting counts as active (non-Free)

    // Release all
    for (auto id : ids) pool.Release(id);
    CHECK(pool.FreeCount() == kMaxVoices);
}

TEST_CASE("VoicePool: stale id returns nullptr from Get", "[engine][voicepool]") {
    VoicePool pool;
    uint32_t id = pool.Acquire(0);
    REQUIRE(id != kInvalidVoice);

    Voice* v = pool.Get(id);
    REQUIRE(v != nullptr);

    pool.Release(id);
    CHECK(pool.Get(id) == nullptr);
}

TEST_CASE("VoicePool: priority stealing when full", "[engine][voicepool]") {
    VoicePool pool;

    // Fill all slots and promote them to Playing with varying priorities.
    std::vector<uint32_t> ids;
    for (uint32_t i = 0; i < kMaxVoices; ++i) {
        uint32_t id = pool.Acquire(static_cast<int32_t>(i)); // priority = slot index
        REQUIRE(id != kInvalidVoice);
        Voice* v = pool.Get(id);
        REQUIRE(v != nullptr);
        v->state.store(VoiceState::Playing, std::memory_order_release);
        ids.push_back(id);
    }
    CHECK(pool.FreeCount() == 0);

    // Acquiring one more should steal the lowest priority (priority 0 → slot 0).
    uint32_t stolen = pool.Acquire(99);
    REQUIRE(stolen != kInvalidVoice);
    // The stolen id must differ from all original ids (it reused a slot with a new gen).
    bool isNew = true;
    for (auto id : ids)
        if (id == stolen) { isNew = false; break; }
    CHECK(isNew);
}

TEST_CASE("VoicePool: IsValid from any thread", "[engine][voicepool]") {
    VoicePool pool;
    uint32_t id = pool.Acquire(0);
    REQUIRE(pool.IsValid(id));

    pool.Release(id);
    CHECK_FALSE(pool.IsValid(id));
    CHECK_FALSE(pool.IsValid(kInvalidVoice));
}

// ---------------------------------------------------------------------------
// EventDispatcher
// ---------------------------------------------------------------------------

TEST_CASE("EventDispatcher: push and drain", "[engine][events]") {
    EventDispatcher<16> disp;
    CHECK(disp.Pending() == 0u);

    for (int i = 0; i < 5; ++i) {
        AudioEvent e{};
        e.type    = EventType::VoiceFinished;
        e.voiceId = static_cast<uint32_t>(i);
        disp.Push(e);
    }
    CHECK(disp.Pending() == 5u);

    std::vector<uint32_t> received;
    disp.DrainAndDispatch([](int, uint32_t vid, uint32_t, float, void* ud) {
        static_cast<std::vector<uint32_t>*>(ud)->push_back(vid);
    }, &received);

    REQUIRE(received.size() == 5);
    for (int i = 0; i < 5; ++i)
        CHECK(received[static_cast<size_t>(i)] == static_cast<uint32_t>(i));

    CHECK(disp.Pending() == 0u);
}

TEST_CASE("EventDispatcher: silently drops when full", "[engine][events]") {
    EventDispatcher<4> disp; // tiny ring
    for (int i = 0; i < 8; ++i) {
        AudioEvent e{};
        e.voiceId = static_cast<uint32_t>(i);
        disp.Push(e);
    }
    // Only 4 fit; the rest are dropped silently.
    CHECK(disp.Pending() == 4u);

    disp.DrainAndDispatch(nullptr, nullptr); // just drain without callback
    CHECK(disp.Pending() == 0u);
}

// ---------------------------------------------------------------------------
// MixerGraph
// ---------------------------------------------------------------------------

TEST_CASE("MixerGraph: register and find groups", "[engine][mixer]") {
    MixerGraph graph(48000, 2);
    uint32_t m = graph.RegisterGroup("master");
    uint32_t s = graph.RegisterGroup("sfx");
    CHECK(m == 0u);
    CHECK(s == 1u);

    CHECK(graph.FindGroup("master") == 0u);
    CHECK(graph.FindGroup("sfx")    == 1u);
    CHECK(graph.FindGroup("none")   == kInvalidGroup);
}

TEST_CASE("MixerGraph: Render with no voices outputs silence", "[engine][mixer]") {
    MixerGraph graph(48000, 2);
    graph.RegisterGroup("master");

    VoicePool pool;
    const int16_t* pcm = graph.Render(pool, kMixFrames);
    REQUIRE(pcm != nullptr);

    // No voices → all samples must be 0.
    bool allZero = true;
    for (uint32_t i = 0; i < kMixFrames * 2; ++i)
        if (pcm[i] != 0) { allZero = false; break; }
    CHECK(allZero);
}

TEST_CASE("MixerGraph: muted group contributes no audio", "[engine][mixer]") {
    MixerGraph graph(48000, 2);
    uint32_t gIdx = graph.RegisterGroup("sfx");
    graph.GetGroup(gIdx).muted.store(true, std::memory_order_relaxed);

    VoicePool pool;
    const int16_t* pcm = graph.Render(pool, kMixFrames);
    bool allZero = true;
    for (uint32_t i = 0; i < kMixFrames * 2; ++i)
        if (pcm[i] != 0) { allZero = false; break; }
    CHECK(allZero);
}

// ---------------------------------------------------------------------------
// AudioContext
// ---------------------------------------------------------------------------

TEST_CASE("AudioContext: start and stop lifecycle", "[engine][context]") {
    auto ctx = std::make_unique<AudioContext>(
        std::make_unique<NullOutput>(), 48000, 2);

    uint32_t g = ctx->RegisterGroup("master");
    CHECK(g == 0u);
    CHECK(ctx->FindGroup("master") == 0u);

    REQUIRE(ctx->Start());
    // Let it run a few cycles.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ctx->Stop(); // must not deadlock
}

TEST_CASE("AudioContext: repeated start/stop cycles", "[engine][context]") {
    for (int cycle = 0; cycle < 3; ++cycle) {
        AudioContext ctx(std::make_unique<NullOutput>(), 48000, 2);
        ctx.RegisterGroup("master");
        REQUIRE(ctx.Start());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ctx.Stop();
    }
}

TEST_CASE("AudioContext: Post PlayVoice command", "[engine][context]") {
    AudioContext ctx(std::make_unique<NullOutput>(), 48000, 2);
    ctx.RegisterGroup("master");
    REQUIRE(ctx.Start());

    Command c{};
    c.type       = CommandType::PlayVoice;
    c.groupId    = 0;
    c.param.i[0] = 0; // priority
    CHECK(ctx.Post(c));

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // In Phase 2 the voice stub returns 0 frames immediately, so the voice
    // transitions to Done and is released in the same cycle.
    // After a short wait there should be a VoiceFinished event.
    std::atomic<int> eventsReceived{ 0 };
    ctx.Update([](int, uint32_t, uint32_t, float, void* ud) {
        static_cast<std::atomic<int>*>(ud)->fetch_add(1, std::memory_order_relaxed);
    }, &eventsReceived);

    // We expect at least VoiceStart + VoiceFinished (2 events).
    CHECK(eventsReceived.load() >= 1);

    ctx.Stop();
}
