{******************************************************************************
  libzfork — Pascal / Delphi / Free Pascal Bindings (Windows x64)
  Synced from Lazarus/LibZFork.pas against include/libzfork/*.h
******************************************************************************}
unit libzfork;

{$IFDEF FPC}
  {$MODE DELPHI}
  {$H+}
{$ENDIF}
{$IFDEF WINDOWS}
  {$CALLING STDCALL}
{$ENDIF}

interface

uses
  SysUtils, Classes;

const
  LIBZFORK_DLL = 'libZFork.dll';

  { Output-Backend fuer TZForkConfig.outputBackend / CreateWithConfig }
  ZFORK_OUTPUT_AUTO    = 0;  // WASAPI zuerst, Fallback WaveOut
  ZFORK_OUTPUT_WASAPI  = 1;  // nur WASAPI Exclusive (kein stiller Fallback)
  ZFORK_OUTPUT_WAVEOUT = 2;  // nur WaveOut/MME

  ZFORK_SEEK_BEGIN   = 0;
  ZFORK_SEEK_CURRENT = 1;
  ZFORK_SEEK_END     = 2;

  { Player-CallbackMessage (async, vom Audio-Thread) }
  ZFORK_CB_STOP_ASYNC                = 0;
  ZFORK_CB_PLAY_ASYNC                = 1;
  ZFORK_CB_ENTER_LOOP_ASYNC          = 2;
  ZFORK_CB_EXIT_LOOP_ASYNC           = 3;
  ZFORK_CB_ENTER_VOLUME_SLIDE_ASYNC  = 4;
  ZFORK_CB_EXIT_VOLUME_SLIDE_ASYNC   = 5;
  ZFORK_CB_STREAM_BUFFER_DONE_ASYNC  = 6;
  ZFORK_CB_STREAM_NEED_MORE_DATA_ASYNC = 7;
  ZFORK_CB_NEXT_SONG_ASYNC           = 8;
  ZFORK_CB_PAUSE_ASYNC               = 9;
  ZFORK_CB_RESUME_ASYNC              = 10;
  ZFORK_CB_POSITION_ASYNC            = 11;  // param1=pos ms, param2=length ms
  ZFORK_CB_QUEUE_ITEM_ADDED_ASYNC    = 12;
  ZFORK_CB_QUEUE_DUP_REJECTED_ASYNC  = 13;
  ZFORK_CB_QUEUE_CHANGED_ASYNC       = 14;
  ZFORK_CB_BROADCAST_CONNECTED_ASYNC = 15;
  ZFORK_CB_BROADCAST_RECONNECTING_ASYNC = 16;
  ZFORK_CB_BROADCAST_DISCONNECTED_ASYNC = 17;
  ZFORK_CB_BROADCAST_UNDERRUN_ASYNC  = 18;
  ZFORK_CB_BROADCAST_AUTH_FAILED_ASYNC = 19;

type
  TZFORK_HANDLE           = Pointer;
  TZFORK_ANALYSIS_HANDLE  = Pointer;
  PZFORK_ANALYSIS_HANDLE  = ^TZFORK_ANALYSIS_HANDLE;

  PInteger  = ^Integer;
  PLongWord = ^LongWord;
  PSingle   = ^Single;
  PByte     = ^Byte;
  PPointer  = ^Pointer;

  { C-Callback: Player-Ereignisse (Audio-Thread — UI nur per QueueAsyncCall) }
  TZForkPlayerCallback = function(
    instance: Pointer;
    userData: Pointer;
    message: Integer;
    param1: LongWord;
    param2: LongWord
  ): Integer; stdcall;

  { C-Callback: PCM nach DSP (interleaved float) }
  TZForkPCMOutputCallback = procedure(
    instance: Pointer;
    userData: Pointer;
    interleaved: PSingle;
    frames: LongWord;
    channels: Integer;
    sampleRate: Integer
  ); stdcall;

  TZForkErrorEvent = procedure(Sender: TObject; const ErrorMessage: string) of object;

  { Startup-Config fuer zfork_CreateWithConfig }
  TZForkConfig = record
    structSize:        LongWord;
    waveOutDeviceId:   Integer;   // -1 = Default
    bufferMs:          LongWord;  // 0 = Default
    masterVolumeLeft:  Integer;   // -1 = Default
    masterVolumeRight: Integer;
    playerVolumeLeft:  Integer;
    playerVolumeRight: Integer;
    monoMix:           Integer;   // -1 unveraendert, 0 Stereo, 1 Mono
    { Angehaengt — aeltere Caller mit kleinerem structSize bleiben kompatibel }
    outputBackend:     Integer;   // -1 = Default(Auto), ZFORK_OUTPUT_*
  end;
  PZForkConfig = ^TZForkConfig;

  {$PACKRECORDS C}
  TStreamHMSTime = record
    hour, minute, second, millisecond: LongWord;
  end;

  TStreamTime = record
    sec:     LongWord;
    ms:      LongWord;
    samples: UInt64;
    hms:     TStreamHMSTime;
  end;

  TStreamInfo = record
    samplingRate:  Integer;
    channelNumber: Integer;
    bitrate:       Integer;
    vbr:           ByteBool;
    _padVbr:       array[0..2] of Byte;
    length:        TStreamTime;
  end;
  PStreamInfo = ^TStreamInfo;

  TStreamStatus = record
    playing:            ByteBool;
    paused:             ByteBool;
    echoEnabled:        ByteBool;
    equalizerEnabled:   ByteBool;
    vocalCutEnabled:    ByteBool;
    channelMixEnabled:  ByteBool;
    slidingVolume:      ByteBool;
    _pad:               Byte;
    loopCount:          Integer;
    songIndex:          Integer;
    songsInQueue:       Integer;
  end;
  PStreamStatus = ^TStreamStatus;

  TEchoParams = record
    leftDelay:      Integer;
    leftSourceVol:  Integer;
    leftEchoVol:    Integer;
    rightDelay:     Integer;
    rightSourceVol: Integer;
    rightEchoVol:   Integer;
  end;
  PEchoParams = ^TEchoParams;

  TWaveDeviceInfo = record
    id:       LongWord;
    name:     array[0..63] of AnsiChar;
    channels: LongWord;
    formats:  LongWord;
  end;
  PWaveDeviceInfo = ^TWaveDeviceInfo;

  TID3InfoEx = record
    title:       array[0..255] of AnsiChar;
    artist:      array[0..255] of AnsiChar;
    album:       array[0..255] of AnsiChar;
    year:        array[0..15] of AnsiChar;
    comment:     array[0..511] of AnsiChar;
    trackNumber: Integer;
    genre:       Integer;
    albumArtist: array[0..255] of AnsiChar;
    composer:    array[0..255] of AnsiChar;
    copyright:   array[0..255] of AnsiChar;
    url:         array[0..511] of AnsiChar;
    publisher:   array[0..255] of AnsiChar;
    bpm:         Integer;
    key:         array[0..31] of AnsiChar;
  end;
  PID3InfoEx = ^TID3InfoEx;

  TMusicalKeyInfo = record
    raw:     array[0..31] of AnsiChar;
    musical: array[0..15] of AnsiChar;
    camelot: array[0..7] of AnsiChar;
    openKey: array[0..7] of AnsiChar;
    wheel:   Integer;
    isMinor: Integer; { 1=minor/A, 0=major/B, -1=unknown }
  end;
  PMusicalKeyInfo = ^TMusicalKeyInfo;
  {$PACKRECORDS DEFAULT}

  TBroadcastStereoMode = (
    bsmAuto = 0,
    bsmStereo = 1,
    bsmJointStereo = 2,
    bsmMono = 3
  );

  TBroadcastServerType = (
    bstIcecast = 0,
    bstShoutcast = 1
  );

  { Layout = MSVC C++ (Windows x64). C++ bool = 1 Byte + Align-Padding. }
  {$PACKRECORDS C}
  TBroadcastConfig = record
    host:              array[0..255] of AnsiChar;
    port:              Word;
    mount:             array[0..127] of AnsiChar;
    user:              array[0..63] of AnsiChar;
    password:          array[0..127] of AnsiChar;
    streamName:        array[0..127] of AnsiChar;
    bitrateKbps:       Integer;
    sampleRate:        Integer;
    channels:          Integer;
    stereoMode:        Integer;   // TBroadcastStereoMode
    serverType:        Integer;   // TBroadcastServerType
    autoReconnect:     ByteBool;
    _padReconnect:     array[0..2] of Byte;
    reconnectDelayMs:  LongWord;
  end;
  PBroadcastConfig = ^TBroadcastConfig;

  TBroadcastStats = record
    bytesSent:       UInt64;
    framesSent:      UInt64;
    reconnectCount:  UInt64;
    droppedFrames:   UInt64;
    connected:       ByteBool;
    _padConnected:   array[0..6] of Byte; // Align 8 → sizeof 40
  end;
  PBroadcastStats = ^TBroadcastStats;

  TBroadcastDuckingConfig = record
    enabled:    ByteBool;
    _padEn:     array[0..2] of Byte;
    duckDb:     Single;
    attackMs:   LongWord;
    releaseMs:  LongWord;
    threshold:  Single;
  end;
  PBroadcastDuckingConfig = ^TBroadcastDuckingConfig;
  {$PACKRECORDS DEFAULT}

{ ── StreamFormat ───────────────────────────────────────────────────────────── }
const
  ZFORK_FMT_UNKNOWN     = 0;
  ZFORK_FMT_MP3         = 1;
  ZFORK_FMT_OGG         = 2;
  ZFORK_FMT_WAV         = 3;
  ZFORK_FMT_PCM         = 4;
  ZFORK_FMT_FLAC        = 5;
  ZFORK_FMT_FLACOGG     = 6;
  ZFORK_FMT_RESERVED7   = 7;
  ZFORK_FMT_AACADTS     = 8;
  ZFORK_FMT_WAVEIN      = 9;
  ZFORK_FMT_AUTODETECT  = 10;
  ZFORK_FMT_AIFF        = 11;
  ZFORK_FMT_OPUS        = 12;
  ZFORK_FMT_CDAUDIO     = 13;
  ZFORK_FMT_M4A         = 14;

  ZFORK_BPM_PEAKS            = 0;
  ZFORK_BPM_AUTOCORRELATION  = 1;
  ZFORK_BPM_ADVANCED         = 2;

  ZFORK_ANALYZE_BPM      = $00000001;
  ZFORK_ANALYZE_WAVEFORM = $00000002;
  ZFORK_ANALYZE_PEAKS    = $00000004;
  ZFORK_ANALYZE_LENGTH   = $00000008;
  ZFORK_ANALYZE_ALL      = ZFORK_ANALYZE_BPM or ZFORK_ANALYZE_WAVEFORM or
                           ZFORK_ANALYZE_PEAKS or ZFORK_ANALYZE_LENGTH;

{ ============================================================================
  DLL-Imports (nur exportierte Symbole der libZFork.dll)
============================================================================ }

function  zfork_Create: TZFORK_HANDLE; external LIBZFORK_DLL name 'zfork_Create';
function  zfork_CreateWithConfig(config: PZForkConfig): TZFORK_HANDLE;
  external LIBZFORK_DLL name 'zfork_CreateWithConfig';
procedure zfork_Destroy(h: TZFORK_HANDLE); external LIBZFORK_DLL name 'zfork_Destroy';

function  zfork_OpenFile(h: TZFORK_HANDLE; path: PWideChar; format: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_OpenFile';
function  zfork_OpenFileA(h: TZFORK_HANDLE; path: PAnsiChar; format: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_OpenFileA';
function  zfork_LoadPlaylistFile(h: TZFORK_HANDLE; path: PWideChar; fallbackFormat: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_LoadPlaylistFile';
function  zfork_LoadPlaylistFileA(h: TZFORK_HANDLE; path: PAnsiChar; fallbackFormat: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_LoadPlaylistFileA';
function  zfork_AddFile(h: TZFORK_HANDLE; path: PWideChar; format: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_AddFile';
function  zfork_AddFileA(h: TZFORK_HANDLE; path: PAnsiChar; format: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_AddFileA';
function  zfork_RemoveQueueItem(h: TZFORK_HANDLE; index: LongWord): Integer;
  external LIBZFORK_DLL name 'zfork_RemoveQueueItem';
function  zfork_MoveQueueItem(h: TZFORK_HANDLE; fromIndex, toIndex: LongWord): Integer;
  external LIBZFORK_DLL name 'zfork_MoveQueueItem';
function  zfork_GetQueueCount(h: TZFORK_HANDLE): LongWord;
  external LIBZFORK_DLL name 'zfork_GetQueueCount';
function  zfork_GetQueueItemPathA(h: TZFORK_HANDLE; index: LongWord; outPath: PAnsiChar;
  outBytes: LongWord): Integer;
  external LIBZFORK_DLL name 'zfork_GetQueueItemPathA';
function  zfork_OpenStream(h: TZFORK_HANDLE; data: Pointer; size: LongWord; format: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_OpenStream';
procedure zfork_Close(h: TZFORK_HANDLE); external LIBZFORK_DLL name 'zfork_Close';

function  zfork_Play(h: TZFORK_HANDLE): Integer; external LIBZFORK_DLL name 'zfork_Play';
function  zfork_PlayLoop(h: TZFORK_HANDLE; count: LongWord): Integer;
  external LIBZFORK_DLL name 'zfork_PlayLoop';
function  zfork_Pause(h: TZFORK_HANDLE): Integer; external LIBZFORK_DLL name 'zfork_Pause';
function  zfork_Resume(h: TZFORK_HANDLE): Integer; external LIBZFORK_DLL name 'zfork_Resume';
function  zfork_Stop(h: TZFORK_HANDLE): Integer; external LIBZFORK_DLL name 'zfork_Stop';

function  zfork_SeekMs(h: TZFORK_HANDLE; origin: Integer; positionMs: LongWord): Integer;
  external LIBZFORK_DLL name 'zfork_SeekMs';
function  zfork_GetPositionMs(h: TZFORK_HANDLE; positionMs: PLongWord): Integer;
  external LIBZFORK_DLL name 'zfork_GetPositionMs';
function  zfork_GetStreamInfo(h: TZFORK_HANDLE; songIndex: Integer; outInfo: PStreamInfo): Integer;
  external LIBZFORK_DLL name 'zfork_GetStreamInfo';
function  zfork_GetStatus(h: TZFORK_HANDLE; outStatus: PStreamStatus): Integer;
  external LIBZFORK_DLL name 'zfork_GetStatus';

function  zfork_SetMasterVolume(h: TZFORK_HANDLE; left, right: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_SetMasterVolume';
function  zfork_GetMasterVolume(h: TZFORK_HANDLE; left, right: PInteger): Integer;
  external LIBZFORK_DLL name 'zfork_GetMasterVolume';
function  zfork_SetPlayerVolume(h: TZFORK_HANDLE; left, right: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_SetPlayerVolume';
function  zfork_GetPlayerVolume(h: TZFORK_HANDLE; left, right: PInteger): Integer;
  external LIBZFORK_DLL name 'zfork_GetPlayerVolume';
function  zfork_GetVUData(h: TZFORK_HANDLE; left, right: PSingle): Integer;
  external LIBZFORK_DLL name 'zfork_GetVUData';
function  zfork_MixChannels(h: TZFORK_HANDLE; mono: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_MixChannels';
function  zfork_SetRate(h: TZFORK_HANDLE; rate: Single): Integer;
  external LIBZFORK_DLL name 'zfork_SetRate';
function  zfork_SetPitch(h: TZFORK_HANDLE; semitones: Single): Integer;
  external LIBZFORK_DLL name 'zfork_SetPitch';
function  zfork_SetTempo(h: TZFORK_HANDLE; tempo: Single): Integer;
  external LIBZFORK_DLL name 'zfork_SetTempo';

function  zfork_EnableEqualizer(h: TZFORK_HANDLE; enable: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_EnableEqualizer';
function  zfork_SetEQBandGain(h: TZFORK_HANDLE; band: Integer; gainDb: Single): Integer;
  external LIBZFORK_DLL name 'zfork_SetEQBandGain';
function  zfork_GetEQBandGain(h: TZFORK_HANDLE; band: Integer; gainDb: PSingle): Integer;
  external LIBZFORK_DLL name 'zfork_GetEQBandGain';
function  zfork_ResetEqualizer(h: TZFORK_HANDLE): Integer;
  external LIBZFORK_DLL name 'zfork_ResetEqualizer';
function  zfork_EnableEcho(h: TZFORK_HANDLE; enable: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_EnableEcho';
function  zfork_SetEchoParams(h: TZFORK_HANDLE; params: PEchoParams): Integer;
  external LIBZFORK_DLL name 'zfork_SetEchoParams';
function  zfork_GetEchoParams(h: TZFORK_HANDLE; outParams: PEchoParams): Integer;
  external LIBZFORK_DLL name 'zfork_GetEchoParams';
function  zfork_EnableVocalCut(h: TZFORK_HANDLE; enable: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_EnableVocalCut';
function  zfork_EnableSideCut(h: TZFORK_HANDLE; enable: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_EnableSideCut';
function  zfork_EnableStereoEnhancer(h: TZFORK_HANDLE; enable: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_EnableStereoEnhancer';
function  zfork_SetStereoWidth(h: TZFORK_HANDLE; width: Single): Integer;
  external LIBZFORK_DLL name 'zfork_SetStereoWidth';
function  zfork_GetStereoWidth(h: TZFORK_HANDLE; outWidth: PSingle): Integer;
  external LIBZFORK_DLL name 'zfork_GetStereoWidth';
function  zfork_SetBufferSize(h: TZFORK_HANDLE; milliseconds: LongWord): Integer;
  external LIBZFORK_DLL name 'zfork_SetBufferSize';

function  zfork_GetFFTData(h: TZFORK_HANDLE; left, right: PSingle; points: LongWord): Integer;
  external LIBZFORK_DLL name 'zfork_GetFFTData';
function  zfork_DetectBPM(h: TZFORK_HANDLE; algo: Integer; outBPM: PSingle): Integer;
  external LIBZFORK_DLL name 'zfork_DetectBPM';
function  zfork_DetectFileBPMA(h: TZFORK_HANDLE; path: PAnsiChar; algo: Integer;
  outBPM: PSingle): Integer;
  external LIBZFORK_DLL name 'zfork_DetectFileBPMA';

function  zfork_AnalyzeFile(h: TZFORK_HANDLE; path: PWideChar; flags: LongWord;
  outHandle: PZFORK_ANALYSIS_HANDLE): Integer;
  external LIBZFORK_DLL name 'zfork_AnalyzeFile';
function  zfork_AnalyzeFileA(h: TZFORK_HANDLE; path: PAnsiChar; flags: LongWord;
  outHandle: PZFORK_ANALYSIS_HANDLE): Integer;
  external LIBZFORK_DLL name 'zfork_AnalyzeFileA';
procedure zfork_FreeAnalysis(h: TZFORK_HANDLE; analysis: TZFORK_ANALYSIS_HANDLE);
  external LIBZFORK_DLL name 'zfork_FreeAnalysis';
function  zfork_GetAnalysisSummary(h: TZFORK_HANDLE; analysis: TZFORK_ANALYSIS_HANDLE;
  outBpm: PSingle; outLengthMs, outFlags: PLongWord): Integer;
  external LIBZFORK_DLL name 'zfork_GetAnalysisSummary';
function  zfork_CopyAnalysisWaveform(h: TZFORK_HANDLE; analysis: TZFORK_ANALYSIS_HANDLE;
  outBins: PByte; maxBins: LongWord): LongWord;
  external LIBZFORK_DLL name 'zfork_CopyAnalysisWaveform';
function  zfork_CopyAnalysisPeaks(h: TZFORK_HANDLE; analysis: TZFORK_ANALYSIS_HANDLE;
  outTimesMs, outValues: PSingle; maxPeaks: LongWord): LongWord;
  external LIBZFORK_DLL name 'zfork_CopyAnalysisPeaks';

function  zfork_SetPlaylistAllowDuplicates(h: TZFORK_HANDLE; allow: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_SetPlaylistAllowDuplicates';
function  zfork_GetPlaylistAllowDuplicates(h: TZFORK_HANDLE): Integer;
  external LIBZFORK_DLL name 'zfork_GetPlaylistAllowDuplicates';

function  zfork_StartBroadcast(h: TZFORK_HANDLE; config: PBroadcastConfig): Integer;
  external LIBZFORK_DLL name 'zfork_StartBroadcast';
function  zfork_StopBroadcast(h: TZFORK_HANDLE): Integer;
  external LIBZFORK_DLL name 'zfork_StopBroadcast';
function  zfork_PushBroadcastPCM(h: TZFORK_HANDLE; interleaved: PSingle;
  frames: LongWord; channels, sampleRate: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_PushBroadcastPCM';
function  zfork_StartBroadcastFile(h: TZFORK_HANDLE; path: PWideChar; format: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_StartBroadcastFile';
function  zfork_StartBroadcastFileA(h: TZFORK_HANDLE; path: PAnsiChar; format: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_StartBroadcastFileA';
function  zfork_AttachBroadcastToCurrentPlayback(h: TZFORK_HANDLE; enable: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_AttachBroadcastToCurrentPlayback';
function  zfork_SetBroadcastMetadata(h: TZFORK_HANDLE; title, artist, album: PAnsiChar): Integer;
  external LIBZFORK_DLL name 'zfork_SetBroadcastMetadata';
function  zfork_PushBroadcastMicPCM(h: TZFORK_HANDLE; interleaved: PSingle;
  frames: LongWord; channels, sampleRate: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_PushBroadcastMicPCM';
function  zfork_SetBroadcastMicActive(h: TZFORK_HANDLE; active: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_SetBroadcastMicActive';
function  zfork_SetBroadcastDucking(h: TZFORK_HANDLE; config: PBroadcastDuckingConfig): Integer;
  external LIBZFORK_DLL name 'zfork_SetBroadcastDucking';
function  zfork_GetBroadcastDucking(h: TZFORK_HANDLE; outConfig: PBroadcastDuckingConfig): Integer;
  external LIBZFORK_DLL name 'zfork_GetBroadcastDucking';
function  zfork_StartBroadcastMicCapture(h: TZFORK_HANDLE; deviceId, sampleRate, channels: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_StartBroadcastMicCapture';
function  zfork_StopBroadcastMicCapture(h: TZFORK_HANDLE): Integer;
  external LIBZFORK_DLL name 'zfork_StopBroadcastMicCapture';
function  zfork_GetBroadcastStats(h: TZFORK_HANDLE; outStats: PBroadcastStats): Integer;
  external LIBZFORK_DLL name 'zfork_GetBroadcastStats';
function  zfork_GetBroadcastError(h: TZFORK_HANDLE): PAnsiChar;
  external LIBZFORK_DLL name 'zfork_GetBroadcastError';

function  zfork_SetPCMOutputCallback(h: TZFORK_HANDLE; callback: TZForkPCMOutputCallback;
  userData: Pointer; disableBuiltInOutput: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_SetPCMOutputCallback';

function  zfork_SetCallback(h: TZFORK_HANDLE; callback: TZForkPlayerCallback;
  userData: Pointer): Integer;
  external LIBZFORK_DLL name 'zfork_SetCallback';

function  zfork_EnumerateOutputDevices(h: TZFORK_HANDLE): Integer;
  external LIBZFORK_DLL name 'zfork_EnumerateOutputDevices';
function  zfork_GetOutputDeviceInfo(h: TZFORK_HANDLE; deviceId: Integer;
  outInfo: PWaveDeviceInfo): Integer;
  external LIBZFORK_DLL name 'zfork_GetOutputDeviceInfo';
function  zfork_SetOutputDevice(h: TZFORK_HANDLE; deviceId: Integer): Integer;
  external LIBZFORK_DLL name 'zfork_SetOutputDevice';

function  zfork_LoadID3Ex(h: TZFORK_HANDLE; songIndex: Integer; outInfo: PID3InfoEx): Integer;
  external LIBZFORK_DLL name 'zfork_LoadID3Ex';
function  zfork_LoadID3Picture(h: TZFORK_HANDLE; songIndex, picIndex: Integer;
  outData: PPointer; outSize: PLongWord; outType: PInteger): Integer;
  external LIBZFORK_DLL name 'zfork_LoadID3Picture';
function  zfork_LoadFileID3ExA(h: TZFORK_HANDLE; path: PAnsiChar; outInfo: PID3InfoEx): Integer;
  external LIBZFORK_DLL name 'zfork_LoadFileID3ExA';
function  zfork_LoadFileID3PictureA(h: TZFORK_HANDLE; path: PAnsiChar; picIndex: Integer;
  outData: PPointer; outSize: PLongWord; outType: PInteger): Integer;
  external LIBZFORK_DLL name 'zfork_LoadFileID3PictureA';
procedure zfork_FreeMemory(ptr: Pointer); external LIBZFORK_DLL name 'zfork_FreeMemory';

function  zfork_ParseMusicalKey(text: PAnsiChar; outInfo: PMusicalKeyInfo): Integer;
  external LIBZFORK_DLL name 'zfork_ParseMusicalKey';
function  zfork_KeysCompatible(a, b: PMusicalKeyInfo): Integer;
  external LIBZFORK_DLL name 'zfork_KeysCompatible';

function  zfork_GetError(h: TZFORK_HANDLE): PAnsiChar;
  external LIBZFORK_DLL name 'zfork_GetError';

{ ============================================================================
  Pascal-Helfer (Strings / Boolean)
============================================================================ }

{ Letzter Player-Fehler als Pascal-String. }
function ZForkGetError(h: TZFORK_HANDLE): string;

{ Letzter Broadcast-Fehler als Pascal-String. }
function ZForkGetBroadcastError(h: TZFORK_HANDLE): string;

{ Unicode-Pfad oeffnen. True = OK. Bei False: ZForkGetError lesen. }
function ZForkOpenFile(h: TZFORK_HANDLE; const APath: UnicodeString;
  AFormat: Integer = ZFORK_FMT_AUTODETECT): Boolean;

function ZForkAddFile(h: TZFORK_HANDLE; const APath: UnicodeString;
  AFormat: Integer = ZFORK_FMT_AUTODETECT): Boolean;

function ZForkLoadPlaylist(h: TZFORK_HANDLE; const APath: UnicodeString;
  AFallbackFormat: Integer = ZFORK_FMT_AUTODETECT): Boolean;

function ZForkPlay(h: TZFORK_HANDLE): Boolean;
function ZForkStop(h: TZFORK_HANDLE): Boolean;
function ZForkPause(h: TZFORK_HANDLE): Boolean;
function ZForkResume(h: TZFORK_HANDLE): Boolean;

function ZForkSetMasterVolume(h: TZFORK_HANDLE; Left, Right: Integer): Boolean;
function ZForkGetMasterVolume(h: TZFORK_HANDLE; out Left, Right: Integer): Boolean;

function ZForkDetectBPM(h: TZFORK_HANDLE; Algo: Integer = ZFORK_BPM_ADVANCED): Single;

{ Leere BroadcastConfig mit sinnvollen Defaults (Icecast, 44100/2, JointStereo). }
procedure ZForkInitBroadcastConfig(out Cfg: TBroadcastConfig);

{ ZForkConfig mit Defaults; outputBackend = ZFORK_OUTPUT_AUTO. }
procedure ZForkInitConfig(out Cfg: TZForkConfig);

{ Geraeteteliste fuer Inspector/Combobox. Backend = ZFORK_OUTPUT_*.
  OutNames[0] = 'Auto (Standard)'; danach Geraetenamen. Result = Anzahl inkl. Auto. }
function ZForkListOutputDevices(Backend: Integer; OutNames: TStrings): Integer;

{ Geraetename (UTF-8 aus DLL). Backend wie oben. DeviceId >= 0. }
function ZForkGetOutputDeviceName(Backend, DeviceId: Integer): string;

implementation

function AnsiPtrToString(P: PAnsiChar): string;
begin
  if P <> nil then
    Result := string(AnsiString(P))
  else
    Result := '';
end;

function ZForkGetError(h: TZFORK_HANDLE): string;
begin
  if h = nil then
    Result := 'Handle is nil'
  else
    Result := AnsiPtrToString(zfork_GetError(h));
end;

function ZForkGetBroadcastError(h: TZFORK_HANDLE): string;
begin
  if h = nil then
    Result := 'Handle is nil'
  else
    Result := AnsiPtrToString(zfork_GetBroadcastError(h));
end;

function ZForkOpenFile(h: TZFORK_HANDLE; const APath: UnicodeString;
  AFormat: Integer): Boolean;
begin
  Result := zfork_OpenFile(h, PWideChar(APath), AFormat) <> 0;
end;

function ZForkAddFile(h: TZFORK_HANDLE; const APath: UnicodeString;
  AFormat: Integer): Boolean;
begin
  Result := zfork_AddFile(h, PWideChar(APath), AFormat) <> 0;
end;

function ZForkLoadPlaylist(h: TZFORK_HANDLE; const APath: UnicodeString;
  AFallbackFormat: Integer): Boolean;
begin
  Result := zfork_LoadPlaylistFile(h, PWideChar(APath), AFallbackFormat) <> 0;
end;

function ZForkPlay(h: TZFORK_HANDLE): Boolean;
begin
  Result := zfork_Play(h) <> 0;
end;

function ZForkStop(h: TZFORK_HANDLE): Boolean;
begin
  Result := zfork_Stop(h) <> 0;
end;

function ZForkPause(h: TZFORK_HANDLE): Boolean;
begin
  Result := zfork_Pause(h) <> 0;
end;

function ZForkResume(h: TZFORK_HANDLE): Boolean;
begin
  Result := zfork_Resume(h) <> 0;
end;

function ZForkSetMasterVolume(h: TZFORK_HANDLE; Left, Right: Integer): Boolean;
begin
  Result := zfork_SetMasterVolume(h, Left, Right) <> 0;
end;

function ZForkGetMasterVolume(h: TZFORK_HANDLE; out Left, Right: Integer): Boolean;
begin
  Left := 0;
  Right := 0;
  Result := zfork_GetMasterVolume(h, @Left, @Right) <> 0;
end;

function ZForkDetectBPM(h: TZFORK_HANDLE; Algo: Integer): Single;
var
  Bpm: Single;
begin
  Bpm := 0;
  if zfork_DetectBPM(h, Algo, @Bpm) = 0 then
    Result := 0
  else
    Result := Bpm;
end;

procedure ZForkInitBroadcastConfig(out Cfg: TBroadcastConfig);
begin
  FillChar(Cfg, SizeOf(Cfg), 0);
  StrPLCopy(Cfg.host, '127.0.0.1', High(Cfg.host));
  Cfg.port := 8000;
  StrPLCopy(Cfg.mount, '/live.mp3', High(Cfg.mount));
  StrPLCopy(Cfg.user, 'source', High(Cfg.user));
  StrPLCopy(Cfg.password, 'hackme', High(Cfg.password));
  StrPLCopy(Cfg.streamName, 'libZFork', High(Cfg.streamName));
  Cfg.bitrateKbps := 192;
  Cfg.sampleRate := 44100;
  Cfg.channels := 2;
  Cfg.stereoMode := Ord(bsmJointStereo);
  Cfg.serverType := Ord(bstIcecast);
  Cfg.autoReconnect := True;
  Cfg.reconnectDelayMs := 3000;
end;

procedure ZForkInitConfig(out Cfg: TZForkConfig);
begin
  FillChar(Cfg, SizeOf(Cfg), 0);
  Cfg.structSize := SizeOf(TZForkConfig);
  Cfg.waveOutDeviceId := -1;
  Cfg.bufferMs := 0;
  Cfg.masterVolumeLeft := -1;
  Cfg.masterVolumeRight := -1;
  Cfg.playerVolumeLeft := -1;
  Cfg.playerVolumeRight := -1;
  Cfg.monoMix := -1;
  Cfg.outputBackend := ZFORK_OUTPUT_AUTO;
end;

function ZForkGetOutputDeviceName(Backend, DeviceId: Integer): string;
var
  H: TZFORK_HANDLE;
  Cfg: TZForkConfig;
  Info: TWaveDeviceInfo;
begin
  Result := '';
  if DeviceId < 0 then Exit;
  ZForkInitConfig(Cfg);
  Cfg.outputBackend := Backend;
  H := zfork_CreateWithConfig(@Cfg);
  if H = nil then Exit;
  try
    FillChar(Info, SizeOf(Info), 0);
    if zfork_GetOutputDeviceInfo(H, DeviceId, @Info) <> 0 then
      Result := string(AnsiString(Info.name));
  finally
    zfork_Destroy(H);
  end;
end;

function ZForkListOutputDevices(Backend: Integer; OutNames: TStrings): Integer;
var
  H: TZFORK_HANDLE;
  Cfg: TZForkConfig;
  I, N: Integer;
  Info: TWaveDeviceInfo;
  Nm: string;
begin
  Result := 0;
  if OutNames = nil then Exit;
  OutNames.Clear;
  OutNames.Add('Auto (Standard)');
  ZForkInitConfig(Cfg);
  Cfg.outputBackend := Backend;
  H := zfork_CreateWithConfig(@Cfg);
  if H = nil then
  begin
    Result := OutNames.Count;
    Exit;
  end;
  try
    N := zfork_EnumerateOutputDevices(H);
    for I := 0 to N - 1 do
    begin
      FillChar(Info, SizeOf(Info), 0);
      if zfork_GetOutputDeviceInfo(H, I, @Info) <> 0 then
        Nm := string(AnsiString(Info.name))
      else
        Nm := Format('Geraet %d', [I]);
      if Nm = '' then
        Nm := Format('Geraet %d', [I]);
      OutNames.Add(Format('%d: %s', [I, Nm]));
    end;
  finally
    zfork_Destroy(H);
  end;
  Result := OutNames.Count;
end;

end.
