#include <libzfork/libzfork.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#ifdef _WIN32
#  include <windows.h>
#  include <winioctl.h>
#  include <ntddcdrm.h>
#endif

using namespace libZFork;
namespace fs = std::filesystem;

static bool IsNumericArg(const char* s) {
    if (!s || !*s) return false;
    char* end = nullptr;
    (void)std::strtod(s, &end);
    return end && *end == '\0';
}

static std::string ToLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static bool HasPlaylistExtension(const fs::path& p) {
    const std::string ext = ToLowerAscii(p.extension().string());
    return ext == ".m3u" || ext == ".m3u8" || ext == ".pls";
}

static bool HasSupportedAudioExtension(const fs::path& p) {
    const std::string ext = ToLowerAscii(p.extension().string());
    return ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".flac" ||
           ext == ".aac" || ext == ".m4a" || ext == ".opus" || ext == ".aiff" ||
           ext == ".cda" ||
           ext == ".aif";
}

#ifdef _WIN32
static bool TryParseDriveLetter(const std::string& input, wchar_t& outDrive) {
    if (input.empty()) return false;
    if (input.size() >= 2 && input[1] == ':') {
        const unsigned char c = static_cast<unsigned char>(input[0]);
        if (std::isalpha(c)) {
            outDrive = static_cast<wchar_t>(std::toupper(c));
            return true;
        }
    }
    if (input.size() == 1 && std::isalpha(static_cast<unsigned char>(input[0]))) {
        outDrive = static_cast<wchar_t>(std::toupper(static_cast<unsigned char>(input[0])));
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

int main(int argc, char** argv) {
    if (argc < 5) {
        std::printf("Usage: broadcast_demo <input(file|folder|playlist)> <host> <port> <password> [mount] [bitrate] [channels:1|2] [mode:auto|stereo|joint|mono] [micDevice] [duckPreset:soft|radio|hard|custom] [duckDb] [attackMs] [releaseMs] [threshold]\n");
        std::printf("   or: broadcast_demo ... [micDevice] --duck-preset <soft|radio|hard|custom> [--duck-db N] [--duck-attack MS] [--duck-release MS] [--duck-threshold T]\n");
        std::printf("   tip: input can be CD drive (e.g. D:) and --cd-list prints audio tracks.\n");
        return 1;
    }

    const char* inputPathUtf8 = argv[1];
    const char* host = argv[2];
    const int port = std::atoi(argv[3]);
    const char* password = argv[4];
    const char* mount = (argc >= 6) ? argv[5] : "/stream";
    const int bitrate = (argc >= 7) ? std::atoi(argv[6]) : 192;
    const int channels = (argc >= 8) ? std::atoi(argv[7]) : 2;
    const char* mode = (argc >= 9) ? argv[8] : "joint";
    const int micDevice = (argc >= 10) ? std::atoi(argv[9]) : -999; // -999 => disabled

    const char* duckPresetFromFlag = nullptr;
    const char* duckDbFromFlag = nullptr;
    const char* attackFromFlag = nullptr;
    const char* releaseFromFlag = nullptr;
    const char* thresholdFromFlag = nullptr;
    bool cdListOnly = false;
    std::vector<const char*> extraPositional;
    for (int i = 10; i < argc; ++i) {
        const char* arg = argv[i];
        if (!arg) continue;
        if (std::strcmp(arg, "--cd-list") == 0) {
            cdListOnly = true;
            continue;
        }
        if (std::strcmp(arg, "--duck-preset") == 0 && (i + 1) < argc) {
            duckPresetFromFlag = argv[++i];
            continue;
        }
        if (std::strcmp(arg, "--duck-db") == 0 && (i + 1) < argc) {
            duckDbFromFlag = argv[++i];
            continue;
        }
        if (std::strcmp(arg, "--duck-attack") == 0 && (i + 1) < argc) {
            attackFromFlag = argv[++i];
            continue;
        }
        if (std::strcmp(arg, "--duck-release") == 0 && (i + 1) < argc) {
            releaseFromFlag = argv[++i];
            continue;
        }
        if (std::strcmp(arg, "--duck-threshold") == 0 && (i + 1) < argc) {
            thresholdFromFlag = argv[++i];
            continue;
        }
        extraPositional.push_back(arg);
    }

    const char* duckPreset = "radio";
    float duckDb = -12.0f;
    uint32_t attackMs = 30u;
    uint32_t releaseMs = 260u;
    float threshold = 0.02f;
    size_t valueArgStart = 0;
    // Backward compatible positional parsing:
    // - first extra numeric  => old syntax (duckDb, attack, release, threshold)
    // - first extra text     => preset, then optional numeric overrides
    if (!extraPositional.empty()) {
        if (IsNumericArg(extraPositional[0])) {
            duckPreset = "custom";
        } else {
            duckPreset = extraPositional[0];
            valueArgStart = 1;
        }
    }
    if (duckPresetFromFlag && *duckPresetFromFlag) duckPreset = duckPresetFromFlag;

    if (std::strcmp(duckPreset, "soft") == 0) {
        duckDb = -8.0f; attackMs = 40u; releaseMs = 340u; threshold = 0.03f;
    } else if (std::strcmp(duckPreset, "hard") == 0) {
        duckDb = -16.0f; attackMs = 15u; releaseMs = 180u; threshold = 0.012f;
    } else if (std::strcmp(duckPreset, "custom") == 0) {
        // keep defaults; values below can override.
    } else {
        // "radio" default
        duckPreset = "radio";
        duckDb = -12.0f; attackMs = 30u; releaseMs = 260u; threshold = 0.02f;
    }
    if (valueArgStart < extraPositional.size()) duckDb = static_cast<float>(std::atof(extraPositional[valueArgStart]));
    if ((valueArgStart + 1) < extraPositional.size()) attackMs = static_cast<uint32_t>(std::atoi(extraPositional[valueArgStart + 1]));
    if ((valueArgStart + 2) < extraPositional.size()) releaseMs = static_cast<uint32_t>(std::atoi(extraPositional[valueArgStart + 2]));
    if ((valueArgStart + 3) < extraPositional.size()) threshold = static_cast<float>(std::atof(extraPositional[valueArgStart + 3]));

    // Named flags override everything.
    if (duckDbFromFlag) duckDb = static_cast<float>(std::atof(duckDbFromFlag));
    if (attackFromFlag) attackMs = static_cast<uint32_t>(std::atoi(attackFromFlag));
    if (releaseFromFlag) releaseMs = static_cast<uint32_t>(std::atoi(releaseFromFlag));
    if (thresholdFromFlag) threshold = static_cast<float>(std::atof(thresholdFromFlag));

    ZFork* p = CreateZFork();
    if (!p) return 2;

    BroadcastConfig cfg{};
    std::snprintf(cfg.host, sizeof(cfg.host), "%s", host);
    std::snprintf(cfg.password, sizeof(cfg.password), "%s", password);
    std::snprintf(cfg.mount, sizeof(cfg.mount), "%s", mount);
    std::snprintf(cfg.user, sizeof(cfg.user), "%s", "source");
    std::snprintf(cfg.streamName, sizeof(cfg.streamName), "%s", "libZFork Broadcast Demo");
    cfg.port = static_cast<uint16_t>(port);
    cfg.bitrateKbps = bitrate;
    cfg.sampleRate = 44100;
    cfg.channels = (channels == 1) ? 1 : 2;
    cfg.stereoMode = BroadcastStereoMode::JointStereo;
    if (std::strcmp(mode, "auto") == 0) cfg.stereoMode = BroadcastStereoMode::Auto;
    else if (std::strcmp(mode, "stereo") == 0) cfg.stereoMode = BroadcastStereoMode::Stereo;
    else if (std::strcmp(mode, "mono") == 0) cfg.stereoMode = BroadcastStereoMode::Mono;
    cfg.serverType = BroadcastServerType::Icecast;
    cfg.autoReconnect = true;
    cfg.reconnectDelayMs = 2000;

    if (!p->StartBroadcast(cfg)) {
        std::printf("StartBroadcast failed: %s\n", p->GetBroadcastError());
        DestroyZFork(p);
        return 3;
    }

    const fs::path inputPath(inputPathUtf8);
    bool sourceLoaded = false;
#ifdef _WIN32
    wchar_t cdDrive = 0;
    if (TryParseDriveLetter(std::string(inputPathUtf8), cdDrive)) {
        const auto cdTracks = EnumerateAudioCdTracks(cdDrive);
        if (cdListOnly) {
            if (cdTracks.empty()) {
                std::printf("No audio CD tracks found on drive %c:\n", static_cast<char>(cdDrive));
            } else {
                std::printf("Audio CD tracks on %c::", static_cast<char>(cdDrive));
                for (int t : cdTracks) std::printf(" %02d", t);
                std::printf("\n");
            }
            p->StopBroadcast();
            DestroyZFork(p);
            return cdTracks.empty() ? 5 : 0;
        }
        if (!cdTracks.empty()) {
            char pathBuf[64]{};
            std::snprintf(pathBuf, sizeof(pathBuf), "%c:\\Track%02d.cda", static_cast<char>(cdDrive), cdTracks.front());
            sourceLoaded = p->OpenFileA(pathBuf, StreamFormat::CDAudio);
            for (size_t i = 1; i < cdTracks.size() && sourceLoaded; ++i) {
                std::snprintf(pathBuf, sizeof(pathBuf), "%c:\\Track%02d.cda", static_cast<char>(cdDrive), cdTracks[i]);
                sourceLoaded = p->AddFileA(pathBuf, StreamFormat::CDAudio);
            }
            if (!sourceLoaded) {
                std::printf("Audio-CD queue load failed: %s\n", p->GetError());
            } else {
                std::printf("Loaded %zu audio CD tracks from drive %c:\n", cdTracks.size(), static_cast<char>(cdDrive));
            }
        } else if (!fs::exists(inputPath)) {
            std::printf("No audio CD tracks found on drive %c:\n", static_cast<char>(cdDrive));
        }
    }
#endif
    if (!sourceLoaded)
    if (HasPlaylistExtension(inputPath)) {
        sourceLoaded = p->LoadPlaylistFileA(inputPathUtf8, StreamFormat::Autodetect);
        if (!sourceLoaded) {
            std::printf("LoadPlaylistFileA failed: %s\n", p->GetError());
        }
    } else if (fs::is_directory(inputPath)) {
        std::vector<std::string> files;
        for (const auto& entry : fs::directory_iterator(inputPath)) {
            if (!entry.is_regular_file()) continue;
            if (!HasSupportedAudioExtension(entry.path())) continue;
            files.push_back(entry.path().string());
        }
        std::sort(files.begin(), files.end());
        if (!files.empty()) {
            sourceLoaded = p->OpenFileA(files[0].c_str(), StreamFormat::Autodetect);
            for (size_t i = 1; i < files.size() && sourceLoaded; ++i) {
                sourceLoaded = p->AddFileA(files[i].c_str(), StreamFormat::Autodetect);
            }
        } else {
            std::printf("Directory has no supported audio files.\n");
        }
        if (!sourceLoaded) {
            std::printf("Folder queue load failed: %s\n", p->GetError());
        }
    } else {
        sourceLoaded = p->OpenFileA(inputPathUtf8, StreamFormat::Autodetect);
        if (!sourceLoaded) {
            std::printf("OpenFileA failed: %s\n", p->GetError());
        }
    }
    if (!sourceLoaded) {
        p->StopBroadcast();
        DestroyZFork(p);
        return 4;
    }

    p->AttachBroadcastToCurrentPlayback(true);
    p->SetBroadcastMetadata("Starting...", "libZFork", "");
    BroadcastDuckingConfig duck{};
    duck.enabled = true;
    duck.duckDb = duckDb;
    duck.attackMs = attackMs;
    duck.releaseMs = releaseMs;
    duck.threshold = threshold;
    p->SetBroadcastDucking(duck);
    std::printf("Ducking preset=%s values: duckDb=%.2f attack=%u release=%u threshold=%.4f\n",
                duckPreset, duckDb, attackMs, releaseMs, threshold);
    if (micDevice != -999) {
        p->SetBroadcastMicActive(true);
        if (!p->StartBroadcastMicCapture(micDevice, cfg.sampleRate, 1)) {
            std::printf("StartBroadcastMicCapture failed: %s\n", p->GetBroadcastError());
        } else {
            std::printf("Mic capture active on device %d (ducking enabled)\n", micDevice);
        }
    }
    p->Play();

    std::printf("Broadcast started. Press Ctrl+C to stop.\n");
    for (;;) {
        BroadcastStats st{};
        p->GetBroadcastStats(st);
        std::printf("\rconnected=%d bytes=%llu frames=%llu reconnects=%llu drops=%llu   ",
                    st.connected ? 1 : 0,
                    static_cast<unsigned long long>(st.bytesSent),
                    static_cast<unsigned long long>(st.framesSent),
                    static_cast<unsigned long long>(st.reconnectCount),
                    static_cast<unsigned long long>(st.droppedFrames));
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Unreachable in this demo; keep cleanup for completeness.
    p->Stop();
    p->StopBroadcastMicCapture();
    p->StopBroadcast();
    DestroyZFork(p);
    return 0;
}

