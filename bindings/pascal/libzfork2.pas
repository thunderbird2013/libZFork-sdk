{******************************************************************************
  libzfork2 — Pascal / Delphi / Free Pascal Bindings (Windows x64)
  Synced from Lazarus/LibZFork2.pas against include/libzfork/*.h
******************************************************************************}
unit libzfork2;

{$IFDEF FPC}
  {$MODE DELPHI}
  {$H+}
{$ENDIF}

interface

const
  LIBZFORK_DLL = 'libZFork.dll';

  ZF2_SOUND_PREDECODED = 0;
  ZF2_SOUND_STREAMING  = 1;

  ZF2_EVENT_VOICE_START      = 0;
  ZF2_EVENT_VOICE_FINISHED   = 1;
  ZF2_EVENT_VOICE_LOOP       = 2;
  ZF2_EVENT_POSITION_CROSSED = 3;

  ZF2_BPM_PEAKS           = 0;
  ZF2_BPM_AUTOCORRELATION = 1;
  ZF2_BPM_ADVANCED        = 2;

  { Output-Backend fuer zfork2_CreateContextWithBackend }
  ZF2_OUTPUT_AUTO    = 0;  // WASAPI zuerst, stiller Fallback WaveOut
  ZF2_OUTPUT_WASAPI  = 1;  // nur WASAPI Exclusive (Fehler statt Fallback)
  ZF2_OUTPUT_WAVEOUT = 2;  // nur WaveOut/MME

  ZF2_MAX_EFFECT_SLOTS = 4;
  ZF2_EFFECT_ECHO      = 0;
  ZF2_EFFECT_REVERB    = 1;

  ZF2_INVALID_VOICE = LongWord(0);
  ZF2_INVALID_GROUP = LongWord($FFFFFFFF);

type
  TZF2_CONTEXT = Pointer;
  TZF2_SOUND   = Pointer;
  TZF2_BANK    = Pointer;
  TZF2_VOICE   = LongWord;
  TZF2_GROUP   = LongWord;

  { Alias-Namen wie im C-Header }
  ZF2_CONTEXT = TZF2_CONTEXT;
  ZF2_SOUND   = TZF2_SOUND;
  ZF2_BANK    = TZF2_BANK;
  ZF2_VOICE   = TZF2_VOICE;
  ZF2_GROUP   = TZF2_GROUP;

  PUInt64   = ^UInt64;
  PLongWord = ^LongWord;
  PSingle   = ^Single;

  TZF2EventCallback = procedure(
    eventType: Integer;
    voice: TZF2_VOICE;
    param1: LongWord;
    param2: Single;
    userData: Pointer
  ); cdecl;

  {$PACKRECORDS C}
  TZF2FileProbe = record
    sampleRate: Integer;
    channels:   Integer;
    bitrate:    Integer;
    vbr:        Integer;
    lengthMs:   LongWord;
    bpmTag:     Single;
    title:      array[0..255] of AnsiChar;
    artist:     array[0..255] of AnsiChar;
    album:      array[0..255] of AnsiChar;
    year:       array[0..15] of AnsiChar;
  end;
  PZF2FileProbe = ^TZF2FileProbe;
  {$PACKRECORDS DEFAULT}

  {$PACKRECORDS C}
  TZF2EchoParams = record
    leftDelayMs:       Integer;
    leftSourceVolume:  Integer;
    leftEchoVolume:    Integer;
    rightDelayMs:      Integer;
    rightSourceVolume: Integer;
    rightEchoVolume:   Integer;
  end;
  PZF2EchoParams = ^TZF2EchoParams;

  TZF2ReverbParams = record
    roomSize: Single;
    damping:  Single;
    wet:      Single;
    dry:      Single;
  end;
  PZF2ReverbParams = ^TZF2ReverbParams;
  {$PACKRECORDS DEFAULT}

{ ── Context ──────────────────────────────────────────────────────────────── }
function  zfork2_CreateContext: TZF2_CONTEXT; cdecl;
  external LIBZFORK_DLL name 'zfork2_CreateContext';
{ backend: ZF2_OUTPUT_AUTO / WASAPI / WAVEOUT }
function  zfork2_CreateContextWithBackend(backend: Integer): TZF2_CONTEXT; cdecl;
  external LIBZFORK_DLL name 'zfork2_CreateContextWithBackend';
procedure zfork2_DestroyContext(ctx: TZF2_CONTEXT); cdecl;
  external LIBZFORK_DLL name 'zfork2_DestroyContext';
function  zfork2_Start(ctx: TZF2_CONTEXT): Integer; cdecl;
  external LIBZFORK_DLL name 'zfork2_Start';
procedure zfork2_Stop(ctx: TZF2_CONTEXT); cdecl;
  external LIBZFORK_DLL name 'zfork2_Stop';
function  zfork2_RegisterGroup(ctx: TZF2_CONTEXT; name: PAnsiChar): TZF2_GROUP; cdecl;
  external LIBZFORK_DLL name 'zfork2_RegisterGroup';
function  zfork2_GetGroup(ctx: TZF2_CONTEXT; name: PAnsiChar): TZF2_GROUP; cdecl;
  external LIBZFORK_DLL name 'zfork2_GetGroup';
procedure zfork2_SetCallback(ctx: TZF2_CONTEXT; cb: TZF2EventCallback;
  userData: Pointer); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetCallback';
procedure zfork2_Update(ctx: TZF2_CONTEXT); cdecl;
  external LIBZFORK_DLL name 'zfork2_Update';

{ ── Sounds ───────────────────────────────────────────────────────────────── }
function  zfork2_LoadSound(ctx: TZF2_CONTEXT; path: PWideChar;
  soundType: Integer): TZF2_SOUND; cdecl;
  external LIBZFORK_DLL name 'zfork2_LoadSound';
procedure zfork2_UnloadSound(sound: TZF2_SOUND); cdecl;
  external LIBZFORK_DLL name 'zfork2_UnloadSound';

{ ── Sound-Cache (LRU, PreDecoded) ────────────────────────────────────────── }
{ budgetBytes=0: dynamisch (kein Evict). Sonst LRU-Cap in Bytes. }
procedure zfork2_SetSoundCacheBudget(budgetBytes: UInt64); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetSoundCacheBudget';
function  zfork2_GetSoundCacheBudget: UInt64; cdecl;
  external LIBZFORK_DLL name 'zfork2_GetSoundCacheBudget';
procedure zfork2_GetSoundCacheStats(outUsedBytes: PUInt64;
  outEntryCount: PLongWord); cdecl;
  external LIBZFORK_DLL name 'zfork2_GetSoundCacheStats';
procedure zfork2_ClearSoundCache; cdecl;
  external LIBZFORK_DLL name 'zfork2_ClearSoundCache';

{ ── SoundBank V1 ─────────────────────────────────────────────────────────── }
function  zfork2_LoadBankV1(ctx: TZF2_CONTEXT; path: PWideChar): TZF2_BANK; cdecl;
  external LIBZFORK_DLL name 'zfork2_LoadBankV1';
function  zfork2_CompileBankV1(textPath, binaryPath: PWideChar): Integer; cdecl;
  external LIBZFORK_DLL name 'zfork2_CompileBankV1';
{ Wie CompileBankV1, bettet Sound-Dateien in die .bank ein. }
function  zfork2_CompileBankV1Embedded(textPath, binaryPath: PWideChar): Integer; cdecl;
  external LIBZFORK_DLL name 'zfork2_CompileBankV1Embedded';
procedure zfork2_UnloadBankV1(bank: TZF2_BANK); cdecl;
  external LIBZFORK_DLL name 'zfork2_UnloadBankV1';
function  zfork2_GetBankEntryCount(bank: TZF2_BANK): LongWord; cdecl;
  external LIBZFORK_DLL name 'zfork2_GetBankEntryCount';
function  zfork2_GetBankSkippedCount(bank: TZF2_BANK): LongWord; cdecl;
  external LIBZFORK_DLL name 'zfork2_GetBankSkippedCount';
function  zfork2_GetBankEventName(bank: TZF2_BANK; index: LongWord;
  outName: PAnsiChar; nameCapacity: LongWord): Integer; cdecl;
  external LIBZFORK_DLL name 'zfork2_GetBankEventName';
function  zfork2_PlayEventV1(ctx: TZF2_CONTEXT; bank: TZF2_BANK;
  eventId: PAnsiChar): TZF2_VOICE; cdecl;
  external LIBZFORK_DLL name 'zfork2_PlayEventV1';
function  zfork2_PlayEventOnGroupV1(ctx: TZF2_CONTEXT; bank: TZF2_BANK;
  eventId: PAnsiChar; group: TZF2_GROUP): TZF2_VOICE; cdecl;
  external LIBZFORK_DLL name 'zfork2_PlayEventOnGroupV1';

{ ── Voices ───────────────────────────────────────────────────────────────── }
function  zfork2_PlaySound(ctx: TZF2_CONTEXT; sound: TZF2_SOUND;
  group: TZF2_GROUP): TZF2_VOICE; cdecl;
  external LIBZFORK_DLL name 'zfork2_PlaySound';
procedure zfork2_StopVoice(ctx: TZF2_CONTEXT; voice: TZF2_VOICE); cdecl;
  external LIBZFORK_DLL name 'zfork2_StopVoice';
procedure zfork2_PauseVoice(ctx: TZF2_CONTEXT; voice: TZF2_VOICE); cdecl;
  external LIBZFORK_DLL name 'zfork2_PauseVoice';
procedure zfork2_ResumeVoice(ctx: TZF2_CONTEXT; voice: TZF2_VOICE); cdecl;
  external LIBZFORK_DLL name 'zfork2_ResumeVoice';
function  zfork2_IsVoiceActive(ctx: TZF2_CONTEXT; voice: TZF2_VOICE): Integer; cdecl;
  external LIBZFORK_DLL name 'zfork2_IsVoiceActive';
procedure zfork2_SetVoiceVolume(ctx: TZF2_CONTEXT; voice: TZF2_VOICE;
  volume: Single); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetVoiceVolume';
procedure zfork2_SetVoicePan(ctx: TZF2_CONTEXT; voice: TZF2_VOICE;
  pan: Single); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetVoicePan';
procedure zfork2_SetVoicePitch(ctx: TZF2_CONTEXT; voice: TZF2_VOICE;
  semitones: Single); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetVoicePitch';
procedure zfork2_SetVoiceTempo(ctx: TZF2_CONTEXT; voice: TZF2_VOICE;
  tempo: Single); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetVoiceTempo';
procedure zfork2_SetVoiceLoop(ctx: TZF2_CONTEXT; voice: TZF2_VOICE;
  loopCount: Integer); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetVoiceLoop';
procedure zfork2_SetVoicePriority(ctx: TZF2_CONTEXT; voice: TZF2_VOICE;
  priority: Integer); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetVoicePriority';
procedure zfork2_SetVoicePosition2D(ctx: TZF2_CONTEXT; voice: TZF2_VOICE;
  x, y: Single); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetVoicePosition2D';
procedure zfork2_SetVoicePosition3D(ctx: TZF2_CONTEXT; voice: TZF2_VOICE;
  x, y, z: Single); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetVoicePosition3D';
procedure zfork2_SetVoiceMarker(ctx: TZF2_CONTEXT; voice: TZF2_VOICE;
  markerId: LongWord; posSeconds: Single); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetVoiceMarker';
procedure zfork2_ClearVoiceMarkers(ctx: TZF2_CONTEXT; voice: TZF2_VOICE); cdecl;
  external LIBZFORK_DLL name 'zfork2_ClearVoiceMarkers';

{ ── Groups ───────────────────────────────────────────────────────────────── }
procedure zfork2_SetGroupVolume(ctx: TZF2_CONTEXT; group: TZF2_GROUP;
  volume: Single); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetGroupVolume';
procedure zfork2_SetGroupMute(ctx: TZF2_CONTEXT; group: TZF2_GROUP;
  muted: Integer); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetGroupMute';
procedure zfork2_GetGroupLevels(ctx: TZF2_CONTEXT; group: TZF2_GROUP;
  outLeft, outRight: PSingle); cdecl;
  external LIBZFORK_DLL name 'zfork2_GetGroupLevels';

{ Group effects (Echo / Reverb) — thread-sicher, greift ab naechstem Mix-Block }
procedure zfork2_SetGroupEcho(ctx: TZF2_CONTEXT; group: TZF2_GROUP;
  params: PZF2EchoParams); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetGroupEcho';
procedure zfork2_SetGroupReverb(ctx: TZF2_CONTEXT; group: TZF2_GROUP;
  params: PZF2ReverbParams); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetGroupReverb';
procedure zfork2_ClearGroupEffect(ctx: TZF2_CONTEXT; group: TZF2_GROUP;
  slot: Integer); cdecl;
  external LIBZFORK_DLL name 'zfork2_ClearGroupEffect';

{ ── Listener ─────────────────────────────────────────────────────────────── }
procedure zfork2_SetListenerPosition2D(ctx: TZF2_CONTEXT; x, y: Single); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetListenerPosition2D';
procedure zfork2_SetListenerPosition3D(ctx: TZF2_CONTEXT; x, y, z: Single); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetListenerPosition3D';
procedure zfork2_SetListenerDirection(ctx: TZF2_CONTEXT; dx, dy, dz: Single); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetListenerDirection';
procedure zfork2_SetListenerRange(ctx: TZF2_CONTEXT;
  minDist, maxDist, rolloff: Single); cdecl;
  external LIBZFORK_DLL name 'zfork2_SetListenerRange';

{ ── File utilities ───────────────────────────────────────────────────────── }
function  zfork2_ProbeFile(path: PWideChar; outInfo: PZF2FileProbe): Integer; cdecl;
  external LIBZFORK_DLL name 'zfork2_ProbeFile';
function  zfork2_DetectFileBPM(path: PWideChar; algo: Integer;
  outBpm: PSingle): Integer; cdecl;
  external LIBZFORK_DLL name 'zfork2_DetectFileBPM';

{ Welches Output-Backend aktiv ist (WASAPI-Exclusive / WaveOut) + Periode. }
function  zfork2_GetOutputBackendInfo(ctx: TZF2_CONTEXT; outName: PAnsiChar;
  nameCapacity: LongWord; outPeriodFrames: PLongWord): Integer; cdecl;
  external LIBZFORK_DLL name 'zfork2_GetOutputBackendInfo';

{ ── Pascal-Helfer ────────────────────────────────────────────────────────── }
procedure ZFork2GetSoundCacheStats(out UsedBytes: UInt64; out EntryCount: LongWord);
function  ZFork2LoadBank(ctx: TZF2_CONTEXT; const APath: UnicodeString): TZF2_BANK;
function  ZFork2CompileBank(const ATextPath, ABinaryPath: UnicodeString): Boolean;
function  ZFork2CompileBankEmbedded(const ATextPath, ABinaryPath: UnicodeString): Boolean;

implementation

procedure ZFork2GetSoundCacheStats(out UsedBytes: UInt64; out EntryCount: LongWord);
begin
  UsedBytes := 0;
  EntryCount := 0;
  zfork2_GetSoundCacheStats(@UsedBytes, @EntryCount);
end;

function ZFork2LoadBank(ctx: TZF2_CONTEXT; const APath: UnicodeString): TZF2_BANK;
begin
  Result := zfork2_LoadBankV1(ctx, PWideChar(APath));
end;

function ZFork2CompileBank(const ATextPath, ABinaryPath: UnicodeString): Boolean;
begin
  Result := zfork2_CompileBankV1(PWideChar(ATextPath), PWideChar(ABinaryPath)) <> 0;
end;

function ZFork2CompileBankEmbedded(const ATextPath, ABinaryPath: UnicodeString): Boolean;
begin
  Result := zfork2_CompileBankV1Embedded(PWideChar(ATextPath), PWideChar(ABinaryPath)) <> 0;
end;

end.
