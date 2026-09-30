/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║  libZFork v2 — Game Audio Engine                                        ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║  C API for multi-voice, group-based, spatially-aware game audio.        ║
 * ║  Compatible with: C99+, C++11+, any caller of libZFork.dll/.lib.        ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 *
 * ── Overview ────────────────────────────────────────────────────────────────
 *
 *  ZF2_CONTEXT   One audio engine instance (mixer + output + event queue).
 *  ZF2_SOUND     A loaded audio asset (pre-decoded in RAM or streaming).
 *  ZF2_VOICE     A playing instance of a sound.  Multiple voices can play
 *                the same ZF2_SOUND simultaneously.
 *  ZF2_GROUP     A named mix bus (volume, mute, VU levels).
 *                Built-in groups: "master", "music", "sfx", "ui".
 *  ZF2_BANK      A sound-bank: maps string event IDs to sounds + defaults.
 *
 * ── Minimal setup ───────────────────────────────────────────────────────────
 *
 *   ZF2_CONTEXT ctx = zfork2_CreateContext();
 *   zfork2_Start(ctx);
 *
 *   ZF2_SOUND snd  = zfork2_LoadSound(ctx, L"explosion.wav", ZF2_SOUND_PREDECODED);
 *   ZF2_GROUP sfx  = zfork2_GetGroup(ctx, "sfx");
 *   ZF2_VOICE id   = zfork2_PlaySound(ctx, snd, sfx);
 *
 *   // Game loop — call once per frame on your update thread:
 *   zfork2_Update(ctx);
 *
 *   // Cleanup:
 *   zfork2_UnloadSound(snd);
 *   zfork2_Stop(ctx);
 *   zfork2_DestroyContext(ctx);
 *
 * ── Groups ──────────────────────────────────────────────────────────────────
 *
 *   // Built-ins are created automatically; add your own before Start():
 *   ZF2_GROUP amb = zfork2_RegisterGroup(ctx, "ambient");
 *
 *   zfork2_SetGroupVolume(ctx, zfork2_GetGroup(ctx, "music"), 0.6f);
 *   zfork2_SetGroupMute  (ctx, zfork2_GetGroup(ctx, "sfx"),  1);
 *
 *   // Read live stereo peak levels (e.g. for a VU meter):
 *   float l, r;
 *   zfork2_GetGroupLevels(ctx, zfork2_GetGroup(ctx, "master"), &l, &r);
 *
 * ── Multi-voice playback ─────────────────────────────────────────────────────
 *
 *   // Fire-and-forget:
 *   zfork2_PlaySound(ctx, snd, sfx);
 *
 *   // Keep the id to control the voice later:
 *   ZF2_VOICE v = zfork2_PlaySound(ctx, music, zfork2_GetGroup(ctx, "music"));
 *   zfork2_SetVoiceVolume  (ctx, v, 0.8f);
 *   zfork2_SetVoicePan     (ctx, v, -0.3f);  // slightly left
 *   zfork2_PauseVoice      (ctx, v);
 *   zfork2_ResumeVoice     (ctx, v);
 *   zfork2_StopVoice       (ctx, v);
 *
 *   // Looping:
 *   zfork2_SetVoiceLoop(ctx, v, -1);   // infinite
 *   zfork2_SetVoiceLoop(ctx, v,  3);   // 3 more repeats, then stop
 *   zfork2_SetVoiceLoop(ctx, v,  0);   // disable looping
 *
 * ── Pitch and Tempo (SoundTouch) ────────────────────────────────────────────
 *
 *   // Shift pitch without changing speed (key change):
 *   zfork2_SetVoicePitch(ctx, v, +2.0f);   // two semitones up
 *   zfork2_SetVoicePitch(ctx, v, -5.0f);   // five semitones down
 *
 *   // Change playback speed without changing pitch (DJ key-lock / time-stretch):
 *   zfork2_SetVoiceTempo(ctx, v, 1.10f);   // 10 % faster
 *   zfork2_SetVoiceTempo(ctx, v, 0.85f);   // 15 % slower
 *
 * ── Positional audio ─────────────────────────────────────────────────────────
 *
 *   // Place the listener in world space:
 *   zfork2_SetListenerPosition2D(ctx, player.x, player.y);
 *   zfork2_SetListenerDirection (ctx, dir.x, dir.y, 0.0f);
 *   zfork2_SetListenerRange     (ctx, 1.0f, 50.0f, 1.5f); // min, max, rolloff
 *
 *   // Place an emitter — pan + gain computed automatically:
 *   ZF2_VOICE footstep = zfork2_PlaySound(ctx, snd, sfx);
 *   zfork2_SetVoicePosition2D(ctx, footstep, npc.x, npc.y);
 *
 *   // Full 3-D:
 *   zfork2_SetListenerPosition3D(ctx, cam.x, cam.y, cam.z);
 *   zfork2_SetVoicePosition3D   (ctx, footstep, npc.x, npc.y, npc.z);
 *
 * ── Events and callbacks ─────────────────────────────────────────────────────
 *
 *   void OnAudioEvent(int type, ZF2_VOICE v, uint32_t p1, float p2, void* ud) {
 *       switch (type) {
 *       case ZF2_EVENT_VOICE_START:      // v just started playing
 *       case ZF2_EVENT_VOICE_FINISHED:   // v stopped (end of file, StopVoice, etc.)
 *       case ZF2_EVENT_VOICE_LOOP:       // v looped; p1 = loops remaining
 *       case ZF2_EVENT_POSITION_CROSSED: // marker hit; p1 = markerId, p2 = posSeconds
 *       }
 *   }
 *   zfork2_SetCallback(ctx, OnAudioEvent, myUserData);
 *
 *   // Add a cue-point marker on a voice:
 *   zfork2_SetVoiceMarker(ctx, v, 42, 3.750f); // marker id=42 at 3.75 s
 *
 *   // zfork2_Update() must be called on your game/update thread to dispatch
 *   // the callbacks — they are NEVER called from the audio thread:
 *   zfork2_Update(ctx);
 *
 * ── Sound banks ──────────────────────────────────────────────────────────────
 *
 *   // Bank file format (one entry per line, '#' = comment):
 *   //   eventId ; path ; group ; type ; volume ; pan ; priority ; loop
 *   //   boom    ; sfx/boom.wav ; sfx ; predecoded ; 1.0 ; 0.0 ; 5 ; 0
 *
 *   ZF2_BANK bank = zfork2_LoadBankV1(ctx, L"sounds.bank");
 *   printf("%u events loaded, %u skipped\n",
 *          zfork2_GetBankEntryCount(bank),
 *          zfork2_GetBankSkippedCount(bank));
 *
 *   ZF2_VOICE v = zfork2_PlayEventV1(ctx, bank, "boom");
 *   // Override group at play time:
 *   zfork2_PlayEventOnGroupV1(ctx, bank, "boom", zfork2_GetGroup(ctx, "ui"));
 *
 *   zfork2_UnloadBankV1(bank);
 *
 * ── File utilities (no engine required) ─────────────────────────────────────
 *
 *   zfork2_FileProbe info;
 *   if (zfork2_ProbeFile(L"track.mp3", &info))
 *       printf("%s — %d Hz, %u ms\n", info.title, info.sampleRate, info.lengthMs);
 *
 *   float bpm = 0.f;
 *   zfork2_DetectFileBPM(L"track.mp3", ZF2_BPM_AUTOCORRELATION, &bpm);
 *
 * ── Threading rules ──────────────────────────────────────────────────────────
 *
 *   • All zfork2_* calls (except zfork2_Update) are safe to call from any
 *     thread simultaneously.
 *   • zfork2_Update MUST be called from a single thread (your game loop).
 *   • The audio render thread is managed internally; do not call into the
 *     API from inside an event callback.
 *   • ZF2_SOUND ref-counting is thread-safe; UnloadSound is safe even while
 *     voices are playing the asset.
 */

#ifndef LIBZFORK2_H
#define LIBZFORK2_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── DLL visibility ──────────────────────────────────────────────────────── */
#if defined(_WIN32)
#  if defined(LIBZFORK_BUILDING_DLL)
#    define ZF2_API __declspec(dllexport)
#  elif defined(LIBZFORK_DLL)
#    define ZF2_API __declspec(dllimport)
#  else
#    define ZF2_API
#  endif
#else
#  define ZF2_API
#endif

/* ── Opaque handle types ─────────────────────────────────────────────────── */
typedef void*    ZF2_CONTEXT; /* AudioContext*  */
typedef void*    ZF2_SOUND;   /* shared_ptr<Sound>* — ref-counted asset */
typedef void*    ZF2_BANK;    /* SoundBankV1* */
typedef uint32_t ZF2_VOICE;   /* generation-stamped slot id; 0 = invalid */
typedef uint32_t ZF2_GROUP;   /* group index; 0xFFFFFFFF = invalid */

#define ZF2_INVALID_VOICE  ((ZF2_VOICE)0u)
#define ZF2_INVALID_GROUP  ((ZF2_GROUP)0xFFFFFFFFu)

/* ── Sound type ──────────────────────────────────────────────────────────── */
#define ZF2_SOUND_PREDECODED 0  /* fully decoded into RAM on load */
#define ZF2_SOUND_STREAMING  1  /* opened fresh per voice */

/* ── BPM algorithms (for zfork2_DetectFileBPM) ──────────────────────────── */
#define ZF2_BPM_PEAKS         0
#define ZF2_BPM_AUTOCORRELATION 1
#define ZF2_BPM_ADVANCED      2

/* ── File probe output (for zfork2_ProbeFile) ───────────────────────────── */
typedef struct zfork2_FileProbe {
    int      sampleRate;
    int      channels;
    int      bitrate;
    int      vbr;        /* 0/1 */
    uint32_t lengthMs;
    float    bpmTag;     /* 0 if not present in metadata */
    char     title[256];
    char     artist[256];
    char     album[256];
    char     year[16];
} zfork2_FileProbe;

/* ── Event types (passed to zfork2_EventCallback) ───────────────────────── */
#define ZF2_EVENT_VOICE_START      0
#define ZF2_EVENT_VOICE_FINISHED   1
#define ZF2_EVENT_VOICE_LOOP       2
#define ZF2_EVENT_POSITION_CROSSED 3

/* ── Event callback ──────────────────────────────────────────────────────── */
/*   eventType  — one of ZF2_EVENT_*                                         */
/*   voice      — stamped voice id                                            */
/*   param1     — loops remaining (VoiceLoop), markerId (PositionCrossed)    */
/*   param2     — position in seconds (PositionCrossed)                       */
/*   userData   — passed through from zfork2_SetCallback                     */
typedef void (*zfork2_EventCallback)(int eventType, ZF2_VOICE voice,
                                     uint32_t param1, float param2,
                                     void* userData);

/* ══════════════════════════════════════════════════════════════════════════ */
/*  Context                                                                   */
/* ══════════════════════════════════════════════════════════════════════════ */

/* Create an AudioContext.  Pre-registers groups "master", "music", "sfx", "ui".
   Returns NULL on failure. */
ZF2_API ZF2_CONTEXT zfork2_CreateContext(void);

/* Output backend selection for zfork2_CreateContextWithBackend(). */
#define ZF2_OUTPUT_AUTO    0  /* try WASAPI-Exclusive, fall back to WaveOut  */
#define ZF2_OUTPUT_WASAPI  1  /* force WASAPI-Exclusive; fails if unusable  */
#define ZF2_OUTPUT_WAVEOUT 2  /* force classic WaveOut/MME                  */

/* Same as zfork2_CreateContext(), but lets you pick the audio output
   backend explicitly instead of the default ZF2_OUTPUT_AUTO. Use
   ZF2_OUTPUT_WASAPI to require the low-latency exclusive-mode path (fails
   loudly instead of silently downgrading — useful to detect environments
   where it isn't available), or ZF2_OUTPUT_WAVEOUT for maximum
   compatibility. Returns NULL on failure. */
ZF2_API ZF2_CONTEXT zfork2_CreateContextWithBackend(int backend);

/* Destroy a context (calls Stop internally if still running). */
ZF2_API void        zfork2_DestroyContext(ZF2_CONTEXT ctx);

/* Start the engine thread and open the audio output.
   Returns 1 on success, 0 on failure. */
ZF2_API int         zfork2_Start(ZF2_CONTEXT ctx);

/* Stop the engine thread.  All active voices are silenced. */
ZF2_API void        zfork2_Stop(ZF2_CONTEXT ctx);

/* Register a named mix group.  Must be called before zfork2_Start.
   Returns the group index or ZF2_INVALID_GROUP if limit is reached. */
ZF2_API ZF2_GROUP   zfork2_RegisterGroup(ZF2_CONTEXT ctx, const char* name);

/* Look up a registered group by name.  Thread-safe after construction. */
ZF2_API ZF2_GROUP   zfork2_GetGroup(ZF2_CONTEXT ctx, const char* name);

/* Store a default event callback used by zfork2_Update(). */
ZF2_API void        zfork2_SetCallback(ZF2_CONTEXT ctx,
                                        zfork2_EventCallback cb,
                                        void* userData);

/* Dispatch all pending engine events to the stored callback.
   Call once per game frame from your update/main thread (not the audio thread). */
ZF2_API void        zfork2_Update(ZF2_CONTEXT ctx);

/* Probe audio file properties and basic ID3 metadata without creating a
   zfork1 Player instance.
   Returns 1 on success, 0 on failure. */
ZF2_API int         zfork2_ProbeFile(const wchar_t* path, zfork2_FileProbe* outInfo);

/* Detect BPM from a file without creating a zfork1 Player instance.
   algo: ZF2_BPM_*.
   Returns 1 on success, 0 on failure. */
ZF2_API int         zfork2_DetectFileBPM(const wchar_t* path, int algo, float* outBpm);

/* Diagnostics: which IAudioOutput backend actually ended up active for this
   context (e.g. "WASAPI-Exclusive" or "WaveOut (MME)" — the engine tries
   WASAPI exclusive/event-driven mode first for the lowest possible
   latency and transparently falls back to WaveOut if that fails to
   initialise, e.g. the device is already held exclusively by another app).
   outPeriodFrames receives the actual device period in frames for WASAPI
   (0 if not applicable/available). Returns 1 on success, 0 if ctx is NULL. */
ZF2_API int         zfork2_GetOutputBackendInfo(ZF2_CONTEXT ctx, char* outName,
                                                uint32_t nameCapacity,
                                                uint32_t* outPeriodFrames);

/* ══════════════════════════════════════════════════════════════════════════ */
/*  Sounds                                                                    */
/* ══════════════════════════════════════════════════════════════════════════ */

/* Load a sound from a file path (Unicode).
   type: ZF2_SOUND_PREDECODED or ZF2_SOUND_STREAMING.
   Returns NULL on failure. */
ZF2_API ZF2_SOUND   zfork2_LoadSound(ZF2_CONTEXT ctx,
                                      const wchar_t* path,
                                      int            type);

/* Release a sound handle.  Voices currently playing the sound continue until
   they finish; the underlying PCM data is freed when the last voice completes. */
ZF2_API void        zfork2_UnloadSound(ZF2_SOUND sound);

/* ══════════════════════════════════════════════════════════════════════════ */
/*  Sound cache (process-wide LRU for pre-decoded sounds)                    */
/* ══════════════════════════════════════════════════════════════════════════ */

/* SoundBankV1 (and any other code loading a PreDecoded sound by path) goes
   through a shared LRU cache: loading the same file twice — e.g. reloading
   a bank, or two banks sharing a UI sound — decodes it from disk only once.
   Streaming sounds are never cached (they hold no PCM data to reuse).

   budgetBytes = 0 (the default) is "dynamic": the cache never evicts on its
   own and simply grows to fit whatever has actually been loaded — a small
   bank needs no manual sizing at all. Pass a nonzero byte budget to cap
   memory use for large sound libraries; least-recently-used entries are
   evicted once the total exceeds it. An evicted sound still held elsewhere
   (e.g. a playing voice) stays alive until that reference goes away. */
ZF2_API void        zfork2_SetSoundCacheBudget(uint64_t budgetBytes);
ZF2_API uint64_t    zfork2_GetSoundCacheBudget(void);

/* Current cache occupancy — useful for tuning zfork2_SetSoundCacheBudget(). */
ZF2_API void        zfork2_GetSoundCacheStats(uint64_t* outUsedBytes, uint32_t* outEntryCount);

/* Drops every cached sound (used only by other code via shared_ptr keeps it
   alive until those references go away). */
ZF2_API void        zfork2_ClearSoundCache(void);

/* ══════════════════════════════════════════════════════════════════════════ */
/*  Soundbank V1                                                             */
/* ══════════════════════════════════════════════════════════════════════════ */

/* Load a soundbank (UTF-16 file path). Both the text format and the
   compiled binary format (see zfork2_CompileBankV1) are accepted; the
   format is auto-detected from the file's contents.
   Text format, one entry per line:
   eventId;path;group;type;volume;pan;priority;loop
   type = predecoded|streaming
   Lines starting with '#' are comments.
   Returns NULL on failure. */
ZF2_API ZF2_BANK    zfork2_LoadBankV1(ZF2_CONTEXT ctx, const wchar_t* path);

/* Compiles a text-format bank file into the compact binary format (magic
   "ZFBK") that zfork2_LoadBankV1 also accepts. Does not need a context and
   does not load any sound files — it only re-serializes the parsed fields
   (paths stay external references). Returns 1 on success, 0 on failure
   (bad textPath or unwritable binaryPath). */
ZF2_API int         zfork2_CompileBankV1(const wchar_t* textPath, const wchar_t* binaryPath);

/* Same as zfork2_CompileBankV1, but also reads every referenced sound file
   (relative to textPath's directory) and embeds its raw bytes into the
   .bank — the result is a single self-contained file, no external
   sounds/*.ogg /*.wav folder needs to ship alongside it. A file that can't
   be read is left as an external path reference instead of failing the
   whole compile. Returns 1 on success, 0 on failure. */
ZF2_API int         zfork2_CompileBankV1Embedded(const wchar_t* textPath, const wchar_t* binaryPath);

/* Destroy a bank handle and release all loaded bank sounds. */
ZF2_API void        zfork2_UnloadBankV1(ZF2_BANK bank);

/* Returns the number of events that were successfully loaded from the bank.
   If this is 0 after zfork2_LoadBankV1 succeeds (non-null), all entries were
   skipped — check your file paths and supported audio formats.
   Returns 0 for a NULL bank. */
ZF2_API uint32_t    zfork2_GetBankEntryCount(ZF2_BANK bank);

/* Returns the number of entries in the bank file that were parsed but whose
   sound files could not be loaded (missing file, unsupported format, etc.).
   Useful for diagnosing why zfork2_GetBankEntryCount() is less than expected.
   Returns 0 for a NULL bank. */
ZF2_API uint32_t    zfork2_GetBankSkippedCount(ZF2_BANK bank);

/* Enumeration — for tooling/UI that wants to show what's actually in a
   loaded bank instead of assuming fixed, hand-known event ids (e.g. a bank
   generated by the bank_builder example). index is [0, GetBankEntryCount).
   Returns 1 on success, 0 if bank/index/outName are invalid (outName is
   left as an empty string on failure so callers can printf it unconditionally). */
ZF2_API int         zfork2_GetBankEventName(ZF2_BANK bank, uint32_t index,
                                            char* outName, uint32_t nameCapacity);

/* Play an event from bank defaults (group/volume/pan/priority/loop from file). */
ZF2_API ZF2_VOICE   zfork2_PlayEventV1(ZF2_CONTEXT ctx, ZF2_BANK bank, const char* eventId);

/* Play an event while overriding only the output group. */
ZF2_API ZF2_VOICE   zfork2_PlayEventOnGroupV1(ZF2_CONTEXT ctx, ZF2_BANK bank,
                                              const char* eventId, ZF2_GROUP group);

/* ══════════════════════════════════════════════════════════════════════════ */
/*  Voices                                                                    */
/* ══════════════════════════════════════════════════════════════════════════ */

/* Play a sound.  Returns a stamped voice id (ZF2_INVALID_VOICE on failure).
   The id is valid from this call until ZF2_EVENT_VOICE_FINISHED is dispatched. */
ZF2_API ZF2_VOICE   zfork2_PlaySound(ZF2_CONTEXT ctx,
                                      ZF2_SOUND   sound,
                                      ZF2_GROUP   group);

ZF2_API void        zfork2_StopVoice  (ZF2_CONTEXT, ZF2_VOICE);
ZF2_API void        zfork2_PauseVoice (ZF2_CONTEXT, ZF2_VOICE);
ZF2_API void        zfork2_ResumeVoice(ZF2_CONTEXT, ZF2_VOICE);

/* Returns 1 if the voice is still active (Playing or Paused), 0 otherwise. */
ZF2_API int         zfork2_IsVoiceActive(ZF2_CONTEXT, ZF2_VOICE);

/* volume: 0.0 – 1.0 (default 1.0) */
ZF2_API void        zfork2_SetVoiceVolume  (ZF2_CONTEXT, ZF2_VOICE, float volume);
/* pan: -1.0 (left) – 0 (center) – +1.0 (right) */
ZF2_API void        zfork2_SetVoicePan     (ZF2_CONTEXT, ZF2_VOICE, float pan);
/* pitch: semitones, changes key (SoundTouch pitch, tempo unchanged) */
ZF2_API void        zfork2_SetVoicePitch   (ZF2_CONTEXT, ZF2_VOICE, float semitones);
/* tempo: 0.5–2.0, changes speed without pitch change (key-lock / DJ sync) */
ZF2_API void        zfork2_SetVoiceTempo   (ZF2_CONTEXT, ZF2_VOICE, float tempo);
/* loopCount: 0 = no loop, -1 = infinite, N = loop N more times */
ZF2_API void        zfork2_SetVoiceLoop    (ZF2_CONTEXT, ZF2_VOICE, int loopCount);
ZF2_API void        zfork2_SetVoicePriority(ZF2_CONTEXT, ZF2_VOICE, int priority);

/* Enable positional audio for this voice. */
ZF2_API void        zfork2_SetVoicePosition2D(ZF2_CONTEXT, ZF2_VOICE, float x, float y);
ZF2_API void        zfork2_SetVoicePosition3D(ZF2_CONTEXT, ZF2_VOICE, float x, float y, float z);

/* Add a position marker.  ZF2_EVENT_POSITION_CROSSED is fired once per marker
   when playback passes posSeconds.  markerId is user-assigned. */
ZF2_API void        zfork2_SetVoiceMarker    (ZF2_CONTEXT, ZF2_VOICE,
                                               uint32_t markerId, float posSeconds);
ZF2_API void        zfork2_ClearVoiceMarkers (ZF2_CONTEXT, ZF2_VOICE);

/* ══════════════════════════════════════════════════════════════════════════ */
/*  Groups                                                                    */
/* ══════════════════════════════════════════════════════════════════════════ */

/* volume: 0.0 – 1.0 */
ZF2_API void        zfork2_SetGroupVolume(ZF2_CONTEXT, ZF2_GROUP, float volume);
/* muted: 0 = unmuted, 1 = muted */
ZF2_API void        zfork2_SetGroupMute  (ZF2_CONTEXT, ZF2_GROUP, int muted);

/* Read post-fader stereo peak levels for a group (0.0 – 1.0). */
ZF2_API void        zfork2_GetGroupLevels(ZF2_CONTEXT, ZF2_GROUP,
                                          float* outLeft, float* outRight);

/* ══════════════════════════════════════════════════════════════════════════ */
/*  Group effects (echo, reverb)                                             */
/* ══════════════════════════════════════════════════════════════════════════ */
/*
 *   Every voice routed through a group is processed by that group's effect
 *   chain (up to ZF2_MAX_EFFECT_SLOTS slots) once per mix block, on the
 *   engine thread — same real-time budget as the mixer itself (see
 *   engine_defs.h::kMixFrames), so both effects below are written to be
 *   cheap: fixed-size delay lines sized once, no per-sample allocation,
 *   O(1) work per filter per sample.
 *
 *   ZF2_GROUP music = zfork2_GetGroup(ctx, "music");
 *   zfork2_ReverbParams rv{ 0.6f, 0.5f, 0.35f, 0.7f };
 *   zfork2_SetGroupReverb(ctx, music, &rv);
 *   ...
 *   zfork2_ClearGroupEffect(ctx, music, ZF2_EFFECT_REVERB);
 */

#define ZF2_MAX_EFFECT_SLOTS 4   /* must match engine::kMaxEffectSlots       */
#define ZF2_EFFECT_ECHO      0   /* slot index for zfork2_SetGroupEcho       */
#define ZF2_EFFECT_REVERB    1   /* slot index for zfork2_SetGroupReverb     */

typedef struct zfork2_EchoParams {
    int leftDelayMs;       /* delay time, left channel (ms)                  */
    int leftSourceVolume;  /* 0-100: how much of the dry left signal passes  */
    int leftEchoVolume;    /* 0-100: feedback/repeat level, left channel     */
    int rightDelayMs;      /* delay time, right channel (ms)                 */
    int rightSourceVolume; /* 0-100: how much of the dry right signal passes */
    int rightEchoVolume;   /* 0-100: feedback/repeat level, right channel    */
} zfork2_EchoParams;

typedef struct zfork2_ReverbParams {
    float roomSize; /* 0.0-1.0: larger = longer decay (comb feedback amount) */
    float damping;  /* 0.0-1.0: larger = darker / more HF absorption         */
    float wet;      /* 0.0-1.0: reverberated signal level                    */
    float dry;      /* 0.0-1.0: unprocessed signal level                     */
} zfork2_ReverbParams;

/* Installs/replaces the echo effect on group's ZF2_EFFECT_ECHO slot.
   Takes effect from the next mix block; safe to call at any time (thread-safe,
   posted through the same command queue as PlaySound etc). */
ZF2_API void        zfork2_SetGroupEcho  (ZF2_CONTEXT ctx, ZF2_GROUP group,
                                          const zfork2_EchoParams* params);

/* Installs/replaces the reverb effect on group's ZF2_EFFECT_REVERB slot. */
ZF2_API void        zfork2_SetGroupReverb(ZF2_CONTEXT ctx, ZF2_GROUP group,
                                          const zfork2_ReverbParams* params);

/* Removes whatever effect currently occupies `slot` (ZF2_EFFECT_* or any
   index < ZF2_MAX_EFFECT_SLOTS) on `group`. No-op if the slot is empty. */
ZF2_API void        zfork2_ClearGroupEffect(ZF2_CONTEXT ctx, ZF2_GROUP group, int slot);

/* ══════════════════════════════════════════════════════════════════════════ */
/*  Listener (positional audio)                                               */
/* ══════════════════════════════════════════════════════════════════════════ */

ZF2_API void        zfork2_SetListenerPosition2D(ZF2_CONTEXT, float x, float y);
ZF2_API void        zfork2_SetListenerPosition3D(ZF2_CONTEXT, float x, float y, float z);
/* direction: normalised forward vector */
ZF2_API void        zfork2_SetListenerDirection (ZF2_CONTEXT, float dx, float dy, float dz);
/* minDist: gain=1 inside; maxDist: gain=0 beyond; rolloff: exponent */
ZF2_API void        zfork2_SetListenerRange     (ZF2_CONTEXT,
                                                  float minDist, float maxDist,
                                                  float rolloff);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* LIBZFORK2_H */
