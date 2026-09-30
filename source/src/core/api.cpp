#include <libzfork/libzfork.h>
#include "player.h"
#include "utils/tags.h"
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#ifdef _WIN32
#  include <windows.h>
#endif

static bool ApplyCreateConfig(libZFork::Player* p, const libZFork::ZForkConfig* cfg);

// ── DLL entry point ────────────────────────────────────────────────────────────

#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
#endif

// ── C++ factory ────────────────────────────────────────────────────────────────

namespace libZFork {

extern "C" {

ZFORK_API ZFork* ZFORK_CALL CreateZFork() {
    return new Player();
}

ZFORK_API ZFork* ZFORK_CALL CreateZForkWithConfig(const ZForkConfig* config) {
    Player* p = new Player();
    if (!::ApplyCreateConfig(p, config)) {
        delete p;
        return nullptr;
    }
    return p;
}

ZFORK_API void ZFORK_CALL DestroyZFork(ZFork* instance) {
    delete instance;
}

} // extern "C"

} // namespace libZFork

// ── C API ──────────────────────────────────────────────────────────────────────

using namespace libZFork;

static Player* cast(ZFORK_HANDLE h) { return static_cast<Player*>(h); }

static std::wstring utf8PathToWide(const char* path) {
    if (!path) return {};
    return std::filesystem::u8path(path).wstring();
}

static bool ApplyCreateConfig(Player* p, const ZForkConfig* cfg) {
    if (!p || !cfg) return true;
    const size_t sz = cfg->structSize;
    if (sz < sizeof(uint32_t)) return false;
    auto has = [sz](size_t off, size_t bytes) {
        return sz >= (off + bytes);
    };
    if (has(offsetof(ZForkConfig, waveOutDeviceId), sizeof(cfg->waveOutDeviceId)) &&
        cfg->waveOutDeviceId >= 0) {
        if (!p->SetWaveOutDevice(cfg->waveOutDeviceId)) return false;
    }
    if (has(offsetof(ZForkConfig, bufferMs), sizeof(cfg->bufferMs)) &&
        cfg->bufferMs > 0) {
        if (!p->SetBufferSize(cfg->bufferMs)) return false;
    }
    if (has(offsetof(ZForkConfig, masterVolumeRight), sizeof(cfg->masterVolumeRight)) &&
        cfg->masterVolumeLeft >= 0 && cfg->masterVolumeRight >= 0) {
        if (!p->SetMasterVolume(cfg->masterVolumeLeft, cfg->masterVolumeRight)) return false;
    }
    if (has(offsetof(ZForkConfig, playerVolumeRight), sizeof(cfg->playerVolumeRight)) &&
        cfg->playerVolumeLeft >= 0 && cfg->playerVolumeRight >= 0) {
        if (!p->SetPlayerVolume(cfg->playerVolumeLeft, cfg->playerVolumeRight)) return false;
    }
    if (has(offsetof(ZForkConfig, monoMix), sizeof(cfg->monoMix)) &&
        (cfg->monoMix == 0 || cfg->monoMix == 1)) {
        if (!p->MixChannels(cfg->monoMix != 0)) return false;
    }
    if (has(offsetof(ZForkConfig, outputBackend), sizeof(cfg->outputBackend)) &&
        cfg->outputBackend >= 0) {
        // Best-effort: only applies to the default AutoOutput backend
        // (always true unless a custom output was injected via the
        // test-only Player(std::unique_ptr<IAudioOutput>) constructor,
        // which CreateZForkWithConfig never uses).
        p->SetOutputBackendPreference(cfg->outputBackend);
    }
    return true;
}

extern "C" {

ZFORK_API ZFORK_HANDLE ZFORK_CALL zfork_Create() {
    return new Player();
}

ZFORK_API ZFORK_HANDLE ZFORK_CALL zfork_CreateWithConfig(const ZForkConfig* config) {
    Player* p = new Player();
    if (!ApplyCreateConfig(p, config)) {
        delete p;
        return nullptr;
    }
    return p;
}

ZFORK_API void ZFORK_CALL zfork_Destroy(ZFORK_HANDLE h) {
    delete cast(h);
}

ZFORK_API int ZFORK_CALL zfork_OpenFile(ZFORK_HANDLE h, const wchar_t* path, int format) {
    return cast(h)->OpenFile(path, static_cast<StreamFormat>(format)) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_OpenFileA(ZFORK_HANDLE h, const char* path, int format) {
    return cast(h)->OpenFileA(path, static_cast<StreamFormat>(format)) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_LoadPlaylistFile(ZFORK_HANDLE h, const wchar_t* path, int fallbackFormat) {
    return cast(h)->LoadPlaylistFile(path, static_cast<StreamFormat>(fallbackFormat)) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_LoadPlaylistFileA(ZFORK_HANDLE h, const char* path, int fallbackFormat) {
    return cast(h)->LoadPlaylistFileA(path, static_cast<StreamFormat>(fallbackFormat)) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_AddFile(ZFORK_HANDLE h, const wchar_t* path, int format) {
    return cast(h)->AddFile(path, static_cast<StreamFormat>(format)) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_AddFileA(ZFORK_HANDLE h, const char* path, int format) {
    return cast(h)->AddFileA(path, static_cast<StreamFormat>(format)) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_RemoveQueueItem(ZFORK_HANDLE h, uint32_t index) {
    return cast(h)->RemoveQueueItem(index) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_MoveQueueItem(ZFORK_HANDLE h, uint32_t fromIndex, uint32_t toIndex) {
    return cast(h)->MoveQueueItem(fromIndex, toIndex) ? 1 : 0;
}

ZFORK_API uint32_t ZFORK_CALL zfork_GetQueueCount(ZFORK_HANDLE h) {
    return cast(h)->GetQueueCount();
}

ZFORK_API int ZFORK_CALL zfork_GetQueueItemPathA(ZFORK_HANDLE h, uint32_t index, char* out, uint32_t outBytes) {
    return cast(h)->GetQueueItemPathA(index, out, outBytes) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_OpenStream(ZFORK_HANDLE h, const void* data, uint32_t size, int format) {
    return cast(h)->OpenStream(data, size, static_cast<StreamFormat>(format)) ? 1 : 0;
}

ZFORK_API void ZFORK_CALL zfork_Close(ZFORK_HANDLE h) {
    cast(h)->Close();
}

ZFORK_API int ZFORK_CALL zfork_Play(ZFORK_HANDLE h) {
    return cast(h)->Play() ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_PlayLoop(ZFORK_HANDLE h, uint32_t count) {
    return cast(h)->PlayLoop(count) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_Pause(ZFORK_HANDLE h) {
    return cast(h)->Pause() ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_Resume(ZFORK_HANDLE h) {
    return cast(h)->Resume() ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_Stop(ZFORK_HANDLE h) {
    return cast(h)->Stop() ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_SeekMs(ZFORK_HANDLE h, int origin, uint32_t positionMs) {
    StreamTime t{};
    t.ms = positionMs;
    return cast(h)->Seek(static_cast<SeekOrigin>(origin), t) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_GetPositionMs(ZFORK_HANDLE h, uint32_t* positionMs) {
    if (!positionMs) return 0;
    StreamTime t{};
    if (!cast(h)->GetPosition(t)) return 0;
    *positionMs = t.ms;
    return 1;
}

ZFORK_API int ZFORK_CALL zfork_GetStreamInfo(ZFORK_HANDLE h, int songIndex, StreamInfo* outInfo) {
    if (!outInfo) return 0;
    return cast(h)->GetStreamInfo(songIndex, *outInfo) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_GetStatus(ZFORK_HANDLE h, StreamStatus* outStatus) {
    if (!outStatus) return 0;
    *outStatus = cast(h)->GetStatus();
    return 1;
}

ZFORK_API int ZFORK_CALL zfork_SetMasterVolume(ZFORK_HANDLE h, int left, int right) {
    return cast(h)->SetMasterVolume(left, right) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_GetMasterVolume(ZFORK_HANDLE h, int* left, int* right) {
    return cast(h)->GetMasterVolume(*left, *right) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_SetPlayerVolume(ZFORK_HANDLE h, int left, int right) {
    return cast(h)->SetPlayerVolume(left, right) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_GetPlayerVolume(ZFORK_HANDLE h, int* left, int* right) {
    if (!left || !right) return 0;
    return cast(h)->GetPlayerVolume(*left, *right) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_GetVUData(ZFORK_HANDLE h, float* left, float* right) {
    if (!left || !right) return 0;
    VUData vu{};
    if (!cast(h)->GetVUData(vu)) return 0;
    *left  = vu.left;
    *right = vu.right;
    return 1;
}

ZFORK_API int ZFORK_CALL zfork_MixChannels(ZFORK_HANDLE h, int mono) {
    return cast(h)->MixChannels(mono != 0) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_SetRate(ZFORK_HANDLE h, float rate) {
    return cast(h)->SetRate(rate) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_SetPitch(ZFORK_HANDLE h, float semitones) {
    return cast(h)->SetPitch(semitones) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_SetTempo(ZFORK_HANDLE h, float tempo) {
    return cast(h)->SetTempo(tempo) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_EnableEqualizer(ZFORK_HANDLE h, int enable) {
    return cast(h)->EnableEqualizer(enable != 0) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_SetEQBandGain(ZFORK_HANDLE h, int band, float gainDb) {
    return cast(h)->SetEqualizerBandGain(band, gainDb) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_GetEQBandGain(ZFORK_HANDLE h, int band, float* gainDb) {
    if (!gainDb) return 0;
    return cast(h)->GetEqualizerBandGain(band, *gainDb) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_ResetEqualizer(ZFORK_HANDLE h) {
    return cast(h)->ResetEqualizer() ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_EnableEcho(ZFORK_HANDLE h, int enable) {
    return cast(h)->EnableEcho(enable != 0) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_SetEchoParams(ZFORK_HANDLE h, const EchoParams* params) {
    if (!params) return 0;
    return cast(h)->SetEchoParams(*params) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_GetEchoParams(ZFORK_HANDLE h, EchoParams* outParams) {
    if (!outParams) return 0;
    return cast(h)->GetEchoParams(*outParams) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_EnableVocalCut(ZFORK_HANDLE h, int enable) {
    return cast(h)->EnableVocalCut(enable != 0) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_EnableSideCut(ZFORK_HANDLE h, int enable) {
    return cast(h)->EnableSideCut(enable != 0) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_EnableStereoEnhancer(ZFORK_HANDLE h, int enable) {
    return cast(h)->EnableStereoEnhancer(enable != 0) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_SetStereoWidth(ZFORK_HANDLE h, float width) {
    return cast(h)->SetStereoWidth(width) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_GetStereoWidth(ZFORK_HANDLE h, float* outWidth) {
    if (!outWidth) return 0;
    return cast(h)->GetStereoWidth(*outWidth) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_SetBufferSize(ZFORK_HANDLE h, uint32_t milliseconds) {
    return cast(h)->SetBufferSize(milliseconds) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_GetFFTData(ZFORK_HANDLE h, float* left, float* right, uint32_t points) {
    return cast(h)->GetFFTData(left, right, points) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_DetectBPM(ZFORK_HANDLE h, int algo, float* outBPM) {
    return cast(h)->DetectBPM(static_cast<BPMAlgorithm>(algo), *outBPM) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_DetectFileBPMA(ZFORK_HANDLE h, const char* path, int algo, float* outBPM) {
    if (!path || !outBPM) return 0;
    const auto wpath = utf8PathToWide(path);
    return cast(h)->DetectFileBPM(wpath.c_str(), static_cast<BPMAlgorithm>(algo), *outBPM) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_AnalyzeFile(ZFORK_HANDLE h, const wchar_t* path, uint32_t flags, ZFORK_ANALYSIS_HANDLE* outHandle) {
    if (!outHandle) return 0;
    return cast(h)->AnalyzeFile(path, flags, reinterpret_cast<void**>(outHandle)) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_AnalyzeFileA(ZFORK_HANDLE h, const char* path, uint32_t flags, ZFORK_ANALYSIS_HANDLE* outHandle) {
    if (!outHandle || !path) return 0;
    const auto wpath = utf8PathToWide(path);
    return cast(h)->AnalyzeFile(wpath.c_str(), flags, reinterpret_cast<void**>(outHandle)) ? 1 : 0;
}

ZFORK_API void ZFORK_CALL zfork_FreeAnalysis(ZFORK_HANDLE h, ZFORK_ANALYSIS_HANDLE analysis) {
    cast(h)->FreeAnalysisHandle(analysis);
}

ZFORK_API int ZFORK_CALL zfork_GetAnalysisSummary(ZFORK_HANDLE h, ZFORK_ANALYSIS_HANDLE analysis,
                                                  float* outBpm, uint32_t* outLengthMs, uint32_t* outFlags) {
    if (!outBpm || !outLengthMs || !outFlags) return 0;
    return cast(h)->GetAnalysisSummary(analysis, *outBpm, *outLengthMs, *outFlags) ? 1 : 0;
}

ZFORK_API uint32_t ZFORK_CALL zfork_CopyAnalysisWaveform(ZFORK_HANDLE h, ZFORK_ANALYSIS_HANDLE analysis,
                                                         uint8_t* outBins, uint32_t maxBins) {
    return cast(h)->CopyAnalysisWaveform(analysis, outBins, maxBins);
}

ZFORK_API uint32_t ZFORK_CALL zfork_CopyAnalysisPeaks(ZFORK_HANDLE h, ZFORK_ANALYSIS_HANDLE analysis,
                                                      float* outTimesMs, float* outValues, uint32_t maxPeaks) {
    return cast(h)->CopyAnalysisPeaks(analysis, outTimesMs, outValues, maxPeaks);
}

ZFORK_API int ZFORK_CALL zfork_SetPlaylistAllowDuplicates(ZFORK_HANDLE h, int allow) {
    return cast(h)->SetPlaylistAllowDuplicates(allow != 0) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_GetPlaylistAllowDuplicates(ZFORK_HANDLE h) {
    return cast(h)->GetPlaylistAllowDuplicates() ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_StartBroadcast(ZFORK_HANDLE h, const BroadcastConfig* config) {
    if (!config) return 0;
    return cast(h)->StartBroadcast(*config) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_StopBroadcast(ZFORK_HANDLE h) {
    return cast(h)->StopBroadcast() ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_PushBroadcastPCM(ZFORK_HANDLE h, const float* interleaved,
                                                uint32_t frames, int channels, int sampleRate) {
    return cast(h)->PushBroadcastPCM(interleaved, frames, channels, sampleRate) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_StartBroadcastFile(ZFORK_HANDLE h, const wchar_t* path, int format) {
    return cast(h)->StartBroadcastFile(path, static_cast<StreamFormat>(format)) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_StartBroadcastFileA(ZFORK_HANDLE h, const char* path, int format) {
    if (!path) return 0;
    const auto wpath = utf8PathToWide(path);
    return cast(h)->StartBroadcastFile(wpath.c_str(), static_cast<StreamFormat>(format)) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_AttachBroadcastToCurrentPlayback(ZFORK_HANDLE h, int enable) {
    return cast(h)->AttachBroadcastToCurrentPlayback(enable != 0) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_SetBroadcastMetadata(ZFORK_HANDLE h, const char* title, const char* artist, const char* album) {
    return cast(h)->SetBroadcastMetadata(title, artist, album) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_PushBroadcastMicPCM(ZFORK_HANDLE h, const float* interleaved,
                                                   uint32_t frames, int channels, int sampleRate) {
    return cast(h)->PushBroadcastMicPCM(interleaved, frames, channels, sampleRate) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_SetBroadcastMicActive(ZFORK_HANDLE h, int active) {
    return cast(h)->SetBroadcastMicActive(active != 0) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_SetBroadcastDucking(ZFORK_HANDLE h, const BroadcastDuckingConfig* config) {
    if (!config) return 0;
    return cast(h)->SetBroadcastDucking(*config) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_GetBroadcastDucking(ZFORK_HANDLE h, BroadcastDuckingConfig* outConfig) {
    if (!outConfig) return 0;
    return cast(h)->GetBroadcastDucking(*outConfig) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_StartBroadcastMicCapture(ZFORK_HANDLE h, int deviceId, int sampleRate, int channels) {
    return cast(h)->StartBroadcastMicCapture(deviceId, sampleRate, channels) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_StopBroadcastMicCapture(ZFORK_HANDLE h) {
    return cast(h)->StopBroadcastMicCapture() ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_GetBroadcastStats(ZFORK_HANDLE h, BroadcastStats* outStats) {
    if (!outStats) return 0;
    return cast(h)->GetBroadcastStats(*outStats) ? 1 : 0;
}

ZFORK_API const char* ZFORK_CALL zfork_GetBroadcastError(ZFORK_HANDLE h) {
    return cast(h)->GetBroadcastError();
}

ZFORK_API int ZFORK_CALL zfork_SetPCMOutputCallback(ZFORK_HANDLE h,
                                                    PCMOutputCallback callback,
                                                    void* userData,
                                                    int disableBuiltInOutput) {
    return cast(h)->SetPCMOutputCallback(callback, userData, disableBuiltInOutput != 0) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_SetCallback(ZFORK_HANDLE h,
                                           PlayerCallback callback,
                                           void* userData) {
    return cast(h)->SetCallback(callback, userData) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_EnumerateOutputDevices(ZFORK_HANDLE h) {
    return cast(h)->EnumerateWaveOutDevices();
}

ZFORK_API int ZFORK_CALL zfork_GetOutputDeviceInfo(ZFORK_HANDLE h, int deviceId,
                                                   WaveDeviceInfo* outInfo) {
    if (!outInfo) return 0;
    return cast(h)->GetWaveOutDeviceInfo(deviceId, *outInfo) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_SetOutputDevice(ZFORK_HANDLE h, int deviceId) {
    return cast(h)->SetWaveOutDevice(deviceId) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_LoadID3Ex(ZFORK_HANDLE h, int songIndex, ID3InfoEx* outInfo) {
    if (!outInfo) return 0;
    return cast(h)->LoadID3Ex(songIndex, *outInfo) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_LoadID3Picture(ZFORK_HANDLE h, int songIndex, int picIndex,
                                               uint8_t** outData, uint32_t* outSize, int* outType) {
    if (!outData || !outSize) return 0;
    *outData = nullptr;
    *outSize = 0;
    if (outType) *outType = -1;

    ID3Picture pic{};
    if (!cast(h)->GetID3Picture(songIndex, picIndex, pic) || !pic.data || pic.size == 0) {
        if (pic.data) delete[] pic.data;
        return 0;
    }

    void* copy = std::malloc(pic.size);
    if (!copy) {
        delete[] pic.data;
        return 0;
    }
    std::memcpy(copy, pic.data, pic.size);
    delete[] pic.data;

    *outData = static_cast<uint8_t*>(copy);
    *outSize = pic.size;
    if (outType) *outType = pic.type;
    return 1;
}

ZFORK_API int ZFORK_CALL zfork_LoadFileID3ExA(ZFORK_HANDLE h, const char* path, ID3InfoEx* outInfo) {
    if (!outInfo || !path) return 0;
    const auto wpath = utf8PathToWide(path);
    return cast(h)->LoadFileID3Ex(wpath.c_str(), *outInfo) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_LoadFileID3PictureA(ZFORK_HANDLE h, const char* path, int picIndex,
                                                    uint8_t** outData, uint32_t* outSize, int* outType) {
    (void)h;
    if (!outData || !outSize || !path) return 0;
    *outData = nullptr;
    *outSize = 0;
    if (outType) *outType = -1;

    ID3Picture pic{};
    const auto wpath = utf8PathToWide(path);
    if (!ID3Tags::GetPicture(wpath.c_str(), picIndex, pic) || !pic.data || pic.size == 0) {
        if (pic.data) delete[] pic.data;
        return 0;
    }

    void* copy = std::malloc(pic.size);
    if (!copy) {
        delete[] pic.data;
        return 0;
    }
    std::memcpy(copy, pic.data, pic.size);
    delete[] pic.data;

    *outData = static_cast<uint8_t*>(copy);
    *outSize = pic.size;
    if (outType) *outType = pic.type;
    return 1;
}

ZFORK_API void ZFORK_CALL zfork_FreeMemory(void* ptr) {
    std::free(ptr);
}

ZFORK_API int ZFORK_CALL zfork_ParseMusicalKey(const char* text, MusicalKeyInfo* out) {
    if (!out) return 0;
    return ParseMusicalKey(text, *out) ? 1 : 0;
}

ZFORK_API int ZFORK_CALL zfork_KeysCompatible(const MusicalKeyInfo* a, const MusicalKeyInfo* b) {
    if (!a || !b) return 0;
    return KeysHarmonicallyCompatible(*a, *b) ? 1 : 0;
}

ZFORK_API const char* ZFORK_CALL zfork_GetError(ZFORK_HANDLE h) {
    return cast(h)->GetError();
}

} // extern "C"
