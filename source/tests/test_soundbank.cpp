/**
 * libZFork2 — SoundBank V1 Unit Tests
 *
 * Tests the zfork2_LoadBankV1 / zfork2_PlayEventV1 API without a real
 * sound card (NullOutput) and without real audio files (we create tiny
 * in-memory WAV files on disk via a temp-file helper).
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <libzfork/libzfork2.h>

#include "output/null_output.h"
#include "engine/audio_context.h"
#include "engine/engine_defs.h"
#include "engine/soundbank_v1.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <cstdint>

using namespace std::chrono_literals;
namespace fs = std::filesystem;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Build a ZF2_CONTEXT backed by NullOutput so tests don't need a sound card.
static ZF2_CONTEXT MakeTestContext() {
    auto out = std::make_unique<libZFork::NullOutput>();
    auto* ac = new libZFork::engine::AudioContext(std::move(out), 44100, 2);
    (void)ac->RegisterGroup("master");
    (void)ac->RegisterGroup("music");
    (void)ac->RegisterGroup("sfx");
    (void)ac->RegisterGroup("ui");
    return static_cast<ZF2_CONTEXT>(ac);
}

// Write a minimal valid PCM WAV file (16-bit stereo, 44100 Hz).
// frameCount determines how many stereo frames (samples per channel) it has.
static bool WriteMinimalWav(const fs::path& path, uint32_t frameCount = 512) {
    // RIFF/WAVE header sizes
    const uint32_t channels   = 2;
    const uint32_t sampleRate = 44100;
    const uint16_t bitsPerSample = 16;
    const uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
    const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8));
    const uint32_t dataBytes = frameCount * channels * (bitsPerSample / 8);

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    auto write16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    auto write32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto writeTag = [&](const char* tag) { f.write(tag, 4); };

    // RIFF header
    writeTag("RIFF");
    write32(36 + dataBytes);   // chunk size
    writeTag("WAVE");

    // fmt sub-chunk
    writeTag("fmt ");
    write32(16);               // sub-chunk size (PCM)
    write16(1);                // PCM format
    write16(static_cast<uint16_t>(channels));
    write32(sampleRate);
    write32(byteRate);
    write16(blockAlign);
    write16(bitsPerSample);

    // data sub-chunk
    writeTag("data");
    write32(dataBytes);

    // Write silence (all zeros)
    std::vector<char> silence(dataBytes, '\0');
    f.write(silence.data(), static_cast<std::streamsize>(silence.size()));

    return f.good();
}

// Write a bank text file that references one or more relative WAV paths.
// Returns the bank file path.
static fs::path WriteBankFile(const fs::path& dir,
                               const std::vector<std::pair<std::string,std::string>>& events,
                               const std::string& filename = "test.bank")
{
    fs::path bankPath = dir / filename;
    std::ofstream f(bankPath);
    f << "# test bank\n";
    for (const auto& [id, wavRel] : events) {
        // eventId;path;group;type;volume;pan;priority;loop
        f << id << ";" << wavRel << ";sfx;predecoded;1.0;0.0;5;0\n";
    }
    return bankPath;
}

// ══════════════════════════════════════════════════════════════════════════════
// 1. Null-guard tests
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("SoundBank: null guards don't crash", "[soundbank][null]") {
    CHECK(zfork2_GetBankEntryCount(nullptr)   == 0u);
    CHECK(zfork2_GetBankSkippedCount(nullptr) == 0u);
    CHECK(zfork2_PlayEventV1(nullptr, nullptr, "x") == ZF2_INVALID_VOICE);
    zfork2_UnloadBankV1(nullptr);  // must not crash
}

TEST_CASE("SoundBank: LoadBankV1 with null/bad path returns null", "[soundbank]") {
    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    CHECK(zfork2_LoadBankV1(ctx, nullptr) == nullptr);
    CHECK(zfork2_LoadBankV1(ctx, L"does_not_exist_xyz_123.bank") == nullptr);
    CHECK(zfork2_LoadBankV1(nullptr, L"irrelevant.bank") == nullptr);

    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
}

// ══════════════════════════════════════════════════════════════════════════════
// 2. Diagnostic API — EntryCount / SkippedCount
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("SoundBank: bank with no valid sounds has EntryCount 0 and SkippedCount > 0", "[soundbank][diag]") {
    // Create a temp dir with a bank file referencing non-existent WAVs.
    fs::path tmpDir = fs::temp_directory_path() / "libzfork_test_bank_missing";
    fs::create_directories(tmpDir);

    fs::path bankPath = WriteBankFile(tmpDir, {
        { "snd_missing_a", "nonexistent_a.wav" },
        { "snd_missing_b", "nonexistent_b.wav" },
    });

    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    ZF2_BANK bank = zfork2_LoadBankV1(ctx, bankPath.wstring().c_str());
    REQUIRE(bank != nullptr);                          // file opened OK
    CHECK(zfork2_GetBankEntryCount(bank)   == 0u);    // nothing loaded
    CHECK(zfork2_GetBankSkippedCount(bank) == 2u);    // both skipped

    zfork2_UnloadBankV1(bank);
    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);

    fs::remove_all(tmpDir);
}

TEST_CASE("SoundBank: bank with valid WAV files loads correctly", "[soundbank][diag]") {
    fs::path tmpDir = fs::temp_directory_path() / "libzfork_test_bank_valid";
    fs::create_directories(tmpDir);

    // Write two tiny WAV files next to the bank
    REQUIRE(WriteMinimalWav(tmpDir / "a.wav"));
    REQUIRE(WriteMinimalWav(tmpDir / "b.wav"));

    fs::path bankPath = WriteBankFile(tmpDir, {
        { "sound_a", "a.wav" },
        { "sound_b", "b.wav" },
    });

    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    ZF2_BANK bank = zfork2_LoadBankV1(ctx, bankPath.wstring().c_str());
    REQUIRE(bank != nullptr);
    CHECK(zfork2_GetBankEntryCount(bank)   == 2u);
    CHECK(zfork2_GetBankSkippedCount(bank) == 0u);

    zfork2_UnloadBankV1(bank);
    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);

    fs::remove_all(tmpDir);
}

TEST_CASE("SoundBank: mixed valid + missing entries", "[soundbank][diag]") {
    fs::path tmpDir = fs::temp_directory_path() / "libzfork_test_bank_mixed";
    fs::create_directories(tmpDir);

    REQUIRE(WriteMinimalWav(tmpDir / "ok.wav"));

    fs::path bankPath = WriteBankFile(tmpDir, {
        { "good_sound",   "ok.wav"            },
        { "bad_sound_a",  "missing_a.wav"     },
        { "bad_sound_b",  "missing_b.wav"     },
    });

    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    ZF2_BANK bank = zfork2_LoadBankV1(ctx, bankPath.wstring().c_str());
    REQUIRE(bank != nullptr);
    CHECK(zfork2_GetBankEntryCount(bank)   == 1u);
    CHECK(zfork2_GetBankSkippedCount(bank) == 2u);

    zfork2_UnloadBankV1(bank);
    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);

    fs::remove_all(tmpDir);
}

// ══════════════════════════════════════════════════════════════════════════════
// 3. PlayEventV1 — find / not-find events
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("SoundBank: PlayEventV1 unknown event returns INVALID_VOICE", "[soundbank]") {
    fs::path tmpDir = fs::temp_directory_path() / "libzfork_test_bank_play1";
    fs::create_directories(tmpDir);
    REQUIRE(WriteMinimalWav(tmpDir / "snd.wav"));
    fs::path bankPath = WriteBankFile(tmpDir, { { "click", "snd.wav" } });

    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    ZF2_BANK bank = zfork2_LoadBankV1(ctx, bankPath.wstring().c_str());
    REQUIRE(bank != nullptr);
    REQUIRE(zfork2_GetBankEntryCount(bank) == 1u);

    CHECK(zfork2_PlayEventV1(ctx, bank, "does_not_exist") == ZF2_INVALID_VOICE);

    zfork2_UnloadBankV1(bank);
    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
    fs::remove_all(tmpDir);
}

TEST_CASE("SoundBank: PlayEventV1 known event returns valid voice", "[soundbank]") {
    fs::path tmpDir = fs::temp_directory_path() / "libzfork_test_bank_play2";
    fs::create_directories(tmpDir);
    REQUIRE(WriteMinimalWav(tmpDir / "snd.wav", 4096));  // > one mix block
    fs::path bankPath = WriteBankFile(tmpDir, { { "boom", "snd.wav" } });

    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    ZF2_BANK bank = zfork2_LoadBankV1(ctx, bankPath.wstring().c_str());
    REQUIRE(bank != nullptr);

    ZF2_VOICE v = zfork2_PlayEventV1(ctx, bank, "boom");
    CHECK(v != ZF2_INVALID_VOICE);
    CHECK(zfork2_IsVoiceActive(ctx, v) == 1);

    std::this_thread::sleep_for(50ms);  // let engine consume a few blocks
    zfork2_Update(ctx);                 // dispatch events

    zfork2_UnloadBankV1(bank);
    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
    fs::remove_all(tmpDir);
}

TEST_CASE("SoundBank: PlayEventV1 with null bank returns INVALID_VOICE", "[soundbank]") {
    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    CHECK(zfork2_PlayEventV1(ctx, nullptr, "any") == ZF2_INVALID_VOICE);

    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
}

// ══════════════════════════════════════════════════════════════════════════════
// 4. PlayEventOnGroupV1
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("SoundBank: PlayEventOnGroupV1 overrides group", "[soundbank]") {
    fs::path tmpDir = fs::temp_directory_path() / "libzfork_test_bank_group";
    fs::create_directories(tmpDir);
    REQUIRE(WriteMinimalWav(tmpDir / "snd.wav", 4096));
    fs::path bankPath = WriteBankFile(tmpDir, { { "tone", "snd.wav" } });

    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    ZF2_BANK  bank  = zfork2_LoadBankV1(ctx, bankPath.wstring().c_str());
    REQUIRE(bank != nullptr);

    ZF2_GROUP music = zfork2_GetGroup(ctx, "music");
    REQUIRE(music != ZF2_INVALID_GROUP);

    ZF2_VOICE v = zfork2_PlayEventOnGroupV1(ctx, bank, "tone", music);
    CHECK(v != ZF2_INVALID_VOICE);

    std::this_thread::sleep_for(20ms);

    zfork2_UnloadBankV1(bank);
    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
    fs::remove_all(tmpDir);
}

// ══════════════════════════════════════════════════════════════════════════════
// 5. Bank format robustness
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("SoundBank: comment lines and blank lines are ignored", "[soundbank][format]") {
    fs::path tmpDir = fs::temp_directory_path() / "libzfork_test_bank_fmt";
    fs::create_directories(tmpDir);
    REQUIRE(WriteMinimalWav(tmpDir / "snd.wav"));

    // Bank with comments, blank lines, and one valid entry
    fs::path bankPath = tmpDir / "comments.bank";
    {
        std::ofstream f(bankPath);
        f << "# This is a comment\n";
        f << "\n";
        f << "   \n";           // whitespace-only line
        f << "# Another comment\n";
        f << "click;snd.wav;sfx;predecoded;1.0;0.0;5;0\n";
    }

    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    ZF2_BANK bank = zfork2_LoadBankV1(ctx, bankPath.wstring().c_str());
    REQUIRE(bank != nullptr);
    CHECK(zfork2_GetBankEntryCount(bank)   == 1u);
    CHECK(zfork2_GetBankSkippedCount(bank) == 0u);

    zfork2_UnloadBankV1(bank);
    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
    fs::remove_all(tmpDir);
}

TEST_CASE("SoundBank: entries with too few fields are skipped silently", "[soundbank][format]") {
    fs::path tmpDir = fs::temp_directory_path() / "libzfork_test_bank_malformed";
    fs::create_directories(tmpDir);

    // All lines have fewer than 4 semicolon-separated fields
    fs::path bankPath = tmpDir / "bad.bank";
    {
        std::ofstream f(bankPath);
        f << "only_one_field\n";
        f << "two;fields\n";
        f << "three;fields;here\n";
    }

    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    ZF2_BANK bank = zfork2_LoadBankV1(ctx, bankPath.wstring().c_str());
    REQUIRE(bank != nullptr);
    CHECK(zfork2_GetBankEntryCount(bank)   == 0u);
    // These lines are skipped before reaching the LoadFile call, so
    // skippedCount_ reflects only "could not load sound" failures.
    // Malformed-field entries are simply ignored, not counted as skipped.
    CHECK(zfork2_GetBankSkippedCount(bank) == 0u);

    zfork2_UnloadBankV1(bank);
    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
    fs::remove_all(tmpDir);
}

TEST_CASE("SoundBank: type field is case-insensitive", "[soundbank][format]") {
    fs::path tmpDir = fs::temp_directory_path() / "libzfork_test_bank_casetype";
    fs::create_directories(tmpDir);
    REQUIRE(WriteMinimalWav(tmpDir / "a.wav"));
    REQUIRE(WriteMinimalWav(tmpDir / "b.wav"));

    fs::path bankPath = tmpDir / "case.bank";
    {
        std::ofstream f(bankPath);
        f << "sound_a;a.wav;sfx;PREDECODED;1.0;0.0;0;0\n";
        f << "sound_b;b.wav;sfx;PreDecoded;1.0;0.0;0;0\n";
    }

    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    ZF2_BANK bank = zfork2_LoadBankV1(ctx, bankPath.wstring().c_str());
    REQUIRE(bank != nullptr);
    CHECK(zfork2_GetBankEntryCount(bank) == 2u);

    zfork2_UnloadBankV1(bank);
    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
    fs::remove_all(tmpDir);
}

// ══════════════════════════════════════════════════════════════════════════════
// 6. Bank lifecycle (load → play → unload while voice still active)
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("SoundBank: unloading bank while voice plays does not crash", "[soundbank][lifecycle]") {
    fs::path tmpDir = fs::temp_directory_path() / "libzfork_test_bank_life";
    fs::create_directories(tmpDir);
    // Very long sound so the voice is still active when we unload the bank
    REQUIRE(WriteMinimalWav(tmpDir / "long.wav", 44100 * 10)); // 10 s
    fs::path bankPath = WriteBankFile(tmpDir, { { "long", "long.wav" } });

    ZF2_CONTEXT ctx = MakeTestContext();
    REQUIRE(zfork2_Start(ctx) == 1);

    ZF2_BANK bank = zfork2_LoadBankV1(ctx, bankPath.wstring().c_str());
    REQUIRE(bank != nullptr);

    ZF2_VOICE v = zfork2_PlayEventV1(ctx, bank, "long");
    CHECK(v != ZF2_INVALID_VOICE);

    std::this_thread::sleep_for(30ms); // let engine start rendering

    // Unload the bank — the voice holds a shared_ptr to the Sound, so the
    // underlying PCM data must stay alive until the voice finishes.
    zfork2_UnloadBankV1(bank);

    std::this_thread::sleep_for(30ms); // should not crash

    CHECK(zfork2_IsVoiceActive(ctx, v) == 1); // still playing

    zfork2_StopVoice(ctx, v);
    std::this_thread::sleep_for(20ms);
    zfork2_Update(ctx);

    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);
    fs::remove_all(tmpDir);
}
