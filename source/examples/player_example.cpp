/**
 * libZFork — Example: Basic Audio Player
 *
 * Demonstrates:
 *  - Opening a file (MP3 / OGG / FLAC / WAV / AAC)
 *  - Playing with volume control
 *  - FFT spectrum data retrieval
 *  - ID3 tag reading
 *  - C++ and C API usage
 */

#define NOMINMAX
#include <libzfork/libzfork.h>
#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <array>
#include <algorithm>
#include <cwctype>
#include <vector>
#include <filesystem>
#include <atomic>
#ifdef _WIN32
#  include <winioctl.h>
#  include <ntddcdrm.h>
#endif

namespace fs = std::filesystem;
enum class InputKind {
    File,
    Folder,
    Playlist,
    AudioCD,
};

struct DemoCallbackState {
    std::atomic<int> pendingNextSongIndex{-1};
};

// ── Helpers ────────────────────────────────────────────────────────────────────

static void PrintStreamInfo(libZFork::ZFork* player, int index) {
    libZFork::StreamInfo info{};
    if (!player->GetStreamInfo(index, info)) {
        std::printf("  [error getting stream info]\n");
        return;
    }
    std::printf("  Sample Rate : %d Hz\n",   info.samplingRate);
    std::printf("  Channels    : %d\n",       info.channelNumber);
    std::printf("  Bitrate     : %d kbps\n",  info.bitrate);
    std::printf("  VBR         : %s\n",        info.vbr ? "yes" : "no");
    std::printf("  Duration    : %02u:%02u:%02u.%03u\n",
                info.length.hms.hour, info.length.hms.minute,
                info.length.hms.second, info.length.hms.millisecond);
}

static void PrintID3(libZFork::ZFork* player, int index) {
    libZFork::ID3InfoEx tag{};
    if (!player->LoadID3Ex(index, tag)) return;
    std::printf("  Title   : %s\n",  tag.title[0]  ? tag.title   : "(none)");
    std::printf("  Artist  : %s\n",  tag.artist[0] ? tag.artist  : "(none)");
    std::printf("  Album   : %s\n",  tag.album[0]  ? tag.album   : "(none)");
    std::printf("  Year    : %s\n",  tag.year[0]   ? tag.year    : "(none)");
    if (tag.bpm > 0) std::printf("  BPM     : %d\n", tag.bpm);
}

static std::wstring ToLowerAsciiW(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
        if (c >= L'A' && c <= L'Z') return static_cast<wchar_t>(c + (L'a' - L'A'));
        return c;
    });
    return s;
}

static bool HasPlaylistExtension(const fs::path& p) {
    const std::wstring ext = ToLowerAsciiW(p.extension().wstring());
    return ext == L".m3u" || ext == L".m3u8" || ext == L".pls";
}

static bool HasSupportedAudioExtension(const fs::path& p) {
    const std::wstring ext = ToLowerAsciiW(p.extension().wstring());
    return ext == L".mp3" || ext == L".wav" || ext == L".wave" ||
           ext == L".ogg" || ext == L".flac" || ext == L".aac" ||
           ext == L".opus" || ext == L".aif" || ext == L".aiff" ||
           ext == L".pcm" || ext == L".raw" || ext == L".cda";
}

static const char* InputKindName(InputKind k) {
    switch (k) {
        case InputKind::File: return "File";
        case InputKind::Folder: return "Folder";
        case InputKind::Playlist: return "Playlist";
        case InputKind::AudioCD: return "Audio-CD";
        default: return "Unknown";
    }
}

static int ZFORK_CALL OnPlayerEvent(void* /*instance*/, void* userData,
                                    libZFork::CallbackMessage msg, uint32_t param1, uint32_t /*param2*/) {
    if (!userData) return 0;
    auto* state = static_cast<DemoCallbackState*>(userData);
    if (msg == libZFork::CallbackMessage::NextSongAsync) {
        state->pendingNextSongIndex.store(static_cast<int>(param1));
    }
    return 0;
}

#ifdef _WIN32
static bool TryParseDriveLetter(const wchar_t* input, wchar_t& outDrive) {
    if (!input || !input[0]) return false;
    if (input[1] == L':' && ((input[0] >= L'A' && input[0] <= L'Z') || (input[0] >= L'a' && input[0] <= L'z'))) {
        outDrive = static_cast<wchar_t>(::towupper(input[0]));
        return true;
    }
    if (!input[1] && ((input[0] >= L'A' && input[0] <= L'Z') || (input[0] >= L'a' && input[0] <= L'z'))) {
        outDrive = static_cast<wchar_t>(::towupper(input[0]));
        return true;
    }
    return false;
}

static std::vector<int> EnumerateAudioCdTracks(wchar_t drive) {
    std::vector<int> tracks;
    const std::wstring root = std::wstring(1, drive) + L":\\";
    if (GetDriveTypeW(root.c_str()) != DRIVE_CDROM) return tracks;

    const std::wstring devPath = L"\\\\.\\" + std::wstring(1, drive) + L":";
    HANDLE h = CreateFileW(devPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return tracks;

    CDROM_TOC toc{};
    DWORD got = 0;
    if (!DeviceIoControl(h, IOCTL_CDROM_READ_TOC, nullptr, 0, &toc, sizeof(toc), &got, nullptr)) {
        CloseHandle(h);
        return tracks;
    }
    CloseHandle(h);

    const int first = static_cast<int>(toc.FirstTrack);
    const int last = static_cast<int>(toc.LastTrack);
    for (int t = first; t <= last; ++t) {
        const auto& td = toc.TrackData[t - first];
        const bool isDataTrack = (td.Control & 0x04) != 0;
        if (!isDataTrack) tracks.push_back(t);
    }
    return tracks;
}
#endif

// ── Main ───────────────────────────────────────────────────────────────────────

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        std::printf("Usage: zfork_player <input(file|folder|playlist|D:)> [--eq] [--echo] [--bpm] [--cd-list]\n");
        return 1;
    }

    const wchar_t* inputPath = argv[1];
    bool doEQ   = false;
    bool doEcho = false;
    bool doBPM  = false;
    bool cdListOnly = false;

    for (int i = 2; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--eq")   == 0) doEQ   = true;
        if (std::wcscmp(argv[i], L"--echo") == 0) doEcho = true;
        if (std::wcscmp(argv[i], L"--bpm")  == 0) doBPM  = true;
        if (std::wcscmp(argv[i], L"--cd-list") == 0) cdListOnly = true;
    }

    // ── Create player ─────────────────────────────────────────────────────────
    libZFork::ZFork* player = libZFork::CreateZFork();
    if (!player) {
        std::printf("CreateZFork() failed\n");
        return 1;
    }
    DemoCallbackState cbState{};
    (void)player->SetCallback(OnPlayerEvent, &cbState);

    // ── Open input (file/folder/playlist/audio-cd) ───────────────────────────
    bool sourceLoaded = false;
    InputKind inputKind = InputKind::File;
    const fs::path path(inputPath ? inputPath : L"");

#ifdef _WIN32
    wchar_t cdDrive = 0;
    if (TryParseDriveLetter(inputPath, cdDrive)) {
        const auto tracks = EnumerateAudioCdTracks(cdDrive);
        if (cdListOnly) {
            if (tracks.empty()) {
                std::printf("No audio CD tracks found on drive %c:\n", static_cast<char>(cdDrive));
            } else {
                std::printf("Audio CD tracks on %c::", static_cast<char>(cdDrive));
                for (int t : tracks) std::printf(" %02d", t);
                std::printf("\n");
            }
            libZFork::DestroyZFork(player);
            return tracks.empty() ? 1 : 0;
        }
        if (!tracks.empty()) {
            inputKind = InputKind::AudioCD;
            wchar_t trackPath[64]{};
            std::swprintf(trackPath, 64, L"%c:\\Track%02d.cda", cdDrive, tracks.front());
            sourceLoaded = player->OpenFile(trackPath, libZFork::StreamFormat::CDAudio);
            for (size_t i = 1; i < tracks.size() && sourceLoaded; ++i) {
                std::swprintf(trackPath, 64, L"%c:\\Track%02d.cda", cdDrive, tracks[i]);
                sourceLoaded = player->AddFile(trackPath, libZFork::StreamFormat::CDAudio);
            }
        }
    }
#endif

    if (!sourceLoaded && HasPlaylistExtension(path)) {
        inputKind = InputKind::Playlist;
        sourceLoaded = player->LoadPlaylistFile(inputPath, libZFork::StreamFormat::Autodetect);
    } else if (!sourceLoaded && fs::is_directory(path)) {
        inputKind = InputKind::Folder;
        std::vector<std::wstring> files;
        for (const auto& entry : fs::directory_iterator(path)) {
            if (!entry.is_regular_file()) continue;
            if (!HasSupportedAudioExtension(entry.path())) continue;
            files.push_back(entry.path().wstring());
        }
        std::sort(files.begin(), files.end());
        if (!files.empty()) {
            sourceLoaded = player->OpenFile(files.front().c_str(), libZFork::StreamFormat::Autodetect);
            for (size_t i = 1; i < files.size() && sourceLoaded; ++i) {
                sourceLoaded = player->AddFile(files[i].c_str(), libZFork::StreamFormat::Autodetect);
            }
        }
    } else if (!sourceLoaded) {
        sourceLoaded = player->OpenFile(inputPath, libZFork::StreamFormat::Autodetect);
    }

    if (!sourceLoaded) {
        std::printf("Input load failed: %s\n", player->GetError());
        libZFork::DestroyZFork(player);
        return 1;
    }

    std::printf("=== libZFork Player ===\n");
    std::printf("Source Type: %s\n", InputKindName(inputKind));
    std::printf("Input: ");
    std::fputws(inputPath, stdout);
    std::printf("\nQueue Count: %u\n\nStream Info:\n", player->GetQueueCount());
    PrintStreamInfo(player, 0);
    std::printf("\nID3 Tags:\n");
    PrintID3(player, 0);
    std::printf("\nHotkeys:\n");
    std::printf("  Q/Enter = Stop    P = Pause/Resume    +/- = Volume\n\n");

    // ── Configure effects ─────────────────────────────────────────────────────
    if (doEQ) {
        // Smiley-face EQ curve
        player->EnableEqualizer(true);
        const float gains[] = {
             6, 5, 4, 3, 2, 1, 0,-1,-2,-2,-2,-2,-2,-1, 0, 0,
             0, 1, 2, 3, 4, 5, 5, 5, 5, 4, 3, 2, 1, 0,-1
        };
        for (int b = 0; b < 31; ++b)
            player->SetEqualizerBandGain(b, gains[b]);
        std::printf("Equalizer enabled (smiley curve)\n");
    }

    if (doEcho) {
        libZFork::EchoParams ep{};
        ep.leftDelay      = 250;
        ep.leftSourceVol  = 100;
        ep.leftEchoVol    = 40;
        ep.rightDelay     = 280;
        ep.rightSourceVol = 100;
        ep.rightEchoVol   = 40;
        player->EnableEcho(true);
        player->SetEchoParams(ep);
        std::printf("Echo enabled (250/280 ms)\n");
    }

    // ── BPM detection (before playback) ───────────────────────────────────────
    if (doBPM) {
        std::printf("Detecting BPM...\n");
        float bpm = 0.f;
        if (player->DetectBPM(libZFork::BPMAlgorithm::Advanced, bpm))
            std::printf("BPM (advanced): %.1f\n\n", bpm);
        else
            std::printf("BPM detection failed\n\n");
    }

    // ── Play ──────────────────────────────────────────────────────────────────
    player->SetMasterVolume(80, 80);
    if (!player->Play()) {
        std::printf("Play() failed: %s\n", player->GetError());
        libZFork::DestroyZFork(player);
        return 1;
    }
    std::printf("Playing... Press Enter to stop, +/- to adjust volume.\n\n");

    // ── Playback loop ─────────────────────────────────────────────────────────
    int volume = 80;
    while (true) {
        // Non-blocking key check
        if (_kbhit()) {
            int ch = _getch();
            if (ch == '\r' || ch == '\n' || ch == 'q' || ch == 'Q') break;
            if (ch == '+' || ch == '=') {
                volume = std::min(100, volume + 5);
                player->SetMasterVolume(volume, volume);
                std::printf("\rVolume: %d%%   ", volume);
            }
            if (ch == '-') {
                volume = std::max(0, volume - 5);
                player->SetMasterVolume(volume, volume);
                std::printf("\rVolume: %d%%   ", volume);
            }
            if (ch == 'p' || ch == 'P') {
                auto status = player->GetStatus();
                if (status.paused) player->Resume();
                else               player->Pause();
            }
        }

        // Print position
        libZFork::StreamTime pos{};
        if (player->GetPosition(pos)) {
            std::printf("\r  Position: %02u:%02u:%02u  Vol: %d%%   ",
                        pos.hms.hour, pos.hms.minute, pos.hms.second, volume);
        }

        // Show VU meter
        libZFork::VUData vu{};
        if (player->GetVUData(vu)) {
            int barL = static_cast<int>(vu.left  * 20);
            int barR = static_cast<int>(vu.right * 20);
            std::printf(" L:[");
            for (int i = 0; i < 20; ++i) std::printf(i < barL ? "#" : " ");
            std::printf("] R:[");
            for (int i = 0; i < 20; ++i) std::printf(i < barR ? "#" : " ");
            std::printf("]");
        }

        const int nextSongIdx = cbState.pendingNextSongIndex.exchange(-1);
        if (nextSongIdx >= 0) {
            std::printf("\n\nNow playing queue index %d:\n", nextSongIdx);
            PrintStreamInfo(player, nextSongIdx);
            PrintID3(player, nextSongIdx);
        }

        // Check if stopped
        auto st = player->GetStatus();
        if (!st.playing && !st.paused) {
            std::printf("\n\nPlayback complete.\n");
            break;
        }

        Sleep(100);
    }

    player->Stop();

    // ── FFT example ───────────────────────────────────────────────────────────
    std::printf("\nLast FFT snapshot (first 8 bins):\n");
    std::array<float, 2048> fftL{}, fftR{};
    if (player->GetFFTData(fftL.data(), fftR.data(), 2048)) {
        for (int i = 0; i < 8; ++i)
            std::printf("  Bin %2d:  L=%.4f  R=%.4f\n", i, fftL[i], fftR[i]);
    }

    libZFork::DestroyZFork(player);
    return 0;
}

// ── C API demonstration (optional, compiled separately) ───────────────────────
#ifdef DEMO_C_API
void DemoCAPI(const wchar_t* path) {
    ZFORK_HANDLE h = zfork_Create();
    zfork_OpenFile(h, path, static_cast<int>(libZFork::StreamFormat::Autodetect));
    zfork_SetMasterVolume(h, 80, 80);
    zfork_Play(h);
    Sleep(5000);
    zfork_Stop(h);
    zfork_Destroy(h);
}
#endif
