/**
 * libZFork — Soundcard Test
 *
 * Zeigt alle WaveOut/WaveIn-Geräte an und spielt auf jedem einen
 * kurzen 440 Hz Sinuston (1 Sekunde) zur Überprüfung.
 *
 * Usage:
 *   soundcard_test [--list]          Nur auflisten, nicht spielen
 *   soundcard_test [--device N]      Nur Gerät N testen (0-basiert)
 *   soundcard_test [--freq Hz]       Testfrequenz (default: 440 Hz)
 *   soundcard_test [--dur ms]        Dauer in ms   (default: 1000)
 */

#define NOMINMAX
#include <libzfork/libzfork.h>
#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <numbers>
#pragma comment(lib, "winmm.lib")

// ── Argparse helpers ──────────────────────────────────────────────────────────

static bool HasFlag(int argc, char* argv[], const char* flag) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], flag) == 0) return true;
    return false;
}

static int GetInt(int argc, char* argv[], const char* key, int def) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], key) == 0) return std::atoi(argv[i + 1]);
    return def;
}

// ── Generate sine wave PCM (16-bit stereo) ────────────────────────────────────

static std::vector<int16_t> GenerateSine(int sampleRate, int channels,
                                          double freqHz, int durationMs,
                                          float amplitude = 0.5f) {
    int frames  = sampleRate * durationMs / 1000;
    std::vector<int16_t> pcm(frames * channels);
    for (int i = 0; i < frames; ++i) {
        // Fade-in/out to avoid clicks (10 ms)
        int fadeFrames = std::min(frames / 2, sampleRate / 100);
        float env = 1.f;
        if (i < fadeFrames)               env = static_cast<float>(i) / fadeFrames;
        if (i >= frames - fadeFrames)     env = static_cast<float>(frames - 1 - i) / fadeFrames;
        float sample = amplitude * env *
                       static_cast<float>(std::sin(2.0 * std::numbers::pi * freqHz * i / sampleRate));
        int16_t s16  = static_cast<int16_t>(sample * 32767.f);
        for (int c = 0; c < channels; ++c) pcm[i * channels + c] = s16;
    }
    return pcm;
}

// ── Play PCM directly via waveOut on a specific device ────────────────────────

static bool PlayOnDevice(int deviceId, const std::vector<int16_t>& pcm,
                         int sampleRate, int channels) {
    WAVEFORMATEX wfx{};
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = static_cast<WORD>(channels);
    wfx.nSamplesPerSec  = static_cast<DWORD>(sampleRate);
    wfx.wBitsPerSample  = 16;
    wfx.nBlockAlign     = static_cast<WORD>(channels * 2);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    HWAVEOUT hwo = nullptr;
    MMRESULT r = waveOutOpen(&hwo, static_cast<UINT>(deviceId),
                             &wfx, 0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR) {
        char err[256]; waveOutGetErrorTextA(r, err, sizeof(err));
        std::printf("     waveOutOpen error: %s\n", err);
        return false;
    }

    WAVEHDR hdr{};
    hdr.lpData         = reinterpret_cast<LPSTR>(const_cast<int16_t*>(pcm.data()));
    hdr.dwBufferLength = static_cast<DWORD>(pcm.size() * 2);
    waveOutPrepareHeader(hwo, &hdr, sizeof(hdr));
    waveOutWrite(hwo, &hdr, sizeof(hdr));

    // Wait until done
    while (!(hdr.dwFlags & WHDR_DONE)) Sleep(10);

    waveOutUnprepareHeader(hwo, &hdr, sizeof(hdr));
    waveOutClose(hwo);
    return true;
}

// ── Print capabilities ─────────────────────────────────────────────────────────

static void PrintWaveOutCaps(UINT id) {
    WAVEOUTCAPSW caps{};
    if (waveOutGetDevCapsW(id, &caps, sizeof(caps)) != MMSYSERR_NOERROR) return;

    char name[256]{};
    WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, name, 256, nullptr, nullptr);
    std::printf("  [%u] %s\n", id, name);
    std::printf("       Channels  : %u\n", caps.wChannels);

    // Format flags
    const struct { DWORD flag; const char* label; } fmts[] = {
        {WAVE_FORMAT_1M08,"11kHz Mono 8b"}, {WAVE_FORMAT_1S08,"11kHz St 8b"},
        {WAVE_FORMAT_1M16,"11kHz Mono 16b"},{WAVE_FORMAT_1S16,"11kHz St 16b"},
        {WAVE_FORMAT_2M08,"22kHz Mono 8b"}, {WAVE_FORMAT_2S08,"22kHz St 8b"},
        {WAVE_FORMAT_2M16,"22kHz Mono 16b"},{WAVE_FORMAT_2S16,"22kHz St 16b"},
        {WAVE_FORMAT_4M08,"44kHz Mono 8b"}, {WAVE_FORMAT_4S08,"44kHz St 8b"},
        {WAVE_FORMAT_4M16,"44kHz Mono 16b"},{WAVE_FORMAT_4S16,"44kHz St 16b"},
        {WAVE_FORMAT_48M16,"48kHz Mono 16b"},{WAVE_FORMAT_48S16,"48kHz St 16b"},
        {WAVE_FORMAT_96M16,"96kHz Mono 16b"},{WAVE_FORMAT_96S16,"96kHz St 16b"},
    };
    std::printf("       Formats   :");
    for (auto& f : fmts) if (caps.dwFormats & f.flag) std::printf(" [%s]", f.label);
    std::printf("\n");

    // Support flags
    if (caps.dwSupport & WAVECAPS_VOLUME)      std::printf("       Volume control supported\n");
    if (caps.dwSupport & WAVECAPS_LRVOLUME)    std::printf("       L/R volume control supported\n");
    if (caps.dwSupport & WAVECAPS_PITCH)        std::printf("       Pitch control supported\n");
    if (caps.dwSupport & WAVECAPS_PLAYBACKRATE) std::printf("       Playback rate control supported\n");
    if (caps.dwSupport & WAVECAPS_SYNC)         std::printf("       Synchronous driver\n");
}

static void PrintWaveInCaps(UINT id) {
    WAVEINCAPSW caps{};
    if (waveInGetDevCapsW(id, &caps, sizeof(caps)) != MMSYSERR_NOERROR) return;
    char name[256]{};
    WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, name, 256, nullptr, nullptr);
    std::printf("  [%u] %s  (channels: %u)\n", id, name, caps.wChannels);
}

// ── libZFork device enumeration (using the library API) ───────────────────────

static void EnumerateViaLibZFork() {
    auto* player = libZFork::CreateZFork();
    if (!player) return;

    std::printf("\n── Via libZFork API ────────────────────────────────────────\n");

    int outDevs = player->EnumerateWaveOutDevices();
    std::printf("WaveOut devices: %d\n", outDevs);
    for (int i = 0; i < outDevs; ++i) {
        libZFork::WaveDeviceInfo info{};
        if (player->GetWaveOutDeviceInfo(i, info))
            std::printf("  [%d] %s  (ch:%u)\n", i, info.name, info.channels);
    }

    int inDevs = player->EnumerateWaveInDevices();
    std::printf("WaveIn devices: %d\n", inDevs);
    for (int i = 0; i < inDevs; ++i) {
        libZFork::WaveDeviceInfo info{};
        if (player->GetWaveInDeviceInfo(i, info))
            std::printf("  [%d] %s  (ch:%u)\n", i, info.name, info.channels);
    }

    libZFork::DestroyZFork(player);
}

// ── Main ───────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    bool listOnly  = HasFlag(argc, argv, "--list");
    int  targetDev = GetInt(argc, argv, "--device", -1);   // -1 = all
    int  freqHz    = GetInt(argc, argv, "--freq",   440);
    int  durMs     = GetInt(argc, argv, "--dur",    1000);

    std::printf("=== libZFork Soundcard Test ===\n");
    std::printf("libZFork version: %s\n\n", LIBZFORK_VERSION_STRING);

    // ── WaveOut (Ausgabe) ──────────────────────────────────────────────────────
    UINT numOut = waveOutGetNumDevs();
    std::printf("━━ WaveOut (Ausgabe) ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  %u Gerät(e)\n", numOut);

    // WAVE_MAPPER (default device)
    std::printf("  [WAVE_MAPPER] Standard-Ausgabegerät\n");

    for (UINT i = 0; i < numOut; ++i) PrintWaveOutCaps(i);

    // ── WaveIn (Eingabe) ───────────────────────────────────────────────────────
    UINT numIn = waveInGetNumDevs();
    std::printf("\n━━ WaveIn (Eingabe) ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  %u Gerät(e)\n", numIn);
    for (UINT i = 0; i < numIn; ++i) PrintWaveInCaps(i);

    // ── libZFork API check ─────────────────────────────────────────────────────
    EnumerateViaLibZFork();

    if (listOnly) return 0;

    // ── Ton-Test ──────────────────────────────────────────────────────────────
    auto sine44 = GenerateSine(44100, 2, freqHz, durMs);
    std::printf("\n━━ Ton-Test (%.0f Hz Sinus, %d ms) ━━━━━━━━━━━━━━━━\n",
                static_cast<double>(freqHz), durMs);

    auto testDevice = [&](UINT id, const char* label) {
        std::printf("  → Teste %s...", label);
        std::fflush(stdout);
        bool ok = PlayOnDevice(static_cast<int>(id), sine44, 44100, 2);
        std::printf(" %s\n", ok ? "OK" : "FEHLER");
    };

    if (targetDev < 0) {
        testDevice(WAVE_MAPPER, "WAVE_MAPPER (Standard)");
        for (UINT i = 0; i < numOut; ++i) {
            WAVEOUTCAPSW caps{};
            waveOutGetDevCapsW(i, &caps, sizeof(caps));
            char name[256]{};
            WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, name, 256, nullptr, nullptr);
            testDevice(i, name);
        }
    } else {
        WAVEOUTCAPSW caps{};
        waveOutGetDevCapsW(targetDev, &caps, sizeof(caps));
        char name[256]{};
        WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, name, 256, nullptr, nullptr);
        testDevice(static_cast<UINT>(targetDev), name);
    }

    // ── libZFork: PCM-Stream Test ──────────────────────────────────────────────
    std::printf("\n━━ libZFork PCM-Stream Test ━━━━━━━━━━━━━━━━━━━━━━\n");
    auto* player = libZFork::CreateZFork();
    if (player) {
        libZFork::PCMFormat fmt{ 44100, 2, 16, false };
        if (player->OpenPCMStream(fmt)) {
            if (targetDev >= 0) player->SetWaveOutDevice(targetDev);
            player->SetMasterVolume(70, 70);
            if (player->Play()) {
                // Push sine wave data
                auto sine = GenerateSine(44100, 2, static_cast<double>(freqHz), durMs);
                player->PushPCMData(sine.data(), static_cast<uint32_t>(sine.size() * 2));
                std::printf("  PCM-Stream über libZFork: OK (%.0f Hz, %d ms)\n",
                            static_cast<double>(freqHz), durMs);
                Sleep(durMs + 200);
                player->Stop();
            } else {
                std::printf("  Play() fehler: %s\n", player->GetError());
            }
        }
        libZFork::DestroyZFork(player);
    }

    std::printf("\n=== Test abgeschlossen ===\n");
    return 0;
}
