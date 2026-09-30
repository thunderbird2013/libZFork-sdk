/**
 * libZFork2 — DJ Demo (2 Decks + Crossfader + Pitch + Sync/Key-Lock)
 *
 * Usage:
 *   zfork2_dj_demo <deckA> <deckB>
 *
 * Tasten:
 *   Left/Right  Crossfader
 *   Space       Crossfader Mitte
 *   E / C       Deck A Pitch hoch / runter
 *   U / M       Deck B Pitch hoch / runter
 *   S           Sync B -> A (Toggle)
 *   K           Key-Lock Toggle (Deck B)
 *   Q/Esc       Beenden
 */

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <libzfork/libzfork2.h>
#include <libzfork/libzfork.h>
#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <string>
#include <thread>
#include <atomic>

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kPitchMin = -8.f;
static constexpr float kPitchMax =  8.f;
static constexpr float kPitchStep = 0.25f;

static int g_consoleW = 120;
static int g_pitchCol = 8;
static int g_deckW    = 50;
static int g_meterBars = 42;

static HANDLE g_con = nullptr;

static void UpdateLayout() {
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (!g_con || !GetConsoleScreenBufferInfo(g_con, &csbi)) return;

    int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    if (w < 100) w = 100;
    if (w > 200) w = 200;
    g_consoleW = w;

    const int fixed = g_pitchCol * 2 + 1;
    g_deckW = (g_consoleW - fixed) / 2;
    if (g_deckW < 44) g_deckW = 44;

    g_meterBars = g_deckW - 10;
    if (g_meterBars < 24) g_meterBars = 24;
    if (g_meterBars > 80) g_meterBars = 80;
}

struct DeckMeta {
    char title[256]       = {};
    char artist[256]      = {};
    char album[256]       = {};
    char year[16]         = {};
    char fileName[64]     = {};
    int  trackNumber      = 0;
    int  tagBpm           = 0;
    int  sampleRate       = 0;
    int  channels         = 0;
    int  bitrate          = 0;
    uint32_t durationSec  = 0;
};

struct BpmDetectJob {
    std::atomic<bool> doneA{ false };
    std::atomic<bool> doneB{ false };
    float fileBpmA = 0.f;
    float fileBpmB = 0.f;
};

class LiveBpm {
public:
    void Reset(float seed = 0.f) {
        bpm_ = seed;
        lastLevel_ = 0.f;
        lastPeakMs_ = 0;
        intervalCount_ = 0;
        std::fill(std::begin(intervals_), std::end(intervals_), 0.f);
    }

    void Feed(float level) {
        const ULONGLONG now = GetTickCount64();
        level = std::clamp(level, 0.f, 1.f);

        if (level > threshold_ && level > lastLevel_ * 1.08f &&
            (lastPeakMs_ == 0 || now - lastPeakMs_ > 250)) {
            if (lastPeakMs_ > 0) {
                const float dt = static_cast<float>(now - lastPeakMs_) / 1000.f;
                if (dt >= 0.28f && dt <= 1.5f) {
                    float b = 60.f / dt;
                    while (b < 70.f)  b *= 2.f;
                    while (b > 190.f) b *= 0.5f;
                    intervals_[intervalCount_ % kIntervals] = b;
                    ++intervalCount_;

                    float sum = 0.f;
                    int n = std::min(intervalCount_, kIntervals);
                    for (int i = 0; i < n; ++i) sum += intervals_[i];
                    bpm_ = sum / static_cast<float>(n);
                }
            }
            lastPeakMs_ = now;
        }
        lastLevel_ = lastLevel_ * 0.88f + level * 0.12f;
    }

    float Value() const { return bpm_; }

private:
    static constexpr int kIntervals = 8;
    float threshold_ = 0.22f;
    float bpm_ = 0.f;
    float lastLevel_ = 0.f;
    ULONGLONG lastPeakMs_ = 0;
    float intervals_[kIntervals]{};
    int intervalCount_ = 0;
};

static void GotoXY(int x, int y) {
    if (!g_con) return;
    COORD c{ static_cast<SHORT>(x), static_cast<SHORT>(y) };
    SetConsoleCursorPosition(g_con, c);
}

static void SetColor(WORD attr) {
    if (g_con) SetConsoleTextAttribute(g_con, attr);
}

static std::wstring ToWide(const char* s) {
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

static void CopyTrunc(char* dst, size_t n, const char* src) {
    if (!dst || n == 0) return;
    if (!src || !*src) { dst[0] = '\0'; return; }
    std::snprintf(dst, n, "%s", src);
}

static void BaseName(const char* path, char* out, size_t n) {
    if (!path || !out || n == 0) return;
    const char* slash = std::strrchr(path, '\\');
    if (!slash) slash = std::strrchr(path, '/');
    CopyTrunc(out, n, slash ? slash + 1 : path);
}

static void FormatDuration(uint32_t sec, char* out, size_t n) {
    if (!out || n == 0) return;
    std::snprintf(out, n, "%u:%02u", sec / 60, sec % 60);
}

static float PitchToPercent(float semitones) {
    return (std::pow(2.f, semitones / 12.f) - 1.f) * 100.f;
}

static float DetectFileBpm(const wchar_t* path) {
    libZFork::ZFork* probe = libZFork::CreateZFork();
    if (!probe) return 0.f;
    float bpm = 0.f;
    (void)probe->DetectFileBPM(path, libZFork::BPMAlgorithm::Advanced, bpm);
    libZFork::DestroyZFork(probe);
    return bpm;
}

static void DetectBpmThread(BpmDetectJob* job, std::wstring pathA, std::wstring pathB) {
    job->fileBpmA = DetectFileBpm(pathA.c_str());
    job->doneA.store(true, std::memory_order_release);
    job->fileBpmB = DetectFileBpm(pathB.c_str());
    job->doneB.store(true, std::memory_order_release);
}

static bool LoadDeckMeta(const wchar_t* path, const char* argvPath, DeckMeta& meta) {
    BaseName(argvPath, meta.fileName, sizeof meta.fileName);

    libZFork::ZFork* probe = libZFork::CreateZFork();
    if (!probe) return false;

    libZFork::ID3InfoEx tag{};
    (void)probe->LoadFileID3Ex(path, tag);
    CopyTrunc(meta.title,  sizeof meta.title,  tag.title[0]  ? tag.title  : meta.fileName);
    CopyTrunc(meta.artist, sizeof meta.artist, tag.artist);
    CopyTrunc(meta.album,  sizeof meta.album,  tag.album);
    CopyTrunc(meta.year,   sizeof meta.year,   tag.year);
    meta.trackNumber = tag.trackNumber;
    meta.tagBpm      = tag.bpm;

    if (probe->OpenFile(path, libZFork::StreamFormat::Autodetect)) {
        libZFork::StreamInfo info{};
        if (probe->GetStreamInfo(0, info)) {
            meta.sampleRate  = info.samplingRate;
            meta.channels    = info.channelNumber;
            meta.bitrate     = info.bitrate;
            meta.durationSec = info.length.sec;
        }
        probe->Close();
    }

    libZFork::DestroyZFork(probe);
    return true;
}

static void PrintField(int x, int y, int w, const char* label, const char* value) {
    if (!value || !value[0]) value = "-";
    GotoXY(x, y);
    SetColor(0x07);
    std::printf("%-8s %-*.*s", label, w - 9, w - 9, value);
}

static void DrawVerticalPitch(int x, int yTop, int height,
                              float semitones, const char* keys) {
    semitones = std::clamp(semitones, kPitchMin, kPitchMax);
    const int mid = yTop + height / 2;
    const int handleRow = mid - static_cast<int>(semitones / kPitchMax * (height / 2 - 1));

    GotoXY(x, yTop);
    SetColor(0x0E);
    std::printf(" PITCH ");

    GotoXY(x, yTop + 1);
    SetColor(0x08);
    std::printf("  +8   ");

    for (int row = yTop + 2; row < yTop + height - 2; ++row) {
        GotoXY(x, row);
        if (row == mid) {
            SetColor(0x08);
            std::printf(" -0- ");
        } else if (row == handleRow) {
            SetColor(0x0E);
            std::printf("==O==");
        } else {
            SetColor(0x08);
            std::printf("  |   ");
        }
    }

    GotoXY(x, yTop + height - 2);
    SetColor(0x08);
    std::printf("  -8   ");

    GotoXY(x, yTop + height - 1);
    SetColor(0x0B);
    std::printf("%+5.0f%%", PitchToPercent(semitones));

    GotoXY(x, yTop + height);
    SetColor(0x08);
    std::printf(" %s ", keys);
    SetColor(0x07);
}

static void DrawStereoMeter(int x, int y, float left, float right, int bars) {
    left  = std::clamp(left,  0.f, 1.f);
    right = std::clamp(right, 0.f, 1.f);
    int barL = static_cast<int>(left  * bars);
    int barR = static_cast<int>(right * bars);

    GotoXY(x, y);
    SetColor(0x0B);
    std::printf("L[");
    for (int i = 0; i < bars; ++i) {
        SetColor(i < barL ? 0x0A : 0x08);
        std::printf(i < barL ? "#" : "-");
    }
    SetColor(0x0B);
    std::printf("]");

    GotoXY(x, y + 1);
    SetColor(0x0D);
    std::printf("R[");
    for (int i = 0; i < bars; ++i) {
        SetColor(i < barR ? 0x0A : 0x08);
        std::printf(i < barR ? "#" : "-");
    }
    SetColor(0x0D);
    std::printf("]");
    SetColor(0x07);
}

static void DrawDeckPanel(int x, int yStart, int deckW, const char* deckLabel,
                          const DeckMeta& meta, float vol,
                          float meterL, float meterR,
                          float liveBpm, float fileBpm, bool analyzing,
                          bool isMaster, bool showSyncButtons,
                          bool synced, bool keyLock) {
    char dur[16];
    FormatDuration(meta.durationSec, dur, sizeof dur);

    GotoXY(x, yStart);
    SetColor(isMaster ? 0x0E : 0x0F);
    std::printf("%-*s", deckW, deckLabel);

    PrintField(x, yStart + 2, deckW, "Title:",  meta.title);
    PrintField(x, yStart + 3, deckW, "Artist:", meta.artist);
    PrintField(x, yStart + 4, deckW, "Album:",  meta.album);
    PrintField(x, yStart + 5, deckW, "Year:",   meta.year);

    char sr[48];
    if (meta.sampleRate > 0)
        std::snprintf(sr, sizeof sr, "%d Hz / %d ch", meta.sampleRate,
                      meta.channels > 0 ? meta.channels : 2);
    else
        std::snprintf(sr, sizeof sr, "-");
    PrintField(x, yStart + 7, deckW, "Audio:", sr);

    char br[32];
    if (meta.bitrate > 0) std::snprintf(br, sizeof br, "%d kbps", meta.bitrate / 1000);
    else                  std::snprintf(br, sizeof br, "-");
    PrintField(x, yStart + 8, deckW, "Bitrate:", br);

    PrintField(x, yStart + 9, deckW, "Length:", dur);
    PrintField(x, yStart + 10, deckW, "File:", meta.fileName);

    GotoXY(x, yStart + 12);
    SetColor(0x0B);
    if (analyzing) std::printf("BPM:     Analyse...");
    else if (liveBpm > 0.f) std::printf("BPM:     %6.1f live", liveBpm);
    else std::printf("BPM:     ----");

    GotoXY(x, yStart + 13);
    SetColor(0x08);
    if (meta.tagBpm > 0 && fileBpm > 0.f)
        std::printf("Tag/File: %d / %.0f", meta.tagBpm, fileBpm);
    else if (meta.tagBpm > 0)
        std::printf("Tag:      %d", meta.tagBpm);
    else if (fileBpm > 0.f)
        std::printf("Detect:   %.0f", fileBpm);
    else
        std::printf("Tag/File: -");

    DrawStereoMeter(x, yStart + 15, meterL, meterR, g_meterBars);

    GotoXY(x, yStart + 17);
    SetColor(0x0E);
    std::printf("Vol: %3.0f%%", vol * 100.f);

    if (showSyncButtons) {
        GotoXY(x, yStart + 18);
        if (synced) { SetColor(0x0A); std::printf("[ SYNC ]"); }
        else        { SetColor(0x08); std::printf("[ sync ]"); }
        SetColor(0x07);
        std::printf(" S    ");
        if (keyLock) { SetColor(0x0A); std::printf("[ KEY ]"); }
        else         { SetColor(0x08); std::printf("[ key ]"); }
        SetColor(0x07);
        std::printf(" K");
    }
    SetColor(0x07);
}

static void ApplyCrossfade(ZF2_CONTEXT ctx, ZF2_GROUP deckA, ZF2_GROUP deckB, float t) {
    t = std::clamp(t, 0.f, 1.f);
    const float angle = t * (kPi * 0.5f);
    zfork2_SetGroupVolume(ctx, deckA, std::cos(angle));
    zfork2_SetGroupVolume(ctx, deckB, std::sin(angle));
}

static float EffectiveBpm(float live, float file, int tag) {
    if (file > 0.f) return file;
    if (tag > 0)    return static_cast<float>(tag);
    if (live > 0.f) return live;
    return 0.f;
}

static void ApplyDeckVoice(ZF2_CONTEXT ctx, ZF2_VOICE voice,
                           float pitchSemi, float syncBaseTempo,
                           bool synced, bool keyLock) {
    if (voice == ZF2_INVALID_VOICE) return;

    const float ratio = std::pow(2.f, pitchSemi / 12.f);
    float tempo = ratio;
    float pitch = 0.f;

    if (synced) {
        tempo = syncBaseTempo * ratio;
        pitch = keyLock ? 0.f : pitchSemi;
    } else {
        tempo = ratio;
        pitch = keyLock ? 0.f : pitchSemi;
    }

    tempo = std::clamp(tempo, 0.5f, 2.0f);
    pitch = std::clamp(pitch, kPitchMin, kPitchMax);

    zfork2_SetVoiceTempo(ctx, voice, tempo);
    zfork2_SetVoicePitch(ctx, voice, pitch);
}

static bool ToggleSyncB(ZF2_CONTEXT ctx, ZF2_VOICE voiceB,
                        float pitchB, bool keyLockB,
                        float bpmA, float bpmB,
                        bool currentlySynced, float& syncBaseTempo) {
    if (currentlySynced) {
        syncBaseTempo = 1.f;
        ApplyDeckVoice(ctx, voiceB, pitchB, 1.f, false, keyLockB);
        return false;
    }

    if (bpmA <= 0.f || bpmB <= 0.f) return false;

    syncBaseTempo = std::clamp(bpmA / bpmB, 0.85f, 1.15f);
    ApplyDeckVoice(ctx, voiceB, pitchB, syncBaseTempo, true, keyLockB);
    return true;
}

static void DrawCrossfader(int y, float crossfade, float volA, float volB,
                           float masterBpm, bool analyzing) {
    const int barW = std::max(60, g_consoleW - 24);
    const int x0   = (g_consoleW - barW) / 2;

    GotoXY(0, y);
    SetColor(0x08);
    for (int i = 0; i < g_consoleW; ++i) std::printf("=");
    SetColor(0x07);

    GotoXY(x0, y + 1);
    SetColor(0x0F);
    std::printf("CROSSFADER");
    SetColor(0x08);
    std::printf("     Deck A <<");
    SetColor(0x0E);
    std::printf("-------|-------");
    SetColor(0x08);
    std::printf(">> Deck B");

    GotoXY(x0, y + 3);
    std::printf("[");
    int pos = static_cast<int>(crossfade * (barW - 1) + 0.5f);
    for (int i = 0; i < barW; ++i) {
        if (i == pos)           { SetColor(0x0E); std::printf("|"); }
        else if (i == barW / 2) { SetColor(0x08); std::printf("+"); }
        else                    { SetColor(0x08); std::printf("-"); }
    }
    SetColor(0x07);
    std::printf("]");

    char mix[48];
    std::snprintf(mix, sizeof mix, "%3.0f%% A  /  %3.0f%% B", volA * 100.f, volB * 100.f);
    GotoXY(x0 + (barW - static_cast<int>(std::strlen(mix))) / 2, y + 4);
    SetColor(0x0B);
    std::printf("%s", mix);

    GotoXY(x0 + barW / 2 - 8, y + 6);
    SetColor(0x0E);
    if (analyzing) std::printf("Master BPM: ...");
    else if (masterBpm > 0.f) std::printf("Master BPM: %.1f", masterBpm);
    else std::printf("Master BPM: --");

    GotoXY(2, y + 8);
    SetColor(0x08);
    std::printf("E/C = Deck A Pitch    U/M = Deck B Pitch    S = Sync Toggle    K = Key-Lock    Space = Crossfader Mitte    Q = Ende");
    SetColor(0x07);
}

static void DrawUI(const DeckMeta& metaA, const DeckMeta& metaB,
                   float crossfade, float volA, float volB,
                   float meterAL, float meterAR, float meterBL, float meterBR,
                   float liveA, float liveB, float fileA, float fileB,
                   bool analyzingA, bool analyzingB,
                   float pitchA, float pitchB,
                   bool synced, bool keyLockB) {
    UpdateLayout();

    const int xA      = g_pitchCol;
    const int sepX    = g_pitchCol + g_deckW;
    const int xB      = sepX + 1;
    const int xPitchB = xB + g_deckW;
    const float masterBpm = EffectiveBpm(liveA, fileA, metaA.tagBpm);

    GotoXY(0, 0);
    SetColor(0x0F);
    const char* title = " libZFork2 DJ Demo ";
    int pad = (g_consoleW - static_cast<int>(std::strlen(title))) / 2;
    if (pad < 0) pad = 0;
    for (int i = 0; i < pad; ++i) std::printf(" ");
    std::printf("%s", title);
    SetColor(0x08);
    for (int i = pad + static_cast<int>(std::strlen(title)); i < g_consoleW; ++i)
        std::printf("=");

    for (int row = 1; row <= 19; ++row) {
        GotoXY(sepX, row);
        SetColor(0x08);
        std::printf("|");
    }

    DrawVerticalPitch(0, 2, 13, pitchA, "E/C");
    DrawDeckPanel(xA, 1, g_deckW, "DECK A", metaA, volA, meterAL, meterAR,
                  liveA, fileA, analyzingA, true, false, false, false);
    DrawDeckPanel(xB, 1, g_deckW, "DECK B", metaB, volB, meterBL, meterBR,
                  liveB, fileB, analyzingB, false, true, synced, keyLockB);
    DrawVerticalPitch(xPitchB, 2, 13, pitchB, "U/M");

    DrawCrossfader(21, crossfade, volA, volB, masterBpm, analyzingA || analyzingB);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: zfork2_dj_demo <deckA> <deckB>\n");
        return 1;
    }

    g_con = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO ci{ 1, FALSE };
    SetConsoleCursorInfo(g_con, &ci);
    UpdateLayout();
    SetConsoleScreenBufferSize(g_con, { static_cast<SHORT>(g_consoleW + 2), 34 });

    const std::wstring pathA = ToWide(argv[1]);
    const std::wstring pathB = ToWide(argv[2]);

    DeckMeta metaA{}, metaB{};
    LoadDeckMeta(pathA.c_str(), argv[1], metaA);
    LoadDeckMeta(pathB.c_str(), argv[2], metaB);

    BpmDetectJob bpmJob{};
    std::thread bpmThread(DetectBpmThread, &bpmJob, pathA, pathB);

    LiveBpm liveA{}, liveB{};
    liveA.Reset(metaA.tagBpm > 0 ? static_cast<float>(metaA.tagBpm) : 0.f);
    liveB.Reset(metaB.tagBpm > 0 ? static_cast<float>(metaB.tagBpm) : 0.f);

    ZF2_CONTEXT ctx = zfork2_CreateContext();
    if (!ctx) {
        bpmThread.join();
        fprintf(stderr, "CreateContext fehlgeschlagen\n");
        return 1;
    }

    ZF2_GROUP deckA = zfork2_RegisterGroup(ctx, "deckA");
    ZF2_GROUP deckB = zfork2_RegisterGroup(ctx, "deckB");
    if (deckA == ZF2_INVALID_GROUP || deckB == ZF2_INVALID_GROUP) {
        bpmThread.join();
        zfork2_DestroyContext(ctx);
        return 1;
    }

    if (!zfork2_Start(ctx)) {
        bpmThread.join();
        zfork2_DestroyContext(ctx);
        return 1;
    }

    ZF2_SOUND sndA = zfork2_LoadSound(ctx, pathA.c_str(), ZF2_SOUND_STREAMING);
    ZF2_SOUND sndB = zfork2_LoadSound(ctx, pathB.c_str(), ZF2_SOUND_STREAMING);
    if (!sndA || !sndB) {
        bpmThread.join();
        if (sndA) zfork2_UnloadSound(sndA);
        if (sndB) zfork2_UnloadSound(sndB);
        zfork2_Stop(ctx);
        zfork2_DestroyContext(ctx);
        return 1;
    }

    ZF2_VOICE voiceA = zfork2_PlaySound(ctx, sndA, deckA);
    ZF2_VOICE voiceB = zfork2_PlaySound(ctx, sndB, deckB);
    if (voiceA == ZF2_INVALID_VOICE || voiceB == ZF2_INVALID_VOICE) {
        bpmThread.join();
        zfork2_UnloadSound(sndA);
        zfork2_UnloadSound(sndB);
        zfork2_Stop(ctx);
        zfork2_DestroyContext(ctx);
        return 1;
    }

    zfork2_SetVoiceLoop(ctx, voiceA, -1);
    zfork2_SetVoiceLoop(ctx, voiceB, -1);

    float crossfade = 0.5f;
    float pitchA = 0.f;
    float pitchB = 0.f;
    float syncBaseTempo = 1.f;
    bool synced = false;
    bool keyLockB = true;

    ApplyCrossfade(ctx, deckA, deckB, crossfade);
    ApplyDeckVoice(ctx, voiceA, pitchA, 1.f, false, false);
    ApplyDeckVoice(ctx, voiceB, pitchB, 1.f, false, keyLockB);

    system("cls");

    while (true) {
        zfork2_Update(ctx);

        if (_kbhit()) {
            int ch = _getch();
            bool changed = false;

            if (ch == 0 || ch == 224) {
                int ext = _getch();
                if (ext == 75) crossfade -= 0.05f;
                else if (ext == 77) crossfade += 0.05f;
                crossfade = std::clamp(crossfade, 0.f, 1.f);
                ApplyCrossfade(ctx, deckA, deckB, crossfade);
            } else if (ch == ' ') {
                crossfade = 0.5f;
                ApplyCrossfade(ctx, deckA, deckB, crossfade);
            } else if (ch == 'e' || ch == 'E') {
                pitchA = std::clamp(pitchA + kPitchStep, kPitchMin, kPitchMax);
                changed = true;
            } else if (ch == 'c' || ch == 'C') {
                pitchA = std::clamp(pitchA - kPitchStep, kPitchMin, kPitchMax);
                changed = true;
            } else if (ch == 'u' || ch == 'U') {
                pitchB = std::clamp(pitchB + kPitchStep, kPitchMin, kPitchMax);
                changed = true;
            } else if (ch == 'm' || ch == 'M') {
                pitchB = std::clamp(pitchB - kPitchStep, kPitchMin, kPitchMax);
                changed = true;
            } else if (ch == 's' || ch == 'S') {
                const float bpmA = EffectiveBpm(liveA.Value(), bpmJob.fileBpmA, metaA.tagBpm);
                const float bpmB = EffectiveBpm(liveB.Value(), bpmJob.fileBpmB, metaB.tagBpm);
                synced = ToggleSyncB(ctx, voiceB, pitchB, keyLockB,
                                     bpmA, bpmB, synced, syncBaseTempo);
            } else if (ch == 'k' || ch == 'K') {
                keyLockB = !keyLockB;
                ApplyDeckVoice(ctx, voiceB, pitchB, syncBaseTempo, synced, keyLockB);
            } else if (ch == 'q' || ch == 'Q' || ch == 27) {
                break;
            }

            if (changed) {
                ApplyDeckVoice(ctx, voiceA, pitchA, 1.f, false, false);
                ApplyDeckVoice(ctx, voiceB, pitchB, syncBaseTempo, synced, keyLockB);
            }
        }

        float meterAL = 0.f, meterAR = 0.f, meterBL = 0.f, meterBR = 0.f;
        zfork2_GetGroupLevels(ctx, deckA, &meterAL, &meterAR);
        zfork2_GetGroupLevels(ctx, deckB, &meterBL, &meterBR);

        liveA.Feed(std::max(meterAL, meterAR));
        liveB.Feed(std::max(meterBL, meterBR));

        const float volA = std::cos(crossfade * kPi * 0.5f);
        const float volB = std::sin(crossfade * kPi * 0.5f);
        const bool analyzingA = !bpmJob.doneA.load(std::memory_order_acquire);
        const bool analyzingB = !bpmJob.doneB.load(std::memory_order_acquire);

        DrawUI(metaA, metaB, crossfade, volA, volB,
               meterAL, meterAR, meterBL, meterBR,
               liveA.Value(), liveB.Value(),
               bpmJob.fileBpmA, bpmJob.fileBpmB,
               analyzingA, analyzingB,
               pitchA, pitchB, synced, keyLockB);
        Sleep(16);
    }

    bpmThread.join();
    ci.bVisible = TRUE;
    SetConsoleCursorInfo(g_con, &ci);

    zfork2_UnloadSound(sndA);
    zfork2_UnloadSound(sndB);
    zfork2_Stop(ctx);
    zfork2_DestroyContext(ctx);

    GotoXY(0, 31);
    SetColor(0x07);
    std::printf("Fertig.\n");
    return 0;
}
