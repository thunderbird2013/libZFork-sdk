#include <catch2/catch_test_macros.hpp>
#include "core/player.h"
#include "output/null_output.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <filesystem>
#include <thread>
#include <chrono>
#include <vector>

using namespace libZFork;

// Helper: inject NullOutput into Player for headless testing.
static std::unique_ptr<Player> MakeTestPlayer() {
    return std::make_unique<Player>(std::make_unique<NullOutput>());
}

static std::vector<uint8_t> MakeTinyWav16Mono(uint32_t sampleRate = 22050, uint32_t durationMs = 120) {
    const uint32_t frames = (sampleRate * durationMs) / 1000u;
    const uint16_t channels = 1;
    const uint16_t bits = 16;
    const uint32_t byteRate = sampleRate * channels * (bits / 8u);
    const uint16_t blockAlign = channels * (bits / 8u);
    const uint32_t dataSize = frames * blockAlign;
    const uint32_t riffSize = 36u + dataSize;

    std::vector<uint8_t> out(44u + dataSize, 0);
    auto wr16 = [&out](size_t off, uint16_t v) {
        out[off + 0] = static_cast<uint8_t>(v & 0xFFu);
        out[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    };
    auto wr32 = [&out](size_t off, uint32_t v) {
        out[off + 0] = static_cast<uint8_t>(v & 0xFFu);
        out[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
        out[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
        out[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    };

    out[0] = 'R'; out[1] = 'I'; out[2] = 'F'; out[3] = 'F';
    wr32(4, riffSize);
    out[8] = 'W'; out[9] = 'A'; out[10] = 'V'; out[11] = 'E';
    out[12] = 'f'; out[13] = 'm'; out[14] = 't'; out[15] = ' ';
    wr32(16, 16u);
    wr16(20, 1u); // PCM
    wr16(22, channels);
    wr32(24, sampleRate);
    wr32(28, byteRate);
    wr16(32, blockAlign);
    wr16(34, bits);
    out[36] = 'd'; out[37] = 'a'; out[38] = 't'; out[39] = 'a';
    wr32(40, dataSize);

    for (uint32_t i = 0; i < frames; ++i) {
        const int16_t s = static_cast<int16_t>((i % 64u) < 32u ? 12000 : -12000);
        const size_t off = 44u + static_cast<size_t>(i) * 2u;
        out[off + 0] = static_cast<uint8_t>(s & 0xFF);
        out[off + 1] = static_cast<uint8_t>((s >> 8) & 0xFF);
    }
    return out;
}

struct PcmCbCounter {
    std::atomic<uint32_t> calls{0};
    std::atomic<uint64_t> frames{0};
};

static void ZFORK_CALL OnPcmTap(void* /*instance*/,
                                void* userData,
                                const float* /*interleaved*/,
                                uint32_t frames,
                                int /*channels*/,
                                int /*sampleRate*/) {
    auto* c = static_cast<PcmCbCounter*>(userData);
    if (!c) return;
    c->calls.fetch_add(1, std::memory_order_relaxed);
    c->frames.fetch_add(frames, std::memory_order_relaxed);
}

TEST_CASE("Player: construction with NullOutput does not throw", "[player]") {
    REQUIRE_NOTHROW(MakeTestPlayer());
}

TEST_CASE("Player: Play without open stream returns false", "[player]") {
    auto p = MakeTestPlayer();
    REQUIRE_FALSE(p->Play());
    REQUIRE(std::string(p->GetError()).find("No stream") != std::string::npos);
}

TEST_CASE("Player: Stop on stopped player is idempotent", "[player]") {
    auto p = MakeTestPlayer();
    REQUIRE(p->Stop()); // already stopped — should return true
    REQUIRE(p->Stop());
}

TEST_CASE("Player: GetStatus returns Stopped initially", "[player]") {
    auto p = MakeTestPlayer();
    auto s = p->GetStatus();
    REQUIRE_FALSE(s.playing);
    REQUIRE_FALSE(s.paused);
}

TEST_CASE("Player: SetMasterVolume clamps to [0,100]", "[player]") {
    auto p = MakeTestPlayer();
    REQUIRE(p->SetMasterVolume(-10, 200));
    int l{}, r{};
    p->GetMasterVolume(l, r);
    REQUIRE(l == 0);
    REQUIRE(r == 100);
}

TEST_CASE("Player: SetPlayerVolume round-trips", "[player]") {
    auto p = MakeTestPlayer();
    REQUIRE(p->SetPlayerVolume(42, 77));
    int l{}, r{};
    REQUIRE(p->GetPlayerVolume(l, r));
    REQUIRE(l == 42);
    REQUIRE(r == 77);
}

TEST_CASE("Player: EnumerateWaveOutDevices returns >= 0 with NullOutput", "[player]") {
    auto p = MakeTestPlayer();
    REQUIRE(p->EnumerateWaveOutDevices() >= 0);
}

TEST_CASE("Player: SetPCMOutputCallback accepts nullptr", "[player][pcmcb]") {
    auto p = MakeTestPlayer();
    REQUIRE(p->SetPCMOutputCallback(nullptr, nullptr, false));
    REQUIRE(p->SetPCMOutputCallback(nullptr, nullptr, true));
}

TEST_CASE("Player: PCM callback receives data while playing", "[player][pcmcb]") {
    auto p = MakeTestPlayer();
    auto wav = MakeTinyWav16Mono();
    PcmCbCounter counter{};

    REQUIRE(p->SetPCMOutputCallback(&OnPcmTap, &counter, false));
    REQUIRE(p->OpenStream(wav.data(), static_cast<uint32_t>(wav.size()), StreamFormat::Wav));
    REQUIRE(p->Play());

    for (int i = 0; i < 2000 && p->GetStatus().playing; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE_FALSE(p->GetStatus().playing);
    REQUIRE(counter.calls.load(std::memory_order_relaxed) > 0u);
    REQUIRE(counter.frames.load(std::memory_order_relaxed) > 0u);
}

TEST_CASE("Player: PCM callback works with built-in output disabled", "[player][pcmcb]") {
    auto p = MakeTestPlayer();
    auto wav = MakeTinyWav16Mono();
    PcmCbCounter counter{};

    REQUIRE(p->SetPCMOutputCallback(&OnPcmTap, &counter, true));
    REQUIRE(p->OpenStream(wav.data(), static_cast<uint32_t>(wav.size()), StreamFormat::Wav));
    REQUIRE(p->Play());

    for (int i = 0; i < 2000 && p->GetStatus().playing; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE_FALSE(p->GetStatus().playing);
    REQUIRE(counter.calls.load(std::memory_order_relaxed) > 0u);
    REQUIRE(counter.frames.load(std::memory_order_relaxed) > 0u);
}

// This test only runs when a short WAV file is present at the given path.
// Tag [.] means it is skipped by default; run explicitly with --tags [wavplay].
TEST_CASE("Player: Play WAV to NullOutput and natural stop", "[.][wavplay]") {
    namespace fs = std::filesystem;
    const wchar_t* wav = L"test_tone.wav";
    if (!fs::exists(wav)) SKIP("test_tone.wav not found");

    auto p = MakeTestPlayer();
    REQUIRE(p->OpenFile(wav, StreamFormat::Wav));
    REQUIRE(p->Play());

    // Wait for playback to finish (NullOutput is immediate, so this loops fast)
    for (int i = 0; i < 2000; ++i) {
        if (p->GetStatus().playing == false) break;
        // NullOutput's WriteBlock never blocks, so the decode thread runs at
        // CPU speed — give it a moment via polling.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE_FALSE(p->GetStatus().playing);
}
