#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

// Include the public C API header — this is what callers use.
#include <libzfork/libzfork2.h>

// For NullOutput in tests (no sound card required).
#include "output/null_output.h"
#include "engine/audio_context.h"
#include "engine/engine_defs.h"

#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

using namespace std::chrono_literals;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Build a ZF2_CONTEXT backed by NullOutput so tests don't need a sound card.
static ZF2_CONTEXT MakeTestContext()
{
    auto out = std::make_unique<libZFork::NullOutput>();
    auto* ac = new libZFork::engine::AudioContext(std::move(out), 48000, 2);
    (void)ac->RegisterGroup("master");
    (void)ac->RegisterGroup("music");
    (void)ac->RegisterGroup("sfx");
    (void)ac->RegisterGroup("ui");
    return static_cast<ZF2_CONTEXT>(ac);
}

// ══════════════════════════════════════════════════════════════════════════════
// 1. Context lifecycle
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Phase5: context create/start/stop/destroy", "[phase5][api2]")
{
    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(ctx != nullptr);

    REQUIRE(zfork2_Start(ctx) == 1);
    std::this_thread::sleep_for(20ms);
    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
}

TEST_CASE("Phase5: null-context guards don't crash", "[phase5][api2]")
{
    // All functions must tolerate a null context gracefully.
    zfork2_Stop(nullptr);
    zfork2_Update(nullptr);
    zfork2_DestroyContext(nullptr);
    CHECK(zfork2_Start(nullptr) == 0);
    CHECK(zfork2_GetGroup(nullptr, "sfx") == ZF2_INVALID_GROUP);
    CHECK(zfork2_RegisterGroup(nullptr, "x") == ZF2_INVALID_GROUP);
    CHECK(zfork2_IsVoiceActive(nullptr, 1) == 0);
    zfork2_SetVoiceVolume(nullptr, 1, 0.5f);
    zfork2_StopVoice(nullptr, 1);
}

// ══════════════════════════════════════════════════════════════════════════════
// 2. Group lookup
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Phase5: pre-registered groups resolve correctly", "[phase5][api2]")
{
    ZF2_CONTEXT ctx = MakeTestContext();

    CHECK(zfork2_GetGroup(ctx, "master") != ZF2_INVALID_GROUP);
    CHECK(zfork2_GetGroup(ctx, "music")  != ZF2_INVALID_GROUP);
    CHECK(zfork2_GetGroup(ctx, "sfx")    != ZF2_INVALID_GROUP);
    CHECK(zfork2_GetGroup(ctx, "ui")     != ZF2_INVALID_GROUP);
    CHECK(zfork2_GetGroup(ctx, "bogus")  == ZF2_INVALID_GROUP);

    zfork2_DestroyContext(ctx);
}

TEST_CASE("Phase5: RegisterGroup returns valid index", "[phase5][api2]")
{
    ZF2_CONTEXT ctx = MakeTestContext();

    ZF2_GROUP g = zfork2_RegisterGroup(ctx, "ambient");
    CHECK(g != ZF2_INVALID_GROUP);
    // Must round-trip via GetGroup.
    CHECK(zfork2_GetGroup(ctx, "ambient") == g);

    zfork2_DestroyContext(ctx);
}

// ══════════════════════════════════════════════════════════════════════════════
// 3. Sound load/unload
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Phase5: LoadSound returns null for non-existent file", "[phase5][api2]")
{
    ZF2_CONTEXT ctx = MakeTestContext();

    ZF2_SOUND snd = zfork2_LoadSound(ctx, L"does_not_exist_xyz.wav", ZF2_SOUND_PREDECODED);
    CHECK(snd == nullptr);

    zfork2_DestroyContext(ctx);
}

TEST_CASE("Phase5: UnloadSound on null is safe", "[phase5][api2]")
{
    zfork2_UnloadSound(nullptr);   // must not crash
}

// ══════════════════════════════════════════════════════════════════════════════
// 4. AllocVoice / PlaySound without a real file
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Phase5: PlaySound returns INVALID_VOICE for null sound", "[phase5][api2]")
{
    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    ZF2_GROUP sfx   = zfork2_GetGroup(ctx, "sfx");
    ZF2_VOICE voice = zfork2_PlaySound(ctx, nullptr, sfx);
    CHECK(voice == ZF2_INVALID_VOICE);

    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
}

TEST_CASE("Phase5: voice commands on invalid voice don't crash", "[phase5][api2]")
{
    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    const ZF2_VOICE bad = ZF2_INVALID_VOICE;
    zfork2_StopVoice(ctx, bad);
    zfork2_PauseVoice(ctx, bad);
    zfork2_ResumeVoice(ctx, bad);
    zfork2_SetVoiceVolume(ctx, bad, 0.5f);
    zfork2_SetVoicePan(ctx, bad, -0.5f);
    zfork2_SetVoicePitch(ctx, bad, 2.0f);
    zfork2_SetVoiceLoop(ctx, bad, -1);
    zfork2_SetVoicePriority(ctx, bad, 10);
    zfork2_SetVoicePosition2D(ctx, bad, 1.f, 2.f);
    zfork2_SetVoicePosition3D(ctx, bad, 1.f, 2.f, 3.f);
    zfork2_SetVoiceMarker(ctx, bad, 42, 1.5f);
    zfork2_ClearVoiceMarkers(ctx, bad);
    CHECK(zfork2_IsVoiceActive(ctx, bad) == 0);

    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
}

// ══════════════════════════════════════════════════════════════════════════════
// 5. Group volume / mute
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Phase5: group commands on invalid group don't crash", "[phase5][api2]")
{
    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    zfork2_SetGroupVolume(ctx, ZF2_INVALID_GROUP, 0.5f);
    zfork2_SetGroupMute(ctx, ZF2_INVALID_GROUP, 1);

    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
}

TEST_CASE("Phase5: SetGroupVolume / SetGroupMute post without crash", "[phase5][api2]")
{
    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    ZF2_GROUP music = zfork2_GetGroup(ctx, "music");
    REQUIRE(music != ZF2_INVALID_GROUP);

    zfork2_SetGroupVolume(ctx, music, 0.5f);
    zfork2_SetGroupMute(ctx, music, 1);
    zfork2_SetGroupMute(ctx, music, 0);

    std::this_thread::sleep_for(30ms); // let engine process

    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
}

// ══════════════════════════════════════════════════════════════════════════════
// 6. Listener
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Phase5: listener commands post without crash", "[phase5][api2]")
{
    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    zfork2_SetListenerPosition2D(ctx, 10.f, 20.f);
    zfork2_SetListenerPosition3D(ctx, 10.f, 20.f, 5.f);
    zfork2_SetListenerDirection(ctx, 0.f, 1.f, 0.f);
    zfork2_SetListenerRange(ctx, 1.f, 50.f, 1.5f);

    std::this_thread::sleep_for(30ms);

    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
}

// ══════════════════════════════════════════════════════════════════════════════
// 7. Callback / Update
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Phase5: SetCallback / Update round-trip", "[phase5][api2]")
{
    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    std::atomic<int> callCount{0};

    auto cb = [](int /*type*/, ZF2_VOICE /*voice*/, uint32_t /*p1*/,
                  float /*p2*/, void* ud)
    {
        auto* cnt = static_cast<std::atomic<int>*>(ud);
        cnt->fetch_add(1, std::memory_order_relaxed);
    };

    zfork2_SetCallback(ctx, cb, &callCount);

    // Update on empty queue — must not crash even with no events.
    for (int i = 0; i < 5; ++i) {
        zfork2_Update(ctx);
        std::this_thread::sleep_for(5ms);
    }

    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
}

// ══════════════════════════════════════════════════════════════════════════════
// 8. Concurrent post stress
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Phase5: concurrent voice commands from multiple threads", "[phase5][api2][stress]")
{
    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    constexpr int kThreads = 8;
    constexpr int kOps     = 100;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([ctx, t]() {
            for (int i = 0; i < kOps; ++i) {
                // Use a fake-but-non-zero voice id; engine will silently discard
                // if not valid — what we're testing is that no UB or crash occurs.
                ZF2_VOICE fakeVoice = static_cast<ZF2_VOICE>((t + 1) * 1000 + i);
                zfork2_SetVoiceVolume(ctx, fakeVoice, 0.5f);
                zfork2_SetVoicePan(ctx, fakeVoice, (i % 3 == 0) ? -0.5f : 0.5f);
                zfork2_SetGroupVolume(ctx, 0u, 0.8f);
                zfork2_SetListenerPosition2D(ctx,
                    static_cast<float>(i), static_cast<float>(t));
            }
        });
    }

    for (auto& th : threads) th.join();

    std::this_thread::sleep_for(30ms); // let engine drain
    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
}

// ══════════════════════════════════════════════════════════════════════════════
// 9. zfork2_Update without callback set
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Phase5: Update with no callback registered is safe", "[phase5][api2]")
{
    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    // No SetCallback called — Update must not crash.
    for (int i = 0; i < 3; ++i) {
        zfork2_Update(ctx);
        std::this_thread::sleep_for(5ms);
    }

    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
}
