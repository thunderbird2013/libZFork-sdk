#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "output/null_output.h"

using namespace libZFork;

TEST_CASE("NullOutput: Init and Start succeed", "[output][null]") {
    NullOutput out;
    REQUIRE(out.Init(44100, 2, 16, 500));
    REQUIRE(out.Start());
}

TEST_CASE("NullOutput: WriteBlock returns true when running", "[output][null]") {
    NullOutput out;
    out.Init(44100, 2, 16, 500);
    out.Start();

    std::vector<int16_t> buf(512 * 2, 0); // 512 stereo frames
    REQUIRE(out.WriteBlock(buf.data(), 512));
}

TEST_CASE("NullOutput: WriteBlock returns false after Stop", "[output][null]") {
    NullOutput out;
    out.Init(44100, 2, 16, 500);
    out.Start();
    out.Stop();

    std::vector<int16_t> buf(512 * 2, 0);
    REQUIRE_FALSE(out.WriteBlock(buf.data(), 512));
}

TEST_CASE("NullOutput: Drain is a no-op (does not hang)", "[output][null]") {
    NullOutput out;
    out.Init(44100, 2, 16, 500);
    out.Start();
    // Drain on NullOutput must return immediately
    out.Drain();
    SUCCEED("Drain returned without blocking");
}

TEST_CASE("NullOutput: SetMasterVolume does not crash", "[output][null]") {
    NullOutput out;
    out.Init(48000, 2, 16, 100);
    out.SetMasterVolume(0, 0);
    out.SetMasterVolume(50, 75);
    out.SetMasterVolume(100, 100);
    SUCCEED();
}

TEST_CASE("NullOutput: EnumerateDevices returns at least 1", "[output][null]") {
    NullOutput out;
    REQUIRE(out.EnumerateDevices() >= 1);
}

TEST_CASE("NullOutput: GetDeviceInfo populates name", "[output][null]") {
    NullOutput out;
    WaveDeviceInfo info{};
    REQUIRE(out.GetDeviceInfo(0, info));
    REQUIRE(info.name[0] != '\0');
}

TEST_CASE("NullOutput: SetDevice always succeeds", "[output][null]") {
    NullOutput out;
    REQUIRE(out.SetDevice(0));
    REQUIRE(out.SetDevice(42));
}

TEST_CASE("NullOutput: multiple Start/Stop cycles", "[output][null]") {
    NullOutput out;
    out.Init(48000, 2, 16, 20);

    for (int i = 0; i < 5; ++i) {
        out.Start();
        std::vector<int16_t> buf(256 * 2, static_cast<int16_t>(i * 100));
        REQUIRE(out.WriteBlock(buf.data(), 256));
        out.Stop();
        // WriteBlock must fail after Stop
        REQUIRE_FALSE(out.WriteBlock(buf.data(), 256));
        // Re-initialize to reset stopped flag
        out.Init(48000, 2, 16, 20);
    }
}
