;
; libZFork2 — PureBasic Bindings (Game Audio Engine)
; Synced against include/libzfork/libzfork2.h
; Platform : Windows x64  |  PureBasic 5.73 / 6.x Unicode
; Calling  : cdecl (ImportC)
;
;   XIncludeFile "libzfork2.pb"
;   If Not ZFork2Load() : End 1 : EndIf
;   ...
;   ZFork2Unload()
;

#ZF2_SOUND_PREDECODED = 0
#ZF2_SOUND_STREAMING  = 1

#ZF2_EVENT_VOICE_START      = 0
#ZF2_EVENT_VOICE_FINISHED   = 1
#ZF2_EVENT_VOICE_LOOP       = 2
#ZF2_EVENT_POSITION_CROSSED = 3

#ZF2_INVALID_VOICE = 0
#ZF2_INVALID_GROUP = $FFFFFFFF

#ZF2_OUTPUT_AUTO    = 0
#ZF2_OUTPUT_WASAPI  = 1
#ZF2_OUTPUT_WAVEOUT = 2

#ZF2_MAX_EFFECT_SLOTS = 4
#ZF2_EFFECT_ECHO      = 0
#ZF2_EFFECT_REVERB    = 1

#ZF2_BPM_PEAKS           = 0
#ZF2_BPM_AUTOCORRELATION = 1
#ZF2_BPM_ADVANCED        = 2

Structure ZF2_FileProbe
  sampleRate.l : channels.l : bitrate.l : vbr.l
  lengthMs.l
  bpmTag.f
  title.a[256] : artist.a[256] : album.a[256] : year.a[16]
EndStructure

Structure ZF2_EchoParams
  leftDelayMs.l : leftSourceVolume.l : leftEchoVolume.l
  rightDelayMs.l : rightSourceVolume.l : rightEchoVolume.l
EndStructure

Structure ZF2_ReverbParams
  roomSize.f : damping.f : wet.f : dry.f
EndStructure

; Callback:
;   ProcedureC MyCallback(eventType.l, voice.l, param1.l, param2.f, userData.i)
;   EndProcedure
;   zfork2_SetCallback(ctx, @MyCallback(), 0)

Global __ZFork2_Lib.i = 0

Procedure.i ZFork2Load(dllPath.s = "libZFork.dll")
  __ZFork2_Lib = OpenLibrary(#PB_Any, dllPath)
  ProcedureReturn __ZFork2_Lib <> 0
EndProcedure

Procedure ZFork2Unload()
  If __ZFork2_Lib
    CloseLibrary(__ZFork2_Lib)
    __ZFork2_Lib = 0
  EndIf
EndProcedure

; ── Context ───────────────────────────────────────────────────────────────────

ImportC "libZFork.dll"
  zfork2_CreateContext()
  zfork2_CreateContextWithBackend(backend.l)
  zfork2_DestroyContext(ctx.i)
  zfork2_Start.l(ctx.i)
  zfork2_Stop(ctx.i)
  zfork2_RegisterGroup.l(ctx.i, name.p-ascii)
  zfork2_GetGroup.l(ctx.i, name.p-ascii)
  zfork2_SetCallback(ctx.i, cb.i, userData.i)
  zfork2_Update(ctx.i)
  zfork2_ProbeFile.l(path.i, *outInfo.ZF2_FileProbe)
  zfork2_DetectFileBPM.l(path.i, algo.l, *outBpm.float)
  zfork2_GetOutputBackendInfo.l(ctx.i, *outName, nameCapacity.l, *outPeriodFrames)
EndImport

; ── Sounds / Cache ────────────────────────────────────────────────────────────

ImportC "libZFork.dll"
  zfork2_LoadSound.i(ctx.i, path.i, soundType.l)
  zfork2_UnloadSound(sound.i)
  zfork2_SetSoundCacheBudget(budgetBytes.q)
  zfork2_GetSoundCacheBudget.q()
  zfork2_GetSoundCacheStats(*outUsedBytes.quad, *outEntryCount)
  zfork2_ClearSoundCache()
EndImport

; ── SoundBank V1 ──────────────────────────────────────────────────────────────

ImportC "libZFork.dll"
  zfork2_LoadBankV1.i(ctx.i, path.i)
  zfork2_CompileBankV1.l(textPath.i, binaryPath.i)
  zfork2_CompileBankV1Embedded.l(textPath.i, binaryPath.i)
  zfork2_UnloadBankV1(bank.i)
  zfork2_GetBankEntryCount.l(bank.i)
  zfork2_GetBankSkippedCount.l(bank.i)
  zfork2_GetBankEventName.l(bank.i, index.l, *outName, nameCapacity.l)
  zfork2_PlayEventV1.l(ctx.i, bank.i, eventId.p-ascii)
  zfork2_PlayEventOnGroupV1.l(ctx.i, bank.i, eventId.p-ascii, group.l)
EndImport

; ── Voices ────────────────────────────────────────────────────────────────────

ImportC "libZFork.dll"
  zfork2_PlaySound.l(ctx.i, sound.i, group.l)
  zfork2_StopVoice(ctx.i, voice.l)
  zfork2_PauseVoice(ctx.i, voice.l)
  zfork2_ResumeVoice(ctx.i, voice.l)
  zfork2_IsVoiceActive.l(ctx.i, voice.l)
  zfork2_SetVoiceVolume(ctx.i, voice.l, volume.f)
  zfork2_SetVoicePan(ctx.i, voice.l, pan.f)
  zfork2_SetVoicePitch(ctx.i, voice.l, semitones.f)
  zfork2_SetVoiceTempo(ctx.i, voice.l, tempo.f)
  zfork2_SetVoiceLoop(ctx.i, voice.l, loopCount.l)
  zfork2_SetVoicePriority(ctx.i, voice.l, priority.l)
  zfork2_SetVoicePosition2D(ctx.i, voice.l, x.f, y.f)
  zfork2_SetVoicePosition3D(ctx.i, voice.l, x.f, y.f, z.f)
  zfork2_SetVoiceMarker(ctx.i, voice.l, markerId.l, posSeconds.f)
  zfork2_ClearVoiceMarkers(ctx.i, voice.l)
EndImport

; ── Groups / Effects ──────────────────────────────────────────────────────────

ImportC "libZFork.dll"
  zfork2_SetGroupVolume(ctx.i, group.l, volume.f)
  zfork2_SetGroupMute(ctx.i, group.l, muted.l)
  zfork2_GetGroupLevels(ctx.i, group.l, *outLeft.float, *outRight.float)
  zfork2_SetGroupEcho(ctx.i, group.l, *params.ZF2_EchoParams)
  zfork2_SetGroupReverb(ctx.i, group.l, *params.ZF2_ReverbParams)
  zfork2_ClearGroupEffect(ctx.i, group.l, slot.l)
EndImport

; ── Listener ──────────────────────────────────────────────────────────────────

ImportC "libZFork.dll"
  zfork2_SetListenerPosition2D(ctx.i, x.f, y.f)
  zfork2_SetListenerPosition3D(ctx.i, x.f, y.f, z.f)
  zfork2_SetListenerDirection(ctx.i, dx.f, dy.f, dz.f)
  zfork2_SetListenerRange(ctx.i, minDist.f, maxDist.f, rolloff.f)
EndImport
