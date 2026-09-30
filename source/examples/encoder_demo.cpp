/**
 * libZFork — Encoder Demo
 *
 * Demonstrates real-time format conversion using libZFork's encode-while-play API:
 *   StartEncoding()  →  Play()  →  wait for completion  →  StopEncoding()
 *
 * Supported source:  MP3, OGG, FLAC, WAV, AAC
 * Supported output:  WAV, OGG, FLAC, MP3
 *
 * Usage:
 *   encoder_demo <input_file> <output_file> [--format wav|ogg|flac|mp3]
 */

#define NOMINMAX
#include <libzfork/libzfork.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>

// ── Helpers ────────────────────────────────────────────────────────────────────

static std::wstring Utf8ToWide(const char* s) {
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), n);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

static const char* GetArg(int argc, char* argv[], const char* key) {
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], key)) return argv[i + 1];
    return nullptr;
}

static libZFork::StreamFormat ParseOutputFormat(const char* ext, const char* fmtArg) {
    auto eq = [](const char* a, const char* b) {
        return a && !_stricmp(a, b);
    };
    if (eq(fmtArg, "wav")  || eq(ext, ".wav"))  return libZFork::StreamFormat::Wav;
    if (eq(fmtArg, "ogg")  || eq(ext, ".ogg"))  return libZFork::StreamFormat::Ogg;
    if (eq(fmtArg, "flac") || eq(ext, ".flac")) return libZFork::StreamFormat::FLAC;
    if (eq(fmtArg, "mp3")  || eq(ext, ".mp3"))  return libZFork::StreamFormat::Mp3;
    return libZFork::StreamFormat::Wav;
}

static const char* FormatName(libZFork::StreamFormat f) {
    switch (f) {
        case libZFork::StreamFormat::Wav:  return "WAV";
        case libZFork::StreamFormat::Ogg:  return "OGG Vorbis";
        case libZFork::StreamFormat::FLAC: return "FLAC";
        case libZFork::StreamFormat::Mp3:  return "MP3";
        default:                           return "Unknown";
    }
}

static void PrintProgress(double ratio, uint32_t posS, uint32_t totS) {
    int bar = static_cast<int>(ratio * 40);
    std::printf("\r  [");
    for (int i = 0; i < 40; ++i) std::printf(i < bar ? "█" : "░");
    std::printf("]  %02u:%02u / %02u:%02u  %5.1f%%",
                posS / 60, posS % 60,
                totS / 60, totS % 60,
                ratio * 100.0);
    std::fflush(stdout);
}

// ── Main ───────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::printf("Usage: encoder_demo <input> <output> [--format wav|ogg|flac|mp3]\n");
        std::printf("  Supported inputs:  MP3, OGG, FLAC, WAV, AAC\n");
        std::printf("  Supported outputs: WAV, OGG, FLAC, MP3\n");
        return 1;
    }

    const char* inPath  = argv[1];
    const char* outPath = argv[2];
    const char* fmtArg  = GetArg(argc, argv, "--format");

    std::string outStr(outPath);
    std::string ext;
    auto dotPos = outStr.rfind('.');
    if (dotPos != std::string::npos) ext = outStr.substr(dotPos);

    auto fmt = ParseOutputFormat(ext.c_str(), fmtArg);

    std::printf("=== libZFork Encoder Demo ===\n");
    std::printf("Input  : %s\n", inPath);
    std::printf("Output : %s\n", outPath);
    std::printf("Format : %s\n\n", FormatName(fmt));

    // ── Create player and open source ─────────────────────────────────────────
    auto* player = libZFork::CreateZFork();
    if (!player) {
        std::printf("CreateZFork() failed\n");
        return 1;
    }

    auto wIn  = Utf8ToWide(inPath);
    auto wOut = Utf8ToWide(outPath);

    if (!player->OpenFile(wIn.c_str(), libZFork::StreamFormat::Autodetect)) {
        std::printf("OpenFile failed: %s\n", player->GetError());
        libZFork::DestroyZFork(player);
        return 1;
    }

    libZFork::StreamInfo info{};
    player->GetStreamInfo(0, info);

    libZFork::ID3InfoEx tag{};
    player->LoadID3Ex(0, tag);

    std::printf("Source:\n");
    std::printf("  Sample rate  : %d Hz\n",  info.samplingRate);
    std::printf("  Channels     : %d\n",      info.channelNumber);
    std::printf("  Bitrate      : %d kbps%s\n", info.bitrate, info.vbr ? " (VBR)" : "");
    std::printf("  Duration     : %02u:%02u:%02u\n",
                info.length.hms.hour, info.length.hms.minute, info.length.hms.second);
    if (tag.title[0])  std::printf("  Title        : %s\n", tag.title);
    if (tag.artist[0]) std::printf("  Artist       : %s\n", tag.artist);
    std::printf("\n");

    // ── Start encoding then play ───────────────────────────────────────────────
    // StartEncoding hooks into the decode pipeline; audio is written to the
    // output file as the player decodes each block.  Pass nullptr callbacks
    // to use the built-in file writer.
    if (!player->StartEncoding(fmt, wOut.c_str(), nullptr, nullptr)) {
        std::printf("StartEncoding failed: %s\n", player->GetError());
        libZFork::DestroyZFork(player);
        return 1;
    }

    // Set volume to 0 so we don't disturb the user during a pure conversion
    player->SetMasterVolume(0, 0);

    if (!player->Play()) {
        std::printf("Play() failed: %s\n", player->GetError());
        player->StopEncoding();
        libZFork::DestroyZFork(player);
        return 1;
    }

    std::printf("Encoding...\n");
    DWORD t0 = GetTickCount();

    uint32_t totS = info.length.hms.hour * 3600u +
                    info.length.hms.minute * 60u +
                    info.length.hms.second;

    while (true) {
        auto st = player->GetStatus();
        if (!st.playing && !st.paused) break;

        libZFork::StreamTime pos{};
        player->GetPosition(pos);
        uint32_t posS = pos.hms.hour * 3600u + pos.hms.minute * 60u + pos.hms.second;

        double ratio = (totS > 0) ? static_cast<double>(posS) / totS : 0.0;
        PrintProgress(ratio, posS, totS);

        Sleep(100);
    }

    player->StopEncoding();
    player->Stop();

    DWORD elapsed = GetTickCount() - t0;

    std::printf("\n\nDone.\n");
    std::printf("  Elapsed time  : %.2f s\n", elapsed / 1000.0);
    if (elapsed > 0 && totS > 0)
        std::printf("  Speed         : %.1fx realtime\n",
                    totS * 1000.0 / elapsed);

    // Output file size
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (GetFileAttributesExW(wOut.c_str(), GetFileExInfoStandard, &fa)) {
        uint64_t size = (static_cast<uint64_t>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
        std::printf("  Output size   : %.1f KB\n", size / 1024.0);
    }

    libZFork::DestroyZFork(player);
    std::printf("\nOutput written to: %s\n", outPath);
    return 0;
}
