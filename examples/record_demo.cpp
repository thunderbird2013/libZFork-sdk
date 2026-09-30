/**
 * libZFork — Record Demo
 *
 * Portiert von libZPlay/example/record_to_mp3.cpp + record_to_ogg.cpp
 *
 * Nimmt Audio vom Standard-Aufnahmegerät (Line-In / Mikrofon) auf
 * und speichert es gleichzeitig als MP3, OGG oder FLAC.
 *
 * Usage:
 *   record_demo <ausgabe.mp3|.ogg|.flac|.wav> [--device N] [--dur Sek]
 *
 * Tasten während Aufnahme:  Space=Pause  Q=Stopp+Speichern
 */

#define NOMINMAX
#include <libzfork/libzfork.h>
#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>

// ── Helfer ─────────────────────────────────────────────────────────────────────

static std::wstring Utf8ToWide(const char* s) {
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), n);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

static int GetInt(int argc, char* argv[], const char* key, int def) {
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], key)) return std::atoi(argv[i + 1]);
    return def;
}

static libZFork::StreamFormat FormatFromPath(const char* path) {
    const char* ext = std::strrchr(path, '.');
    if (!ext) return libZFork::StreamFormat::Wav;
    if (!_stricmp(ext, ".mp3"))  return libZFork::StreamFormat::Mp3;
    if (!_stricmp(ext, ".ogg"))  return libZFork::StreamFormat::Ogg;
    if (!_stricmp(ext, ".flac")) return libZFork::StreamFormat::FLAC;
    return libZFork::StreamFormat::Wav;
}

static const char* FormatName(libZFork::StreamFormat f) {
    switch (f) {
        case libZFork::StreamFormat::Mp3:  return "MP3";
        case libZFork::StreamFormat::Ogg:  return "OGG Vorbis";
        case libZFork::StreamFormat::FLAC: return "FLAC";
        default:                           return "WAV";
    }
}

// ── VU-Balken ──────────────────────────────────────────────────────────────────

static void PrintVU(float l, float r, uint32_t secElapsed) {
    int bL = static_cast<int>(std::clamp(l, 0.f, 1.f) * 30);
    int bR = static_cast<int>(std::clamp(r, 0.f, 1.f) * 30);
    std::printf("\r  REC %02u:%02u  L[", secElapsed / 60, secElapsed % 60);
    for (int i = 0; i < 30; ++i)
        std::printf(i < bL ? (i < 24 ? "#" : (i < 28 ? "!" : "*")) : " ");
    std::printf("] R[");
    for (int i = 0; i < 30; ++i)
        std::printf(i < bR ? (i < 24 ? "#" : (i < 28 ? "!" : "*")) : " ");
    std::printf("]  ");
    std::fflush(stdout);
}

// ── Hauptprogramm ─────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::printf("Usage: record_demo <ausgabe.mp3|.ogg|.flac|.wav> [--device N] [--dur Sek]\n");
        std::printf("  --device N   WaveIn-Geräte-Index (0=Standard)\n");
        std::printf("  --dur Sek    Maximale Aufnahmedauer in Sekunden (0=unbegrenzt)\n");
        return 1;
    }

    const char* outPath = argv[1];
    int  deviceId = GetInt(argc, argv, "--device",  0);
    int  maxSec   = GetInt(argc, argv, "--dur",     0);
    auto fmt      = FormatFromPath(outPath);

    std::printf("=== libZFork Record Demo ===\n");
    std::printf("Ausgabe : %s\n", outPath);
    std::printf("Format  : %s\n", FormatName(fmt));
    std::printf("Gerät   : %d\n", deviceId);
    if (maxSec > 0) std::printf("Max. Dauer: %d Sek\n", maxSec);
    std::printf("\n");

    // ── Player erstellen ──────────────────────────────────────────────────────
    auto* player = libZFork::CreateZFork();
    if (!player) { std::printf("CreateZFork() fehlgeschlagen\n"); return 1; }

    // WaveIn-Gerät auswählen
    if (deviceId >= 0) player->SetWaveInDevice(deviceId);

    // WaveIn-Stream öffnen: URL-Format "wavein://src=line;volume=50;"
    // libZFork öffnet den WaveIn direkt via PCMFormat
    libZFork::PCMFormat pcmFmt{ 44100, 2, 16, false };
    if (!player->OpenPCMStream(pcmFmt)) {
        std::printf("OpenPCMStream (WaveIn) fehlgeschlagen: %s\n", player->GetError());
        libZFork::DestroyZFork(player);
        return 1;
    }

    // Ausgabedatei für Encoding setzen
    auto wOut = Utf8ToWide(outPath);
    if (!player->SetWaveOutFile(wOut.c_str())) {
        std::printf("SetWaveOutFile fehlgeschlagen: %s\n", player->GetError());
        libZFork::DestroyZFork(player);
        return 1;
    }

    // Encoding starten
    if (!player->StartEncoding(fmt, wOut.c_str(), nullptr, nullptr)) {
        std::printf("StartEncoding fehlgeschlagen: %s\n", player->GetError());
        libZFork::DestroyZFork(player);
        return 1;
    }

    if (!player->Play()) {
        std::printf("Play() fehlgeschlagen: %s\n", player->GetError());
        libZFork::DestroyZFork(player);
        return 1;
    }

    std::printf("Aufnahme läuft...  Space=Pause  Q=Stopp\n\n");

    DWORD t0 = GetTickCount();
    bool running = true;

    while (running) {
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 'q' || ch == 'Q' || ch == 27) break;
            if (ch == ' ') {
                auto st = player->GetStatus();
                if (st.paused) player->Resume(); else player->Pause();
            }
        }

        DWORD elapsed = (GetTickCount() - t0) / 1000;
        if (maxSec > 0 && static_cast<int>(elapsed) >= maxSec) break;

        libZFork::VUData vu{};
        player->GetVUData(vu);
        PrintVU(vu.left, vu.right, elapsed);

        auto st = player->GetStatus();
        if (!st.playing && !st.paused) break;

        Sleep(50);
    }

    player->StopEncoding();
    player->Stop();
    libZFork::DestroyZFork(player);

    DWORD totalMs = GetTickCount() - t0;
    std::printf("\n\nAufnahme gestoppt.\n");
    std::printf("Dauer  : %u:%02u\n", (totalMs/1000)/60, (totalMs/1000)%60);
    std::printf("Gespeichert: %s\n", outPath);
    return 0;
}
