#define LIBZFORK_BUILDING_DLL
#include <libzfork/libzfork2.h>

#include "engine/audio_context.h"
#include "engine/sound.h"
#include "engine/sound_cache.h"
#include "engine/soundbank_v1.h"
#include "engine/effect_chain.h"
#include "engine/command_queue.h"
#include "engine/engine_defs.h"
#include "output/audio_output.h"
#ifdef _WIN32
#  include "output/auto_output.h"
#endif
#include "decoders/decoder_factory.h"
#include "analysis/spectrum.h"
#include "utils/tags.h"

#include <memory>
#include <cstdint>
#include <cstring>

using namespace libZFork::engine;

// ── helpers ──────────────────────────────────────────────────────────────────

static inline AudioContext* AC(ZF2_CONTEXT ctx) {
    return static_cast<AudioContext*>(ctx);
}

static inline void PostCmd(AudioContext* ac, Command& c) noexcept {
    (void)ac->Post(c);
}

static inline Command MakeVoiceCmd(CommandType type, ZF2_VOICE voice) noexcept {
    Command c{};
    c.type    = type;
    c.voiceId = voice;
    return c;
}

static inline Command MakeGroupCmd(CommandType type, ZF2_GROUP group) noexcept {
    Command c{};
    c.type    = type;
    c.groupId = group;
    return c;
}

static inline void CopyZf2String(char* dst, size_t dstSize, const char* src) noexcept {
    if (!dst || dstSize == 0) return;
    dst[0] = '\0';
    if (!src) return;
    std::strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

static ZF2_VOICE PlaySharedSound(AudioContext* ac,
                                 const std::shared_ptr<libZFork::engine::Sound>& sound,
                                 ZF2_GROUP group,
                                 float volume,
                                 float pan,
                                 int priority,
                                 int loopCount) {
    if (!ac || !sound) return ZF2_INVALID_VOICE;

    uint32_t id = ac->AllocVoice(priority);
    if (id == kInvalidVoice) return ZF2_INVALID_VOICE;

    Command c{};
    c.type    = CommandType::PlayVoice;
    c.voiceId = id;
    c.groupId = (group == ZF2_INVALID_GROUP) ? 0u : group;
    c.ptr     = new std::shared_ptr<libZFork::engine::Sound>(sound);
    if (!ac->Post(c)) {
        delete static_cast<std::shared_ptr<libZFork::engine::Sound>*>(c.ptr);
        ac->CancelAllocatedVoice(id);
        return ZF2_INVALID_VOICE;
    }

    Command cv = MakeVoiceCmd(CommandType::SetVoiceVolume, id);
    cv.param.f[0] = volume;
    PostCmd(ac, cv);

    Command cp = MakeVoiceCmd(CommandType::SetVoicePan, id);
    cp.param.f[0] = pan;
    PostCmd(ac, cp);

    Command cprio = MakeVoiceCmd(CommandType::SetVoicePriority, id);
    cprio.param.i[0] = priority;
    PostCmd(ac, cprio);

    Command cloop = MakeVoiceCmd(CommandType::SetVoiceLoop, id);
    cloop.param.i[0] = loopCount;
    PostCmd(ac, cloop);

    return static_cast<ZF2_VOICE>(id);
}

// ── Context ───────────────────────────────────────────────────────────────────

ZF2_CONTEXT zfork2_CreateContext(void) {
    return zfork2_CreateContextWithBackend(ZF2_OUTPUT_AUTO);
}

ZF2_CONTEXT zfork2_CreateContextWithBackend(int backend) {
    auto output = libZFork::CreatePlatformAudioOutput(
        static_cast<libZFork::AudioBackendPreference>(backend));
    if (!output) return nullptr;

    // Default to 44.1 kHz for best compatibility with common music/sfx assets.
    auto* ctx = new AudioContext(std::move(output), 44100, 2);
    (void)ctx->RegisterGroup("master");
    (void)ctx->RegisterGroup("music");
    (void)ctx->RegisterGroup("sfx");
    (void)ctx->RegisterGroup("ui");
    return ctx;
}

void zfork2_DestroyContext(ZF2_CONTEXT ctx) {
    delete AC(ctx);
}

int zfork2_Start(ZF2_CONTEXT ctx) {
    if (!ctx) return 0;
    return AC(ctx)->Start() ? 1 : 0;
}

void zfork2_Stop(ZF2_CONTEXT ctx) {
    if (ctx) AC(ctx)->Stop();
}

ZF2_GROUP zfork2_RegisterGroup(ZF2_CONTEXT ctx, const char* name) {
    if (!ctx || !name) return ZF2_INVALID_GROUP;
    uint32_t idx = AC(ctx)->RegisterGroup(name);
    return idx == kInvalidGroup ? ZF2_INVALID_GROUP : idx;
}

ZF2_GROUP zfork2_GetGroup(ZF2_CONTEXT ctx, const char* name) {
    if (!ctx || !name) return ZF2_INVALID_GROUP;
    uint32_t idx = AC(ctx)->FindGroup(name);
    return idx == kInvalidGroup ? ZF2_INVALID_GROUP : idx;
}

void zfork2_SetCallback(ZF2_CONTEXT ctx, zfork2_EventCallback cb, void* userData) {
    // EventCallback and zfork2_EventCallback are binary-identical (same parameter
    // types after typedef expansion), so a static_cast is well-formed here.
    if (ctx) AC(ctx)->SetDefaultCallback(
        static_cast<EventCallback>(cb), userData);
}

void zfork2_Update(ZF2_CONTEXT ctx) {
    if (ctx) AC(ctx)->Update();
}

int zfork2_ProbeFile(const wchar_t* path, zfork2_FileProbe* outInfo) {
    if (!path || !outInfo) return 0;

    std::memset(outInfo, 0, sizeof(*outInfo));
    auto dec = libZFork::CreateDecoderForFile(path);
    if (!dec || !dec->OpenFile(path)) return 0;

    outInfo->sampleRate = dec->SampleRate();
    outInfo->channels   = dec->Channels();
    outInfo->bitrate    = dec->Bitrate();
    outInfo->vbr        = dec->IsVBR() ? 1 : 0;
    outInfo->lengthMs   = dec->GetLength().ms;

    libZFork::ID3InfoEx tag{};
    if (libZFork::ID3Tags::LoadExtended(path, tag)) {
        outInfo->bpmTag = (tag.bpm > 0) ? static_cast<float>(tag.bpm) : 0.0f;
        CopyZf2String(outInfo->title, sizeof(outInfo->title), tag.title);
        CopyZf2String(outInfo->artist, sizeof(outInfo->artist), tag.artist);
        CopyZf2String(outInfo->album, sizeof(outInfo->album), tag.album);
        CopyZf2String(outInfo->year, sizeof(outInfo->year), tag.year);
    }
    return 1;
}

int zfork2_DetectFileBPM(const wchar_t* path, int algo, float* outBpm) {
    if (!path || !outBpm) return 0;
    *outBpm = 0.0f;

    auto dec = libZFork::CreateDecoderForFile(path);
    if (!dec || !dec->OpenFile(path)) return 0;

    libZFork::SpectrumAnalyzer analyzer;
    const auto bpm = analyzer.DetectBPM(static_cast<libZFork::BPMAlgorithm>(algo), *dec);
    if (bpm <= 0.0f) return 0;
    *outBpm = bpm;
    return 1;
}

int zfork2_GetOutputBackendInfo(ZF2_CONTEXT ctx, char* outName, uint32_t nameCapacity,
                                uint32_t* outPeriodFrames) {
    if (!ctx) return 0;
    const auto* output = AC(ctx)->Output();

    const char* name = "Unknown";
    uint32_t    periodFrames = 0;
#ifdef _WIN32
    if (const auto* autoOut = dynamic_cast<const libZFork::AutoOutput*>(output)) {
        if (autoOut->IsUsingWasapi()) {
            name         = "WASAPI-Exclusive";
            periodFrames = autoOut->ActivePeriodFrames();
        } else {
            name = "WaveOut (MME)";
        }
    }
#endif
    CopyZf2String(outName, nameCapacity, name);
    if (outPeriodFrames) *outPeriodFrames = periodFrames;
    return 1;
}

// ── Sounds ────────────────────────────────────────────────────────────────────

ZF2_SOUND zfork2_LoadSound(ZF2_CONTEXT ctx, const wchar_t* path, int type) {
    if (!ctx || !path) return nullptr;
    SoundType st = (type == ZF2_SOUND_STREAMING) ? SoundType::Streaming
                                                  : SoundType::PreDecoded;
    auto snd = libZFork::engine::Sound::LoadFile(path, st);
    if (!snd) return nullptr;
    return new std::shared_ptr<libZFork::engine::Sound>(std::move(snd));
}

void zfork2_UnloadSound(ZF2_SOUND sound) {
    if (sound)
        delete static_cast<std::shared_ptr<libZFork::engine::Sound>*>(sound);
}

// ── Sound cache ────────────────────────────────────────────────────────────────

void zfork2_SetSoundCacheBudget(uint64_t budgetBytes) {
    SoundCache::Instance().SetBudgetBytes(budgetBytes);
}

uint64_t zfork2_GetSoundCacheBudget(void) {
    return SoundCache::Instance().BudgetBytes();
}

void zfork2_GetSoundCacheStats(uint64_t* outUsedBytes, uint32_t* outEntryCount) {
    if (outUsedBytes)   *outUsedBytes   = SoundCache::Instance().UsedBytes();
    if (outEntryCount)  *outEntryCount  = static_cast<uint32_t>(SoundCache::Instance().EntryCount());
}

void zfork2_ClearSoundCache(void) {
    SoundCache::Instance().Clear();
}

// ── Voices ────────────────────────────────────────────────────────────────────

ZF2_VOICE zfork2_PlaySound(ZF2_CONTEXT ctx, ZF2_SOUND sound, ZF2_GROUP group) {
    if (!ctx || !sound) return ZF2_INVALID_VOICE;

    auto* ac  = AC(ctx);
    auto* snd = static_cast<std::shared_ptr<libZFork::engine::Sound>*>(sound);
    if (!*snd) return ZF2_INVALID_VOICE;
    return PlaySharedSound(ac, *snd, group, 1.0f, 0.0f, 0, 0);
}

void zfork2_StopVoice(ZF2_CONTEXT ctx, ZF2_VOICE voice) {
    if (!ctx) return;
    Command c = MakeVoiceCmd(CommandType::StopVoice, voice);
    PostCmd(AC(ctx), c);
}

void zfork2_PauseVoice(ZF2_CONTEXT ctx, ZF2_VOICE voice) {
    if (!ctx) return;
    Command c = MakeVoiceCmd(CommandType::PauseVoice, voice);
    PostCmd(AC(ctx), c);
}

void zfork2_ResumeVoice(ZF2_CONTEXT ctx, ZF2_VOICE voice) {
    if (!ctx) return;
    Command c = MakeVoiceCmd(CommandType::ResumeVoice, voice);
    PostCmd(AC(ctx), c);
}

int zfork2_IsVoiceActive(ZF2_CONTEXT ctx, ZF2_VOICE voice) {
    if (!ctx) return 0;
    return AC(ctx)->IsVoiceActive(voice) ? 1 : 0;
}

void zfork2_SetVoiceVolume(ZF2_CONTEXT ctx, ZF2_VOICE voice, float volume) {
    if (!ctx) return;
    Command c = MakeVoiceCmd(CommandType::SetVoiceVolume, voice);
    c.param.f[0] = volume;
    PostCmd(AC(ctx), c);
}

void zfork2_SetVoicePan(ZF2_CONTEXT ctx, ZF2_VOICE voice, float pan) {
    if (!ctx) return;
    Command c = MakeVoiceCmd(CommandType::SetVoicePan, voice);
    c.param.f[0] = pan;
    PostCmd(AC(ctx), c);
}

void zfork2_SetVoicePitch(ZF2_CONTEXT ctx, ZF2_VOICE voice, float semitones) {
    if (!ctx) return;
    Command c = MakeVoiceCmd(CommandType::SetVoicePitch, voice);
    c.param.f[0] = semitones;
    PostCmd(AC(ctx), c);
}

void zfork2_SetVoiceTempo(ZF2_CONTEXT ctx, ZF2_VOICE voice, float tempo) {
    if (!ctx) return;
    if (tempo < 0.5f) tempo = 0.5f;
    if (tempo > 2.0f) tempo = 2.0f;
    Command c = MakeVoiceCmd(CommandType::SetVoiceTempo, voice);
    c.param.f[0] = tempo;
    PostCmd(AC(ctx), c);
}

void zfork2_SetVoiceLoop(ZF2_CONTEXT ctx, ZF2_VOICE voice, int loopCount) {
    if (!ctx) return;
    Command c = MakeVoiceCmd(CommandType::SetVoiceLoop, voice);
    c.param.i[0] = loopCount;
    PostCmd(AC(ctx), c);
}

void zfork2_SetVoicePriority(ZF2_CONTEXT ctx, ZF2_VOICE voice, int priority) {
    if (!ctx) return;
    Command c = MakeVoiceCmd(CommandType::SetVoicePriority, voice);
    c.param.i[0] = priority;
    PostCmd(AC(ctx), c);
}

void zfork2_SetVoicePosition2D(ZF2_CONTEXT ctx, ZF2_VOICE voice, float x, float y) {
    if (!ctx) return;
    Command c = MakeVoiceCmd(CommandType::SetVoicePosition2D, voice);
    c.param.f[0] = x;
    c.param.f[1] = y;
    PostCmd(AC(ctx), c);
}

void zfork2_SetVoicePosition3D(ZF2_CONTEXT ctx, ZF2_VOICE voice, float x, float y, float z) {
    if (!ctx) return;
    Command c = MakeVoiceCmd(CommandType::SetVoicePosition3D, voice);
    c.param.f[0] = x;
    c.param.f[1] = y;
    c.param.f[2] = z;
    PostCmd(AC(ctx), c);
}

void zfork2_SetVoiceMarker(ZF2_CONTEXT ctx, ZF2_VOICE voice,
                            uint32_t markerId, float posSeconds) {
    if (!ctx) return;
    Command c = MakeVoiceCmd(CommandType::SetVoiceMarker, voice);
    c.param.i[0] = static_cast<int32_t>(markerId);
    c.param.f[1] = posSeconds;
    PostCmd(AC(ctx), c);
}

void zfork2_ClearVoiceMarkers(ZF2_CONTEXT ctx, ZF2_VOICE voice) {
    if (!ctx) return;
    Command c = MakeVoiceCmd(CommandType::ClearVoiceMarkers, voice);
    PostCmd(AC(ctx), c);
}

// ── Soundbank V1 ──────────────────────────────────────────────────────────────

uint32_t zfork2_GetBankEntryCount(ZF2_BANK bank) {
    if (!bank) return 0u;
    return static_cast<uint32_t>(
        static_cast<SoundBankV1*>(bank)->EntryCount());
}

uint32_t zfork2_GetBankSkippedCount(ZF2_BANK bank) {
    if (!bank) return 0u;
    return static_cast<SoundBankV1*>(bank)->SkippedCount();
}

int zfork2_GetBankEventName(ZF2_BANK bank, uint32_t index, char* outName, uint32_t nameCapacity) {
    if (outName && nameCapacity > 0) outName[0] = '\0';
    if (!bank) return 0;
    const auto* entry = static_cast<SoundBankV1*>(bank)->EntryAt(index);
    if (!entry) return 0;
    CopyZf2String(outName, nameCapacity, entry->eventId.c_str());
    return 1;
}

ZF2_BANK zfork2_LoadBankV1(ZF2_CONTEXT ctx, const wchar_t* path) {
    if (!ctx || !path) return nullptr;
    auto bank = SoundBankV1::LoadFromFile(*AC(ctx), path);
    if (!bank) return nullptr;
    return bank.release();
}

void zfork2_UnloadBankV1(ZF2_BANK bank) {
    delete static_cast<SoundBankV1*>(bank);
}

int zfork2_CompileBankV1(const wchar_t* textPath, const wchar_t* binaryPath) {
    if (!textPath || !binaryPath) return 0;
    return SoundBankV1::ConvertTextToBinary(textPath, binaryPath, false) ? 1 : 0;
}

int zfork2_CompileBankV1Embedded(const wchar_t* textPath, const wchar_t* binaryPath) {
    if (!textPath || !binaryPath) return 0;
    return SoundBankV1::ConvertTextToBinary(textPath, binaryPath, true) ? 1 : 0;
}

ZF2_VOICE zfork2_PlayEventV1(ZF2_CONTEXT ctx, ZF2_BANK bank, const char* eventId) {
    if (!ctx || !bank || !eventId) return ZF2_INVALID_VOICE;
    auto* ac = AC(ctx);
    auto* sb = static_cast<SoundBankV1*>(bank);
    const auto* e = sb->Find(eventId);
    if (!e) return ZF2_INVALID_VOICE;
    return PlaySharedSound(ac, e->sound, e->groupId, e->volume, e->pan, e->priority, e->loopCount);
}

ZF2_VOICE zfork2_PlayEventOnGroupV1(ZF2_CONTEXT ctx, ZF2_BANK bank,
                                    const char* eventId, ZF2_GROUP group) {
    if (!ctx || !bank || !eventId) return ZF2_INVALID_VOICE;
    auto* ac = AC(ctx);
    auto* sb = static_cast<SoundBankV1*>(bank);
    const auto* e = sb->Find(eventId);
    if (!e) return ZF2_INVALID_VOICE;
    ZF2_GROUP useGroup = (group == ZF2_INVALID_GROUP) ? e->groupId : group;
    return PlaySharedSound(ac, e->sound, useGroup, e->volume, e->pan, e->priority, e->loopCount);
}

// ── Groups ────────────────────────────────────────────────────────────────────

void zfork2_SetGroupVolume(ZF2_CONTEXT ctx, ZF2_GROUP group, float volume) {
    if (!ctx || group == ZF2_INVALID_GROUP) return;
    Command c = MakeGroupCmd(CommandType::SetGroupVolume, group);
    c.param.f[0] = volume;
    PostCmd(AC(ctx), c);
}

void zfork2_SetGroupMute(ZF2_CONTEXT ctx, ZF2_GROUP group, int muted) {
    if (!ctx || group == ZF2_INVALID_GROUP) return;
    Command c = MakeGroupCmd(CommandType::SetGroupMute, group);
    c.param.i[0] = muted;
    PostCmd(AC(ctx), c);
}

void zfork2_GetGroupLevels(ZF2_CONTEXT ctx, ZF2_GROUP group,
                           float* outLeft, float* outRight) {
    if (!outLeft || !outRight) return;
    *outLeft = *outRight = 0.f;
    if (!ctx || group == ZF2_INVALID_GROUP) return;
    AC(ctx)->GetGroupLevels(group, *outLeft, *outRight);
}

// ── Group effects ─────────────────────────────────────────────────────────────

void zfork2_SetGroupEcho(ZF2_CONTEXT ctx, ZF2_GROUP group, const zfork2_EchoParams* params) {
    if (!ctx || group == ZF2_INVALID_GROUP || !params) return;

    auto* fx = new EchoEffectWrapper();
    libZFork::EchoParams p{};
    p.leftDelay      = params->leftDelayMs;
    p.leftSourceVol  = params->leftSourceVolume;
    p.leftEchoVol    = params->leftEchoVolume;
    p.rightDelay     = params->rightDelayMs;
    p.rightSourceVol = params->rightSourceVolume;
    p.rightEchoVol   = params->rightEchoVolume;
    fx->SetParams(p);

    AC(ctx)->SetGroupEffect(group, kEchoEffectSlot, fx);
}

void zfork2_SetGroupReverb(ZF2_CONTEXT ctx, ZF2_GROUP group, const zfork2_ReverbParams* params) {
    if (!ctx || group == ZF2_INVALID_GROUP || !params) return;

    auto* fx = new ReverbEffectWrapper();
    libZFork::ReverbParams p{};
    p.roomSize = params->roomSize;
    p.damping  = params->damping;
    p.wet      = params->wet;
    p.dry      = params->dry;
    fx->SetParams(p);

    AC(ctx)->SetGroupEffect(group, kReverbEffectSlot, fx);
}

void zfork2_ClearGroupEffect(ZF2_CONTEXT ctx, ZF2_GROUP group, int slot) {
    if (!ctx || group == ZF2_INVALID_GROUP) return;
    AC(ctx)->ClearGroupEffect(group, static_cast<uint32_t>(slot));
}

// ── Listener ──────────────────────────────────────────────────────────────────

void zfork2_SetListenerPosition2D(ZF2_CONTEXT ctx, float x, float y) {
    if (!ctx) return;
    Command c{};
    c.type       = CommandType::SetListenerPosition2D;
    c.param.f[0] = x;
    c.param.f[1] = y;
    PostCmd(AC(ctx), c);
}

void zfork2_SetListenerPosition3D(ZF2_CONTEXT ctx, float x, float y, float z) {
    if (!ctx) return;
    Command c{};
    c.type       = CommandType::SetListenerPosition3D;
    c.param.f[0] = x;
    c.param.f[1] = y;
    c.param.f[2] = z;
    PostCmd(AC(ctx), c);
}

void zfork2_SetListenerDirection(ZF2_CONTEXT ctx, float dx, float dy, float dz) {
    if (!ctx) return;
    Command c{};
    c.type       = CommandType::SetListenerDirection;
    c.param.f[0] = dx;
    c.param.f[1] = dy;
    c.param.f[2] = dz;
    PostCmd(AC(ctx), c);
}

void zfork2_SetListenerRange(ZF2_CONTEXT ctx,
                              float minDist, float maxDist, float rolloff) {
    if (!ctx) return;
    Command c{};
    c.type       = CommandType::SetListenerRange;
    c.param.f[0] = minDist;
    c.param.f[1] = maxDist;
    c.param.f[2] = rolloff;
    PostCmd(AC(ctx), c);
}
