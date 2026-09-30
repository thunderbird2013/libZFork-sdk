#pragma once
#include <libzfork/libzfork.h>
#include "queue.h"
#include "output/audio_output.h"
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <vector>
#include <array>
#include <span>
#include <functional>
#include <unordered_map>
#include <filesystem>
#include <deque>
#include <condition_variable>
#ifdef _WIN32
#  include <windows.h>
#  include <mmsystem.h>
#endif

namespace libZFork {

class DecoderBase;
class EncoderBase;
class Equalizer;
class EchoEffect;
class SoundTouchProc;
class CenterCutProc;
class StereoEnhancer;
class FFTAnalyzer;
class SpectrumAnalyzer;
class ID3Tags;
class BroadcastTransport;
class Mp3BroadcastEncoder;

class Player final : public ZFork {
public:
    Player();
    // Inject a custom output backend — used by tests (e.g. NullOutput).
    explicit Player(std::unique_ptr<IAudioOutput> output);
    ~Player() override;

    // Not part of the ZFork interface (config-time only, called from
    // CreateZForkWithConfig / zfork_CreateWithConfig before first use) — see
    // ZForkConfig::outputBackend. Returns false if output_ isn't the
    // AutoOutput backend (e.g. a custom output was injected via the test
    // constructor) or the preference couldn't be applied.
    bool SetOutputBackendPreference(int backend);

    // ZFork interface ─────────────────────────────────────────────────────────
    bool OpenFile(const wchar_t* path, StreamFormat format) override;
    bool OpenFileA(const char*   path, StreamFormat format) override;
    bool LoadPlaylistFile(const wchar_t* path, StreamFormat fallbackFormat) override;
    bool LoadPlaylistFileA(const char* path, StreamFormat fallbackFormat) override;
    bool AddFile(const wchar_t*  path, StreamFormat format) override;
    bool AddFileA(const char*    path, StreamFormat format) override;
    bool RemoveQueueItem(uint32_t index) override;
    bool MoveQueueItem(uint32_t fromIndex, uint32_t toIndex) override;
    uint32_t GetQueueCount() const override;
    bool GetQueueItemPathA(uint32_t index, char* out, uint32_t outBytes) const override;
    bool OpenStream(const void* data, uint32_t size, StreamFormat format) override;
    bool OpenPCMStream(PCMFormat fmt) override;
    void Close() override;

    bool Play() override;
    bool PlayLoop(uint32_t loopCount) override;
    bool Pause() override;
    bool Resume() override;
    bool Stop() override;

    bool Seek(SeekOrigin origin, const StreamTime& time) override;
    bool GetPosition(StreamTime& outTime) const override;

    bool         GetStreamInfo(int songIndex, StreamInfo& outInfo) const override;
    StreamStatus GetStatus() const override;
    const char*  GetError() const override;

    bool SetMasterVolume(int left, int right) override;
    bool GetMasterVolume(int& outLeft, int& outRight) const override;
    bool SetPlayerVolume(int left, int right) override;
    bool GetPlayerVolume(int& outLeft, int& outRight) const override;
    bool SlideVolume(int fromLeft, int fromRight, int toLeft, int toRight, uint32_t durationMs) override;
    bool GetVUData(VUData& outVU) const override;
    bool MixChannels(bool mono) override;

    bool SetRate(float rate) override;
    bool SetPitch(float semitones) override;
    bool SetTempo(float tempo) override;

    bool EnableEqualizer(bool enable) override;
    bool SetEqualizerBandGain(int band, float gainDb) override;
    bool GetEqualizerBandGain(int band, float& gainDb) const override;
    bool ResetEqualizer() override;

    bool EnableEcho(bool enable) override;
    bool SetEchoParams(const EchoParams& params) override;
    bool GetEchoParams(EchoParams& outParams) const override;

    bool EnableVocalCut(bool enable) override;
    bool EnableSideCut(bool enable) override;
    bool EnableStereoEnhancer(bool enable) override;
    bool SetStereoWidth(float width) override;
    bool GetStereoWidth(float& outWidth) const override;

    bool SetFFTPoints(uint32_t points) override;
    bool SetFFTWindow(FFTWindow window) override;
    bool GetFFTData(float* outLeft, float* outRight, uint32_t points) const override;
    bool DrawFFTGraph(void* hdc, int x, int y, int width, int height) const override;
    bool SetFFTGraphType(FFTGraphType type) override;
    bool SetFFTGraphColor(int index, uint32_t colorRGB) override;

    bool DetectBPM(BPMAlgorithm algo, float& outBPM) override;
    bool DetectFileBPM(const wchar_t* path, BPMAlgorithm algo, float& outBPM) override;
    bool AnalyzeFile(const wchar_t* path, uint32_t flags, void** outHandle) override;
    void FreeAnalysisHandle(void* handle) override;
    bool GetAnalysisSummary(void* handle, float& outBpm, uint32_t& outLengthMs, uint32_t& outFlags) const override;
    uint32_t CopyAnalysisWaveform(void* handle, uint8_t* outBins, uint32_t maxBins) const override;
    uint32_t CopyAnalysisPeaks(void* handle, float* outTimesMs, float* outValues, uint32_t maxPeaks) const override;

    bool LoadID3(int songIndex, ID3Info& outInfo) const override;
    bool LoadID3Ex(int songIndex, ID3InfoEx& outInfo) const override;
    bool LoadFileID3(const wchar_t* path, ID3Info& outInfo) const override;
    bool LoadFileID3Ex(const wchar_t* path, ID3InfoEx& outInfo) const override;
    bool GetID3Picture(int songIndex, int picIndex, ID3Picture& outPic) const override;

    int  EnumerateWaveOutDevices() const override;
    bool GetWaveOutDeviceInfo(int deviceId, WaveDeviceInfo& outInfo) const override;
    bool SetWaveOutDevice(int deviceId) override;
    bool SetWaveOutFile(const wchar_t* path) override;

    int  EnumerateWaveInDevices() const override;
    bool GetWaveInDeviceInfo(int deviceId, WaveDeviceInfo& outInfo) const override;
    bool SetWaveInDevice(int deviceId) override;

    bool StartEncoding(StreamFormat format, const wchar_t* outputPath,
                       EncoderWriteCallback writeCallback, void* userData) override;
    bool StopEncoding() override;

    bool PushPCMData(const void* data, uint32_t byteCount) override;
    bool DecodeNextChunk(float* outBuf, uint32_t maxFrames, uint32_t& decodedFrames) override;
    bool StartBroadcast(const BroadcastConfig& config) override;
    bool StopBroadcast() override;
    bool PushBroadcastPCM(const float* interleaved, uint32_t frames, int channels, int sampleRate) override;
    bool PushBroadcastMicPCM(const float* interleaved, uint32_t frames, int channels, int sampleRate) override;
    bool SetBroadcastMicActive(bool active) override;
    bool SetBroadcastDucking(const BroadcastDuckingConfig& config) override;
    bool GetBroadcastDucking(BroadcastDuckingConfig& outConfig) const override;
    bool StartBroadcastMicCapture(int deviceId, int sampleRate, int channels) override;
    bool StopBroadcastMicCapture() override;
    bool StartBroadcastFile(const wchar_t* path, StreamFormat inputFormat) override;
    bool AttachBroadcastToCurrentPlayback(bool enable) override;
    bool SetBroadcastMetadata(const char* title, const char* artist, const char* album) override;
    bool GetBroadcastStats(BroadcastStats& outStats) const override;
    const char* GetBroadcastError() const override;

    bool SetBufferSize(uint32_t milliseconds) override;
    bool SetAccurateLength(bool accurate) override;
    bool SetAccurateSeek(bool accurate) override;
    bool SetPlaylistAllowDuplicates(bool allow) override;
    bool GetPlaylistAllowDuplicates() const override;

    bool SetCallback(PlayerCallback callback, void* userData) override;
    bool SetPCMOutputCallback(PCMOutputCallback callback, void* userData,
                              bool disableBuiltInOutput) override;

private:
    // ── Internal helpers ──────────────────────────────────────────────────────
    void SetError(std::string_view msg);
    bool OpenDecoder(const wchar_t* path, StreamFormat format, const void* memData, uint32_t memSize);
    static StreamFormat DetectFormat(const wchar_t* path);
    static StreamFormat DetectFormatFromMemory(const void* data, uint32_t size);

    // ── Decode thread ─────────────────────────────────────────────────────────
    void DecodeThread();
    void StopDecodeThread();

    // ── DSP chain ─────────────────────────────────────────────────────────────
    void ApplyDSP(std::span<float> pcm, int channels, int sampleRate);
    void UpdateVU(std::span<const float> pcm, int channels);
    void FeedFFT(std::span<const float> pcm, int channels);
    // PCM helpers
    void FloatToPCM16(std::span<const float> src, std::span<int16_t> dst);
    void PCM16ToFloat(std::span<const int16_t> src, std::span<float> dst);

    // ── Song queue ────────────────────────────────────────────────────────────
    struct SongEntry {
        std::wstring path;
        StreamFormat format;
        std::vector<uint8_t> memData; // for in-memory streams
        uint64_t trackId = 0;         // stable-ish fingerprint for caching/dedupe
    };

    static uint64_t BuildFileTrackId(const wchar_t* path, StreamFormat format);
    static uint64_t BuildMemoryTrackId(const void* data, uint32_t size, StreamFormat format);
    void RegisterTrackId(uint64_t id);
    void UnregisterTrackId(uint64_t id);
    void FireQueueChangedEvent() const;

    struct TrackCacheEntry {
        std::wstring path;
        std::string  title;
        std::string  artist;
        std::string  album;
        float        bpm = 0.0f;
        uint32_t     lengthMs = 0;
        std::string  waveform; // compact preview string (64 bins, 0-255)
        std::string  peaks;    // "t0:v0,t1:v1,..."
    };
    struct AnalysisHandleData;
    static std::filesystem::path GetCachePath();
    static std::string SanitizeCacheField(std::string_view s);
    void LoadTrackCache();
    void SaveTrackCache() const;
    void PrimeTrackCache(const SongEntry& e);
    void UpdateTrackCacheBpm(uint64_t trackId, float bpm);
    static std::vector<uint8_t> ParseWaveformCsv(std::string_view csv);
    static std::vector<std::pair<float, float>> ParsePeaksCsv(std::string_view csv);
    static std::string BuildPeaksFromWaveformCsv(std::string_view waveformCsv, uint32_t lengthMs);
    void BroadcastWorker();
    void StopBroadcastFileThread();
    bool StopBroadcastMicCaptureLocked();
    bool EnqueueBroadcastInterleaved(const float* interleaved, uint32_t frames, int channels, int sampleRate);
    bool EnqueueBroadcastMicInterleaved(const float* interleaved, uint32_t frames, int channels, int sampleRate);
    bool EnqueueBroadcastMicInterleavedLocked(const float* interleaved, uint32_t frames, int channels, int sampleRate);
    void MixBroadcastMicLocked(std::vector<float>& program, uint32_t frames, int channels);
    void StepBroadcastDuckGainLocked(bool micActive, uint32_t frames);
    bool ApplyBroadcastMetadataLocked();
    void UpdateBroadcastMetadataFromPath(const std::wstring& path);
    void UpdateBroadcastMetadataForCurrentSong();
#ifdef _WIN32
    bool StartBroadcastMicCaptureLocked(int deviceId, int sampleRate, int channels);
    void OnWaveInBufferDone(size_t bufferIndex, uint32_t bytesRecorded);
#endif
#ifdef LIBZFORK_WITH_PORTAUDIO
    bool StartBroadcastMicCapturePortAudioLocked(int deviceId, int sampleRate, int channels);
    void StopBroadcastMicCapturePortAudioLocked();
#endif

    // ── State ─────────────────────────────────────────────────────────────────
    mutable std::string         errorMsg_;
    mutable std::mutex          stateMtx_;

    std::atomic<PlayerStatus>   status_       { PlayerStatus::Stopped };
    std::atomic<bool>           paused_       { false };
    std::atomic<uint32_t>       loopCount_    { 0 };
    std::atomic<int>            songIndex_    { 0 };
    std::atomic<bool>           decodeRunning_{ false };

    int masterVolLeft_   = 100;
    int masterVolRight_  = 100;
    int playerVolLeft_   = 100;
    int playerVolRight_  = 100;
    bool monoMix_        = false;
    bool accurateLength_ = false;
    bool accurateSeek_   = false;
    bool allowDuplicateTracks_ = true;
    uint32_t bufferMs_   = 500;

    // Sliding volume
    std::atomic<bool>  sliding_    { false };
    std::jthread       slideThread_;

    // VU metering
    mutable std::atomic<float> vuLeft_  { 0.0f };
    mutable std::atomic<float> vuRight_ { 0.0f };

    // FFT/Spectrum: only fed while someone actually reads it (GetFFTData /
    // DrawFFTGraph) — otherwise the decode thread would run two 2048-point
    // FFTs per chunk for nothing on every playing deck, even with no
    // spectrum UI attached.
    mutable std::atomic<uint64_t> fftLastAccessMs_{ 0 };
    bool FFTConsumerActive() const;
    void MarkFFTAccessed() const;

    // Effect flags
    bool equalizerEnabled_  = false;
    bool echoEnabled_       = false;
    bool vocalCutEnabled_   = false;
    bool sideCutEnabled_    = false;
    bool stereoEnhancerEnabled_ = false;

    // Song queue
    std::vector<SongEntry>      songs_;
    std::unordered_map<uint64_t, uint32_t> trackRefCount_;
    std::unordered_map<uint64_t, TrackCacheEntry> trackCache_;
    std::unique_ptr<DecoderBase> decoder_;

    // DSP modules
    std::unique_ptr<Equalizer>      equalizer_;
    std::unique_ptr<EchoEffect>     echo_;
    std::unique_ptr<SoundTouchProc> soundTouch_;
    std::unique_ptr<CenterCutProc>  centerCut_;
    std::unique_ptr<StereoEnhancer> stereoEnhancer_;
    std::unique_ptr<SpectrumAnalyzer> spectrum_;

    // Encoding
    std::unique_ptr<EncoderBase> encoder_;
    EncoderWriteCallback         encWriteCb_ = nullptr;
    void*                        encUserData_= nullptr;

    // Broadcast streaming
    mutable std::mutex                   broadcastMtx_;
    mutable BroadcastStats               broadcastStats_{};
    mutable std::string                  broadcastError_;
    BroadcastConfig                      broadcastConfig_{};
    std::unique_ptr<BroadcastTransport>  broadcastTransport_;
    std::unique_ptr<Mp3BroadcastEncoder> broadcastEncoder_;
    std::jthread                         broadcastThread_;
    std::jthread                         broadcastFileThread_;
    std::condition_variable              broadcastCv_;
    std::deque<std::vector<float>>       broadcastQueue_;
    size_t                               broadcastQueuedFrames_ = 0;
    std::atomic<bool>                    broadcastRunning_{ false };
    std::atomic<bool>                    broadcastStopRequested_{ false };
    std::atomic<bool>                    broadcastMirrorPlayback_{ false };
    std::atomic<bool>                    broadcastMicActive_{ false };
    BroadcastDuckingConfig               broadcastDucking_{ true, -10.0f, 35u, 250u, 0.02f };
    float                                broadcastDuckGain_ = 1.0f;
    std::deque<std::vector<float>>       broadcastMicQueue_;
    size_t                               broadcastMicQueuedFrames_ = 0;
    size_t                               broadcastMicFrontOffsetSamples_ = 0;
    int                                  broadcastMicCaptureDevice_ = -1;
    int                                  broadcastMicCaptureSampleRate_ = 0;
    int                                  broadcastMicCaptureChannels_ = 0;
    std::atomic<bool>                    broadcastMicCaptureRunning_{ false };
    std::string                          broadcastMetaTitle_;
    std::string                          broadcastMetaArtist_;
    std::string                          broadcastMetaAlbum_;
#ifdef _WIN32
    HWAVEIN                              waveInHandle_ = nullptr;
    static constexpr size_t              kWaveInBufferCount = 4;
    static constexpr size_t              kWaveInFramesPerBuffer = 1024;
    std::array<WAVEHDR, kWaveInBufferCount> waveInHeaders_{};
    std::array<std::vector<int16_t>, kWaveInBufferCount> waveInBuffers_{};
    std::jthread                         waveInPollThread_;
#endif
#ifdef LIBZFORK_WITH_PORTAUDIO
    void*                                paMicStream_ = nullptr;
    std::jthread                         paMicCaptureThread_;
    bool                                 paMicInitialized_ = false;
#endif

    // Audio output backend (platform-specific behind IAudioOutput)
    std::unique_ptr<IAudioOutput> output_;

    // Decode thread
    AudioQueue        pcmQueue_;
    std::jthread      decodeThread_;

    // Callback
    PlayerCallback    callback_     = nullptr;
    void*             callbackData_ = nullptr;
    PCMOutputCallback pcmOutputCallback_ = nullptr;
    void*             pcmOutputUserData_ = nullptr;
    bool              disableBuiltInOutput_ = false;
    uint32_t          lastPosCbMs_  = 0;
};

} // namespace libZFork
