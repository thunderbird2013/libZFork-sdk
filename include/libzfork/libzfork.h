#pragma once
/**
 * libZFork — Modern C++20 audio playback and processing library
 * Based on libZPlay 2.02 by Zoran Cindori (GPLv2)
 * Modernised: C++20, vcpkg dependencies, clean CMake build
 */

#include <cstdint>
#include <cstddef>
#include <string_view>

// ── Export macros ──────────────────────────────────────────────────────────────
#if defined(_WIN32)
#  if defined(LIBZFORK_STATIC)
#    define ZFORK_API
#  elif defined(LIBZFORK_BUILDING_DLL)
#    define ZFORK_API __declspec(dllexport)
#  else
#    define ZFORK_API __declspec(dllimport)
#  endif
#  define ZFORK_CALL __stdcall
#else
#  if defined(LIBZFORK_BUILDING_DLL)
#    define ZFORK_API __attribute__((visibility("default")))
#  else
#    define ZFORK_API
#  endif
#  define ZFORK_CALL
#endif

// ── Version (single source of truth: VERSION → libzfork_version.h) ─────────────
#include "libzfork_version.h"

namespace libZFork {

// ── Enumerations ───────────────────────────────────────────────────────────────

enum class StreamFormat : int {
    Unknown    = 0,
    Mp3        = 1,
    Ogg        = 2,
    Wav        = 3,
    PCM        = 4,
    FLAC       = 5,
    FLACOgg    = 6,
    LegacyReserved7 = 7, // reserved to keep ABI-stable enum values
    AacADTS    = 8,
    WaveIn     = 9,
    Autodetect = 10,
    Aiff       = 11,
    Opus       = 12,
    CDAudio    = 13,
    M4a        = 14,
};

enum class PlayerStatus : int {
    Stopped = 0,
    Playing = 1,
    Paused  = 2,
};

enum class SeekOrigin : int {
    Begin   = 0,
    Current = 1,
    End     = 2,
};

enum class TimeUnit : int {
    Second  = 0,
    Millisecond = 1,
    Sample  = 2,
};

enum class ChannelMode : int {
    Stereo = 0,
    Mono   = 1,
    Left   = 2,
    Right  = 3,
};

enum class FFTWindow : int {
    Rectangular       = 0,
    Hamming           = 1,
    Hann              = 2,
    Cosine            = 3,
    Lanczos           = 4,
    Bartlett          = 5,
    Triangular        = 6,
    Gauss             = 7,
    BartlettHann      = 8,
    Blackman          = 9,
    Nuttall           = 10,
    BlackmanHarris    = 11,
    BlackmanNuttall   = 12,
    FlatTop           = 13,
};

enum class FFTGraphType : int {
    LinesLeftOnTop  = 0,
    LinesRightOnTop = 1,
    AreaLeftOnTop   = 2,
    AreaRightOnTop  = 3,
    BarsLeftOnTop   = 4,
    BarsRightOnTop  = 5,
    Spectrum        = 6,
};

enum class CallbackMessage : int {
    StopAsync               = 0,
    PlayAsync               = 1,
    EnterLoopAsync          = 2,
    ExitLoopAsync           = 3,
    EnterVolumeSlideAsync   = 4,
    ExitVolumeSlideAsync    = 5,
    StreamBufferDoneAsync   = 6,
    StreamNeedMoreDataAsync = 7,
    NextSongAsync           = 8,
    PauseAsync              = 9,
    ResumeAsync             = 10,
    // param1 = current position ms, param2 = total length ms
    PositionAsync           = 11,
    QueueItemAddedAsync     = 12, // param1 = index, param2 = queue size
    QueueDuplicateRejectedAsync = 13, // param1 = attempted index, param2 = queue size
    QueueChangedAsync       = 14, // param1 = queue size, param2 = reserved
    BroadcastConnectedAsync = 15, // param1 = server type, param2 = reserved
    BroadcastReconnectingAsync = 16, // param1 = retry count, param2 = delay ms
    BroadcastDisconnectedAsync = 17, // param1 = reason code, param2 = reserved
    BroadcastUnderrunAsync  = 18, // param1 = dropped frames low32, param2 = reserved
    BroadcastAuthFailedAsync = 19, // param1 = reserved, param2 = reserved
    Stop                    = 100,
    Play                    = 101,
    EnterLoop               = 102,
    ExitLoop                = 103,
    EnterVolumeSlide        = 104,
    ExitVolumeSlide         = 105,
    StreamBufferDone        = 106,
    StreamNeedMoreData      = 107,
    NextSong                = 108,
    Pause                   = 109,
    Resume                  = 110,
};

enum class BPMAlgorithm : int {
    Peaks          = 0,
    AutoCorrelation = 1,
    Advanced       = 2,
};

// ── Structures ─────────────────────────────────────────────────────────────────

struct StreamHMSTime {
    uint32_t hour;
    uint32_t minute;
    uint32_t second;
    uint32_t millisecond;
};

struct StreamTime {
    uint32_t    sec;
    uint32_t    ms;
    uint64_t    samples;
    StreamHMSTime hms;
};

struct StreamInfo {
    int      samplingRate;
    int      channelNumber;
    int      bitrate;
    bool     vbr;
    StreamTime length;
};

struct StreamStatus {
    bool         playing;
    bool         paused;
    bool         echoEnabled;
    bool         equalizerEnabled;
    bool         vocalCutEnabled;
    bool         channelMixEnabled;
    bool         slidingVolume;
    int          loopCount;
    int          songIndex;
    int          songsInQueue;
};

struct EchoParams {
    int leftDelay;       // ms
    int leftSourceVol;   // 0–100
    int leftEchoVol;     // 0–100
    int rightDelay;      // ms
    int rightSourceVol;  // 0–100
    int rightEchoVol;    // 0–100
};

struct VUData {
    float left;   // 0.0–1.0
    float right;  // 0.0–1.0
};

struct WaveDeviceInfo {
    uint32_t id;
    char     name[64];
    uint32_t channels;
    uint32_t formats;
};

// ID3 tag structures
struct ID3Info {
    char title[256];
    char artist[256];
    char album[256];
    char year[16];
    char comment[512];
    int  trackNumber;
    int  genre;
};

struct ID3InfoEx {
    char title[256];
    char artist[256];
    char album[256];
    char year[16];
    char comment[512];
    int  trackNumber;
    int  genre;
    char albumArtist[256];
    char composer[256];
    char copyright[256];
    char url[512];
    char publisher[256];
    int  bpm;
    char key[32];   // Musical key / Camelot from ID3 TKEY (or comment fallback), e.g. "Am", "8A"
};

// Parsed musical key for DJ harmonic mixing (Camelot / Open Key / classical).
struct MusicalKeyInfo {
    char raw[32];       // input as given
    char musical[16];   // e.g. "Am", "C#", "F"
    char camelot[8];    // e.g. "8A", "1B"
    char openKey[8];    // e.g. "1m", "6d"
    int  wheel;         // 1..12, 0 = unknown
    int  isMinor;       // 1 = minor (Camelot A), 0 = major (B), -1 = unknown
};

/** Parse "Am", "C#m", "8A", "1m", … into MusicalKeyInfo. */
ZFORK_API bool ZFORK_CALL ParseMusicalKey(const char* text, MusicalKeyInfo& out);

/** Classic Camelot compatibility: same key, relative major/minor, or ±1 on the wheel. */
ZFORK_API bool ZFORK_CALL KeysHarmonicallyCompatible(const MusicalKeyInfo& a, const MusicalKeyInfo& b);

struct ID3Picture {
    uint8_t*    data;
    uint32_t    size;
    int         type;     // 0=JPEG, 1=PNG, 2=BMP
    char        description[256];
};

// PCM stream descriptor (for raw PCM input)
struct PCMFormat {
    int sampleRate;
    int channels;
    int bitsPerSample;   // 8, 16, 24, 32
    bool isFloat;
};

enum class BroadcastServerType : int {
    Icecast = 0,
    Shoutcast = 1,
};

enum class BroadcastStereoMode : int {
    Auto = 0,
    Stereo = 1,
    JointStereo = 2,
    Mono = 3,
};

struct BroadcastConfig {
    char     host[256];
    uint16_t port;
    char     mount[128];
    char     user[64];
    char     password[128];
    char     streamName[128];
    int      bitrateKbps;
    int      sampleRate;
    int      channels;
    BroadcastStereoMode stereoMode;
    BroadcastServerType serverType;
    bool     autoReconnect;
    uint32_t reconnectDelayMs;
};

struct BroadcastStats {
    uint64_t bytesSent;
    uint64_t framesSent;
    uint64_t reconnectCount;
    uint64_t droppedFrames;
    bool     connected;
};

struct BroadcastDuckingConfig {
    bool     enabled;
    float    duckDb;       // negative dB while mic is active, e.g. -10.0f
    uint32_t attackMs;     // fade-down time
    uint32_t releaseMs;    // fade-up time
    float    threshold;    // mic activity threshold (0.0 .. 1.0)
};

// Values for ZForkConfig::outputBackend.
#define ZFORK_OUTPUT_AUTO    0  // try WASAPI-Exclusive, fall back to WaveOut
#define ZFORK_OUTPUT_WASAPI  1  // force WASAPI-Exclusive; fails instead of falling back
#define ZFORK_OUTPUT_WAVEOUT 2  // force classic WaveOut/MME

// Optional startup configuration for CreateZForkWithConfig / zfork_CreateWithConfig.
// Use 0 / -1 to keep library defaults.
struct ZForkConfig {
    uint32_t structSize;       // set to sizeof(ZForkConfig)
    int      waveOutDeviceId;  // -1 = default device
    uint32_t bufferMs;         // 0 = default (currently 500 ms)
    int      masterVolumeLeft; // -1 = default (100)
    int      masterVolumeRight;// -1 = default (100)
    int      playerVolumeLeft; // -1 = default (100)
    int      playerVolumeRight;// -1 = default (100)
    int      monoMix;          // -1 = unchanged, 0 = stereo, 1 = mono
    // Appended field — guarded by structSize, so older callers that built a
    // smaller ZForkConfig are unaffected.
    int      outputBackend;    // -1 = default (Auto), 0 = Auto, 1 = WASAPI-Exclusive
                                // (fails instead of silently falling back), 2 = WaveOut/MME
};

// Analyze flags for AnalyzeFile()
#define ZFORK_ANALYZE_BPM      0x00000001u
#define ZFORK_ANALYZE_WAVEFORM 0x00000002u
#define ZFORK_ANALYZE_PEAKS    0x00000004u
#define ZFORK_ANALYZE_LENGTH   0x00000008u
#define ZFORK_ANALYZE_ALL      (ZFORK_ANALYZE_BPM | ZFORK_ANALYZE_WAVEFORM | ZFORK_ANALYZE_PEAKS | ZFORK_ANALYZE_LENGTH)

// Encoder callback types
using EncoderReadCallback  = uint32_t (*)(void* data, uint32_t size, void* userData);
using EncoderWriteCallback = uint32_t (*)(void* data, uint32_t size, void* userData);
using EncoderSeekCallback  = uint32_t (*)(uint32_t offset, void* userData);
using EncoderTellCallback  = uint32_t (*)(void* userData);

// Player event callback
using PlayerCallback = int (ZFORK_CALL *)(
    void*           instance,
    void*           userData,
    CallbackMessage message,
    uint32_t        param1,
    uint32_t        param2
);

// PCM output callback (post-DSP, interleaved float samples).
using PCMOutputCallback = void (ZFORK_CALL *)(
    void*           instance,
    void*           userData,
    const float*    interleaved,
    uint32_t        frames,
    int             channels,
    int             sampleRate
);

// ── ZFork main interface ───────────────────────────────────────────────────────

class ZFork {
public:
    virtual ~ZFork() = default;

    // ── Open / Close ─────────────────────────────────────────────────────────
    [[nodiscard]] virtual bool OpenFile(const wchar_t* path, StreamFormat format) = 0;
    [[nodiscard]] virtual bool OpenFileA(const char*    path, StreamFormat format) = 0;
    [[nodiscard]] virtual bool LoadPlaylistFile(const wchar_t* path, StreamFormat fallbackFormat) = 0;
    [[nodiscard]] virtual bool LoadPlaylistFileA(const char* path, StreamFormat fallbackFormat) = 0;
    [[nodiscard]] virtual bool AddFile(const wchar_t*  path, StreamFormat format) = 0;
    [[nodiscard]] virtual bool AddFileA(const char*    path, StreamFormat format) = 0;
    [[nodiscard]] virtual bool RemoveQueueItem(uint32_t index) = 0;
    [[nodiscard]] virtual bool MoveQueueItem(uint32_t fromIndex, uint32_t toIndex) = 0;
    [[nodiscard]] virtual uint32_t GetQueueCount() const = 0;
    /** UTF-8 path of queue item; returns false if index invalid or buffer too small. */
    [[nodiscard]] virtual bool GetQueueItemPathA(uint32_t index, char* out, uint32_t outBytes) const = 0;
    [[nodiscard]] virtual bool OpenStream(const void* data, uint32_t size, StreamFormat format) = 0;
    [[nodiscard]] virtual bool OpenPCMStream(PCMFormat fmt) = 0;
    virtual void               Close() = 0;

    // ── Playback control ──────────────────────────────────────────────────────
    [[nodiscard]] virtual bool Play() = 0;
    [[nodiscard]] virtual bool PlayLoop(uint32_t loopCount) = 0;
    [[nodiscard]] virtual bool Pause() = 0;
    [[nodiscard]] virtual bool Resume() = 0;
    [[nodiscard]] virtual bool Stop() = 0;

    // ── Position ─────────────────────────────────────────────────────────────
    [[nodiscard]] virtual bool Seek(SeekOrigin origin, const StreamTime& time) = 0;
    [[nodiscard]] virtual bool GetPosition(StreamTime& outTime) const = 0;

    // ── Info ─────────────────────────────────────────────────────────────────
    [[nodiscard]] virtual bool         GetStreamInfo(int songIndex, StreamInfo& outInfo) const = 0;
    [[nodiscard]] virtual StreamStatus GetStatus() const = 0;
    [[nodiscard]] virtual const char*  GetError() const = 0;

    // ── Volume ────────────────────────────────────────────────────────────────
    [[nodiscard]] virtual bool SetMasterVolume(int left, int right) = 0;
    [[nodiscard]] virtual bool GetMasterVolume(int& outLeft, int& outRight) const = 0;
    [[nodiscard]] virtual bool SetPlayerVolume(int left, int right) = 0;
    [[nodiscard]] virtual bool GetPlayerVolume(int& outLeft, int& outRight) const = 0;
    [[nodiscard]] virtual bool SlideVolume(int fromLeft, int fromRight, int toLeft, int toRight, uint32_t durationMs) = 0;
    [[nodiscard]] virtual bool GetVUData(VUData& outVU) const = 0;
    [[nodiscard]] virtual bool MixChannels(bool mono) = 0;

    // ── Speed / Pitch / Tempo ─────────────────────────────────────────────────
    [[nodiscard]] virtual bool SetRate(float rate)   = 0;   // 0.5 – 2.0
    [[nodiscard]] virtual bool SetPitch(float semitones) = 0;
    [[nodiscard]] virtual bool SetTempo(float tempo) = 0;   // 0.5 – 2.0

    // ── Equalizer ─────────────────────────────────────────────────────────────
    [[nodiscard]] virtual bool EnableEqualizer(bool enable) = 0;
    [[nodiscard]] virtual bool SetEqualizerBandGain(int band, float gainDb) = 0;   // band 0–30
    [[nodiscard]] virtual bool GetEqualizerBandGain(int band, float& gainDb) const = 0;
    [[nodiscard]] virtual bool ResetEqualizer() = 0;

    // ── Echo ──────────────────────────────────────────────────────────────────
    [[nodiscard]] virtual bool EnableEcho(bool enable) = 0;
    [[nodiscard]] virtual bool SetEchoParams(const EchoParams& params) = 0;
    [[nodiscard]] virtual bool GetEchoParams(EchoParams& outParams) const = 0;

    // ── Vocal / Stereo ────────────────────────────────────────────────────────
    [[nodiscard]] virtual bool EnableVocalCut(bool enable) = 0;
    [[nodiscard]] virtual bool EnableSideCut(bool enable) = 0;
    [[nodiscard]] virtual bool EnableStereoEnhancer(bool enable) = 0;
    [[nodiscard]] virtual bool SetStereoWidth(float width) = 0;   // 0.0 – 2.0, 1.0 = unchanged
    [[nodiscard]] virtual bool GetStereoWidth(float& outWidth) const = 0;

    // ── FFT / Spectrum ────────────────────────────────────────────────────────
    [[nodiscard]] virtual bool SetFFTPoints(uint32_t points) = 0;   // must be power of 2
    [[nodiscard]] virtual bool SetFFTWindow(FFTWindow window) = 0;
    [[nodiscard]] virtual bool GetFFTData(float* outLeft, float* outRight, uint32_t points) const = 0;
    [[nodiscard]] virtual bool DrawFFTGraph(void* hdc, int x, int y, int width, int height) const = 0;
    [[nodiscard]] virtual bool SetFFTGraphType(FFTGraphType type) = 0;
    [[nodiscard]] virtual bool SetFFTGraphColor(int index, uint32_t colorRGB) = 0;

    // ── BPM detection ─────────────────────────────────────────────────────────
    [[nodiscard]] virtual bool DetectBPM(BPMAlgorithm algo, float& outBPM) = 0;
    [[nodiscard]] virtual bool DetectFileBPM(const wchar_t* path, BPMAlgorithm algo, float& outBPM) = 0;
    [[nodiscard]] virtual bool AnalyzeFile(const wchar_t* path, uint32_t flags, void** outHandle) = 0;
    virtual void               FreeAnalysisHandle(void* handle) = 0;
    [[nodiscard]] virtual bool GetAnalysisSummary(void* handle, float& outBpm, uint32_t& outLengthMs, uint32_t& outFlags) const = 0;
    [[nodiscard]] virtual uint32_t CopyAnalysisWaveform(void* handle, uint8_t* outBins, uint32_t maxBins) const = 0;
    [[nodiscard]] virtual uint32_t CopyAnalysisPeaks(void* handle, float* outTimesMs, float* outValues, uint32_t maxPeaks) const = 0;

    // ── ID3 Tags ──────────────────────────────────────────────────────────────
    [[nodiscard]] virtual bool LoadID3(int songIndex, ID3Info& outInfo) const = 0;
    [[nodiscard]] virtual bool LoadID3Ex(int songIndex, ID3InfoEx& outInfo) const = 0;
    [[nodiscard]] virtual bool LoadFileID3(const wchar_t* path, ID3Info& outInfo) const = 0;
    [[nodiscard]] virtual bool LoadFileID3Ex(const wchar_t* path, ID3InfoEx& outInfo) const = 0;
    [[nodiscard]] virtual bool GetID3Picture(int songIndex, int picIndex, ID3Picture& outPic) const = 0;

    // ── Wave output device ────────────────────────────────────────────────────
    [[nodiscard]] virtual int  EnumerateWaveOutDevices() const = 0;
    [[nodiscard]] virtual bool GetWaveOutDeviceInfo(int deviceId, WaveDeviceInfo& outInfo) const = 0;
    [[nodiscard]] virtual bool SetWaveOutDevice(int deviceId) = 0;
    [[nodiscard]] virtual bool SetWaveOutFile(const wchar_t* path) = 0;

    // ── Wave input device ─────────────────────────────────────────────────────
    [[nodiscard]] virtual int  EnumerateWaveInDevices() const = 0;
    [[nodiscard]] virtual bool GetWaveInDeviceInfo(int deviceId, WaveDeviceInfo& outInfo) const = 0;
    [[nodiscard]] virtual bool SetWaveInDevice(int deviceId) = 0;

    // ── Stream encoding ───────────────────────────────────────────────────────
    [[nodiscard]] virtual bool StartEncoding(
        StreamFormat            format,
        const wchar_t*          outputPath,
        EncoderWriteCallback    writeCallback,
        void*                   userData
    ) = 0;
    [[nodiscard]] virtual bool StopEncoding() = 0;

    // ── PCM push (for streaming) ──────────────────────────────────────────────
    [[nodiscard]] virtual bool PushPCMData(const void* data, uint32_t byteCount) = 0;

    // ── Decode-only (for transcoding / push pipelines) ────────────────────────
    // Decodes up to maxFrames interleaved float samples into outBuf.
    // Returns false on EOF or error; sets decodedFrames = 0 on EOF.
    [[nodiscard]] virtual bool DecodeNextChunk(float* outBuf, uint32_t maxFrames,
                                               uint32_t& decodedFrames) = 0;

    // ── Broadcast (Icecast/Shoutcast MP3) ─────────────────────────────────────
    [[nodiscard]] virtual bool StartBroadcast(const BroadcastConfig& config) = 0;
    [[nodiscard]] virtual bool StopBroadcast() = 0;
    [[nodiscard]] virtual bool PushBroadcastPCM(const float* interleaved, uint32_t frames,
                                                int channels, int sampleRate) = 0;
    [[nodiscard]] virtual bool StartBroadcastFile(const wchar_t* path, StreamFormat inputFormat) = 0;
    [[nodiscard]] virtual bool AttachBroadcastToCurrentPlayback(bool enable) = 0;
    [[nodiscard]] virtual bool SetBroadcastMetadata(const char* title, const char* artist, const char* album) = 0;
    [[nodiscard]] virtual bool PushBroadcastMicPCM(const float* interleaved, uint32_t frames,
                                                   int channels, int sampleRate) = 0;
    [[nodiscard]] virtual bool SetBroadcastMicActive(bool active) = 0;
    [[nodiscard]] virtual bool SetBroadcastDucking(const BroadcastDuckingConfig& config) = 0;
    [[nodiscard]] virtual bool GetBroadcastDucking(BroadcastDuckingConfig& outConfig) const = 0;
    [[nodiscard]] virtual bool StartBroadcastMicCapture(int deviceId, int sampleRate, int channels) = 0;
    [[nodiscard]] virtual bool StopBroadcastMicCapture() = 0;
    [[nodiscard]] virtual bool GetBroadcastStats(BroadcastStats& outStats) const = 0;
    [[nodiscard]] virtual const char* GetBroadcastError() const = 0;

    // ── Settings ──────────────────────────────────────────────────────────────
    [[nodiscard]] virtual bool SetBufferSize(uint32_t milliseconds) = 0;
    [[nodiscard]] virtual bool SetAccurateLength(bool accurate) = 0;
    [[nodiscard]] virtual bool SetAccurateSeek(bool accurate) = 0;
    [[nodiscard]] virtual bool SetPlaylistAllowDuplicates(bool allow) = 0;
    [[nodiscard]] virtual bool GetPlaylistAllowDuplicates() const = 0;

    // ── Callbacks ─────────────────────────────────────────────────────────────
    [[nodiscard]] virtual bool SetCallback(PlayerCallback callback, void* userData) = 0;
    [[nodiscard]] virtual bool SetPCMOutputCallback(PCMOutputCallback callback, void* userData,
                                                    bool disableBuiltInOutput) = 0;
};

// ── Factory ───────────────────────────────────────────────────────────────────
extern "C" {
    ZFORK_API ZFork* ZFORK_CALL CreateZFork();
    ZFORK_API ZFork* ZFORK_CALL CreateZForkWithConfig(const ZForkConfig* config);
    ZFORK_API void   ZFORK_CALL DestroyZFork(ZFork* instance);
}

} // namespace libZFork

// ── C API (opaque handle) ──────────────────────────────────────────────────────
extern "C" {

typedef void* ZFORK_HANDLE;
typedef void* ZFORK_ANALYSIS_HANDLE;

ZFORK_API ZFORK_HANDLE ZFORK_CALL zfork_Create();
ZFORK_API ZFORK_HANDLE ZFORK_CALL zfork_CreateWithConfig(const libZFork::ZForkConfig* config);
ZFORK_API void         ZFORK_CALL zfork_Destroy(ZFORK_HANDLE handle);

ZFORK_API int          ZFORK_CALL zfork_OpenFile(ZFORK_HANDLE h, const wchar_t* path, int format);
ZFORK_API int          ZFORK_CALL zfork_OpenFileA(ZFORK_HANDLE h, const char*    path, int format);
ZFORK_API int          ZFORK_CALL zfork_LoadPlaylistFile(ZFORK_HANDLE h, const wchar_t* path, int fallbackFormat);
ZFORK_API int          ZFORK_CALL zfork_LoadPlaylistFileA(ZFORK_HANDLE h, const char* path, int fallbackFormat);
ZFORK_API int          ZFORK_CALL zfork_AddFile(ZFORK_HANDLE h,  const wchar_t* path, int format);
ZFORK_API int          ZFORK_CALL zfork_AddFileA(ZFORK_HANDLE h, const char* path, int format);
ZFORK_API int          ZFORK_CALL zfork_RemoveQueueItem(ZFORK_HANDLE h, uint32_t index);
ZFORK_API int          ZFORK_CALL zfork_MoveQueueItem(ZFORK_HANDLE h, uint32_t fromIndex, uint32_t toIndex);
ZFORK_API uint32_t     ZFORK_CALL zfork_GetQueueCount(ZFORK_HANDLE h);
/** UTF-8 path of queue item into out (NUL-terminated). Returns 1 on success. */
ZFORK_API int          ZFORK_CALL zfork_GetQueueItemPathA(ZFORK_HANDLE h, uint32_t index, char* out, uint32_t outBytes);
ZFORK_API int          ZFORK_CALL zfork_OpenStream(ZFORK_HANDLE h, const void* data, uint32_t size, int format);
ZFORK_API void         ZFORK_CALL zfork_Close(ZFORK_HANDLE h);

ZFORK_API int          ZFORK_CALL zfork_Play(ZFORK_HANDLE h);
ZFORK_API int          ZFORK_CALL zfork_PlayLoop(ZFORK_HANDLE h, uint32_t count);
ZFORK_API int          ZFORK_CALL zfork_Pause(ZFORK_HANDLE h);
ZFORK_API int          ZFORK_CALL zfork_Resume(ZFORK_HANDLE h);
ZFORK_API int          ZFORK_CALL zfork_Stop(ZFORK_HANDLE h);

ZFORK_API int          ZFORK_CALL zfork_SeekMs(ZFORK_HANDLE h, int origin, uint32_t positionMs);
ZFORK_API int          ZFORK_CALL zfork_GetPositionMs(ZFORK_HANDLE h, uint32_t* positionMs);
ZFORK_API int          ZFORK_CALL zfork_GetStreamInfo(ZFORK_HANDLE h, int songIndex, libZFork::StreamInfo* outInfo);
ZFORK_API int          ZFORK_CALL zfork_GetStatus(ZFORK_HANDLE h, libZFork::StreamStatus* outStatus);

ZFORK_API int          ZFORK_CALL zfork_SetMasterVolume(ZFORK_HANDLE h, int left, int right);
ZFORK_API int          ZFORK_CALL zfork_GetMasterVolume(ZFORK_HANDLE h, int* left, int* right);
ZFORK_API int          ZFORK_CALL zfork_SetPlayerVolume(ZFORK_HANDLE h, int left, int right);
ZFORK_API int          ZFORK_CALL zfork_GetPlayerVolume(ZFORK_HANDLE h, int* left, int* right);
ZFORK_API int          ZFORK_CALL zfork_GetVUData(ZFORK_HANDLE h, float* left, float* right);
ZFORK_API int          ZFORK_CALL zfork_MixChannels(ZFORK_HANDLE h, int mono);
ZFORK_API int          ZFORK_CALL zfork_SetRate(ZFORK_HANDLE h, float rate);
ZFORK_API int          ZFORK_CALL zfork_SetPitch(ZFORK_HANDLE h, float semitones);
ZFORK_API int          ZFORK_CALL zfork_SetTempo(ZFORK_HANDLE h, float tempo);

ZFORK_API int          ZFORK_CALL zfork_EnableEqualizer(ZFORK_HANDLE h, int enable);
ZFORK_API int          ZFORK_CALL zfork_SetEQBandGain(ZFORK_HANDLE h, int band, float gainDb);
ZFORK_API int          ZFORK_CALL zfork_GetEQBandGain(ZFORK_HANDLE h, int band, float* gainDb);
ZFORK_API int          ZFORK_CALL zfork_ResetEqualizer(ZFORK_HANDLE h);
ZFORK_API int          ZFORK_CALL zfork_EnableEcho(ZFORK_HANDLE h, int enable);
ZFORK_API int          ZFORK_CALL zfork_SetEchoParams(ZFORK_HANDLE h, const libZFork::EchoParams* params);
ZFORK_API int          ZFORK_CALL zfork_GetEchoParams(ZFORK_HANDLE h, libZFork::EchoParams* outParams);
ZFORK_API int          ZFORK_CALL zfork_EnableVocalCut(ZFORK_HANDLE h, int enable);
ZFORK_API int          ZFORK_CALL zfork_EnableSideCut(ZFORK_HANDLE h, int enable);
ZFORK_API int          ZFORK_CALL zfork_EnableStereoEnhancer(ZFORK_HANDLE h, int enable);
ZFORK_API int          ZFORK_CALL zfork_SetStereoWidth(ZFORK_HANDLE h, float width);
ZFORK_API int          ZFORK_CALL zfork_GetStereoWidth(ZFORK_HANDLE h, float* outWidth);
ZFORK_API int          ZFORK_CALL zfork_SetBufferSize(ZFORK_HANDLE h, uint32_t milliseconds);

ZFORK_API int          ZFORK_CALL zfork_GetFFTData(ZFORK_HANDLE h, float* left, float* right, uint32_t points);
ZFORK_API int          ZFORK_CALL zfork_DetectBPM(ZFORK_HANDLE h, int algo, float* outBPM);
ZFORK_API int          ZFORK_CALL zfork_DetectFileBPMA(ZFORK_HANDLE h, const char* path, int algo, float* outBPM);
ZFORK_API int          ZFORK_CALL zfork_AnalyzeFile(ZFORK_HANDLE h, const wchar_t* path, uint32_t flags, ZFORK_ANALYSIS_HANDLE* outHandle);
ZFORK_API int          ZFORK_CALL zfork_AnalyzeFileA(ZFORK_HANDLE h, const char* path, uint32_t flags, ZFORK_ANALYSIS_HANDLE* outHandle);
ZFORK_API void         ZFORK_CALL zfork_FreeAnalysis(ZFORK_HANDLE h, ZFORK_ANALYSIS_HANDLE analysis);
ZFORK_API int          ZFORK_CALL zfork_GetAnalysisSummary(ZFORK_HANDLE h, ZFORK_ANALYSIS_HANDLE analysis,
                                                           float* outBpm, uint32_t* outLengthMs, uint32_t* outFlags);
ZFORK_API uint32_t     ZFORK_CALL zfork_CopyAnalysisWaveform(ZFORK_HANDLE h, ZFORK_ANALYSIS_HANDLE analysis,
                                                             uint8_t* outBins, uint32_t maxBins);
ZFORK_API uint32_t     ZFORK_CALL zfork_CopyAnalysisPeaks(ZFORK_HANDLE h, ZFORK_ANALYSIS_HANDLE analysis,
                                                          float* outTimesMs, float* outValues, uint32_t maxPeaks);
ZFORK_API int          ZFORK_CALL zfork_SetPlaylistAllowDuplicates(ZFORK_HANDLE h, int allow);
ZFORK_API int          ZFORK_CALL zfork_GetPlaylistAllowDuplicates(ZFORK_HANDLE h);
ZFORK_API int          ZFORK_CALL zfork_StartBroadcast(ZFORK_HANDLE h, const libZFork::BroadcastConfig* config);
ZFORK_API int          ZFORK_CALL zfork_StopBroadcast(ZFORK_HANDLE h);
ZFORK_API int          ZFORK_CALL zfork_PushBroadcastPCM(ZFORK_HANDLE h, const float* interleaved,
                                                         uint32_t frames, int channels, int sampleRate);
ZFORK_API int          ZFORK_CALL zfork_StartBroadcastFile(ZFORK_HANDLE h, const wchar_t* path, int format);
ZFORK_API int          ZFORK_CALL zfork_StartBroadcastFileA(ZFORK_HANDLE h, const char* path, int format);
ZFORK_API int          ZFORK_CALL zfork_AttachBroadcastToCurrentPlayback(ZFORK_HANDLE h, int enable);
ZFORK_API int          ZFORK_CALL zfork_SetBroadcastMetadata(ZFORK_HANDLE h, const char* title, const char* artist, const char* album);
ZFORK_API int          ZFORK_CALL zfork_PushBroadcastMicPCM(ZFORK_HANDLE h, const float* interleaved,
                                                            uint32_t frames, int channels, int sampleRate);
ZFORK_API int          ZFORK_CALL zfork_SetBroadcastMicActive(ZFORK_HANDLE h, int active);
ZFORK_API int          ZFORK_CALL zfork_SetBroadcastDucking(ZFORK_HANDLE h, const libZFork::BroadcastDuckingConfig* config);
ZFORK_API int          ZFORK_CALL zfork_GetBroadcastDucking(ZFORK_HANDLE h, libZFork::BroadcastDuckingConfig* outConfig);
ZFORK_API int          ZFORK_CALL zfork_StartBroadcastMicCapture(ZFORK_HANDLE h, int deviceId, int sampleRate, int channels);
ZFORK_API int          ZFORK_CALL zfork_StopBroadcastMicCapture(ZFORK_HANDLE h);
ZFORK_API int          ZFORK_CALL zfork_GetBroadcastStats(ZFORK_HANDLE h, libZFork::BroadcastStats* outStats);
ZFORK_API const char*  ZFORK_CALL zfork_GetBroadcastError(ZFORK_HANDLE h);
ZFORK_API int          ZFORK_CALL zfork_SetPCMOutputCallback(ZFORK_HANDLE h,
                                                            libZFork::PCMOutputCallback callback,
                                                            void* userData,
                                                            int disableBuiltInOutput);

/** Player-Ereignisse (CallbackMessage). Callback darf vom Audio-Thread kommen. */
ZFORK_API int          ZFORK_CALL zfork_SetCallback(ZFORK_HANDLE h,
                                                    libZFork::PlayerCallback callback,
                                                    void* userData);

/** Ausgabegeraete (-1 = Default/Auto). Enumeration braucht gueltiges Handle. */
ZFORK_API int          ZFORK_CALL zfork_EnumerateOutputDevices(ZFORK_HANDLE h);
ZFORK_API int          ZFORK_CALL zfork_GetOutputDeviceInfo(ZFORK_HANDLE h, int deviceId,
                                                            libZFork::WaveDeviceInfo* outInfo);
ZFORK_API int          ZFORK_CALL zfork_SetOutputDevice(ZFORK_HANDLE h, int deviceId);

ZFORK_API int          ZFORK_CALL zfork_LoadID3Ex(ZFORK_HANDLE h, int songIndex, libZFork::ID3InfoEx* outInfo);
ZFORK_API int          ZFORK_CALL zfork_LoadID3Picture(ZFORK_HANDLE h, int songIndex, int picIndex,
                                                        uint8_t** outData, uint32_t* outSize, int* outType);
ZFORK_API int          ZFORK_CALL zfork_LoadFileID3ExA(ZFORK_HANDLE h, const char* path, libZFork::ID3InfoEx* outInfo);
ZFORK_API int          ZFORK_CALL zfork_LoadFileID3PictureA(ZFORK_HANDLE h, const char* path, int picIndex,
                                                             uint8_t** outData, uint32_t* outSize, int* outType);
ZFORK_API void         ZFORK_CALL zfork_FreeMemory(void* ptr);

/** Parse musical / Camelot / Open Key text (no player handle needed). */
ZFORK_API int          ZFORK_CALL zfork_ParseMusicalKey(const char* text, libZFork::MusicalKeyInfo* out);
/** 1 if keys mix well on the Camelot wheel (same / relative / adjacent). */
ZFORK_API int          ZFORK_CALL zfork_KeysCompatible(const libZFork::MusicalKeyInfo* a,
                                                       const libZFork::MusicalKeyInfo* b);

ZFORK_API const char*  ZFORK_CALL zfork_GetError(ZFORK_HANDLE h);

} // extern "C"
