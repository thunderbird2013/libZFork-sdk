{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  TZForkPlayer — zentrale LCL-Player-Komponente (Palette libZFork)

  Was die Komponente kann:
  - Datei/Playlist oeffnen, AutoPlay, Active Start/Stop des Engines
  - Transport, Queue, Seek, Volume, Pitch/Tempo als Properties/Methoden
  - DSP live: Equalizer, Echo, VocalCut, SideCut, Stereo-Enhancer
  - FFT/VU, BPM, Analyse, ID3, Broadcast-Methoden an der Komponente
  - OutputBackend / OutputDevice (WASAPI Exclusive oder WaveOut)
  - Events asynchron auf dem Hauptthread (Play/Stop/Position/Queue/Broadcast)
  - Client-Anbindung (IZForkPlayerClient) fuer Spectrum/Waveform/ID3/…
  - About im Object Inspector (Property-Editor ".")
******************************************************************************}
unit ZForkPlayer;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Dialogs, LibZFork, ZForkIntf;

type
  TZForkOutputBackend = (
    obAuto,
    obWasapi,
    obWaveOut
  );

  TZForkIndexEvent = procedure(Sender: TObject; Index: Integer) of object;
  TZForkPositionEvent = procedure(Sender: TObject; PositionMs, LengthMs: LongWord) of object;
  TZForkQueueEvent = procedure(Sender: TObject; QueueCount: LongWord) of object;
  TZForkParamEvent = procedure(Sender: TObject; Param1, Param2: LongWord) of object;

  { TZForkPlayer }
  TZForkPlayer = class(TComponent)
  private
    FHandle: TZFORK_HANDLE;
    FFileName: string;
    FAutoPlay: Boolean;
    FMasterVolumeLeft: Integer;
    FMasterVolumeRight: Integer;
    FOutputBackend: TZForkOutputBackend;
    FOutputDevice: Integer; { -1 = Auto/Default }
    FActive: Boolean;
    FEqualizerEnabled: Boolean;
    FEchoEnabled: Boolean;
    FLastError: string;
    FAbout: string;
    FShuttingDown: Boolean;
    FPosPending: Boolean;
    FPosMs: LongWord;
    FLenMs: LongWord;
    FOnError: TZForkErrorEvent;
    FOnOpen: TNotifyEvent;
    FOnClose: TNotifyEvent;
    FOnPlay: TNotifyEvent;
    FOnPause: TNotifyEvent;
    FOnResume: TNotifyEvent;
    FOnStop: TNotifyEvent;
    FOnNextSong: TZForkIndexEvent;
    FOnPosition: TZForkPositionEvent;
    FOnQueueChanged: TZForkQueueEvent;
    FOnStreamDone: TNotifyEvent;
    FOnBroadcastConnected: TZForkParamEvent;
    FOnBroadcastDisconnected: TZForkParamEvent;
    FOnBroadcastReconnecting: TZForkParamEvent;
    FOnBroadcastAuthFailed: TNotifyEvent;
    FOnBroadcastUnderrun: TZForkParamEvent;
    FClients: TFPList;
    procedure SetActive(AValue: Boolean);
    procedure SetFileName(const AValue: string);
    procedure SetMasterVolumeLeft(AValue: Integer);
    procedure SetMasterVolumeRight(AValue: Integer);
    procedure SetOutputBackend(AValue: TZForkOutputBackend);
    procedure SetOutputDevice(AValue: Integer);
    procedure SetEqualizerEnabled(AValue: Boolean);
    procedure SetEchoEnabled(AValue: Boolean);
    function GetAbout: string;
    procedure SetAbout(const AValue: string);
    procedure ApplyVolume;
    procedure ApplyEqualizer;
    procedure ApplyEcho;
    function BackendToInt: Integer;
    procedure Fail(const Msg: string);
    function EnsureHandle: Boolean;
    procedure DestroyHandle;
    function OkOrFail(Ok: Boolean): Boolean;
    function TryOk(Ok: Boolean): Boolean;
    procedure InstallNativeCallback;
    procedure HandleNativeMessage(Msg: Integer; Param1, Param2: LongWord);
    procedure DispatchOnMain(Msg: Integer; Param1, Param2: LongWord);
    procedure FirePosition;
    procedure AsyncEvent(Data: PtrInt);
    procedure AsyncPosition(Data: PtrInt);
    procedure StartIfReady;
    procedure NotifyClientsOpen;
    procedure NotifyClientsSong(SongIndex: Integer);
    procedure NotifyClientsClose;
    procedure NotifyClientsStop;
    class function NativeCallback(instance, userData: Pointer; message: Integer;
      param1, param2: LongWord): Integer; static; stdcall;
  protected
    procedure Loaded; override;
    procedure Notification(AComponent: TComponent; Operation: TOperation); override;
  public
    constructor Create(AOwner: TComponent); override;
    destructor Destroy; override;

    { Komponente anbinden (ID3/Spectrum/Broadcast implementieren IZForkPlayerClient). }
    procedure RegisterClient(AClient: TComponent);
    procedure UnregisterClient(AClient: TComponent);

    property Handle: TZFORK_HANDLE read FHandle;
    property LastError: string read FLastError;

    { OpenDialog (Design-Time „…“ oder zur Laufzeit). Setzt FileName. }
    function BrowseFileName: Boolean;
    { OpenDialog + OpenFile. }
    function BrowseAndOpen: Boolean;

    { ── Open / Queue ─────────────────────────────────────────────────────── }
    function Open: Boolean;
    function OpenFile(const APath: string; AFormat: Integer = ZFORK_FMT_AUTODETECT): Boolean;
    function LoadPlaylist(const APath: string; AFallbackFormat: Integer = ZFORK_FMT_AUTODETECT): Boolean;
    function AddFile(const APath: string; AFormat: Integer = ZFORK_FMT_AUTODETECT): Boolean;
    function RemoveQueueItem(Index: LongWord): Boolean;
    function MoveQueueItem(FromIndex, ToIndex: LongWord): Boolean;
    function GetQueueCount: LongWord;
    function GetQueueItemPath(Index: LongWord; out Path: string): Boolean;
    function OpenStream(Data: Pointer; Size: LongWord; AFormat: Integer): Boolean;
    function Close: Boolean;

    { Ausgabe-Geraete (Index -1 = Auto). Liste haengt vom OutputBackend ab. }
    function GetOutputDeviceCount: Integer;
    function GetOutputDeviceName(DeviceId: Integer): string;
    procedure ListOutputDevices(OutNames: TStrings);

    { ── Transport ────────────────────────────────────────────────────────── }
    function Play: Boolean;
    function PlayLoop(Count: LongWord): Boolean;
    function Pause: Boolean;
    function Resume: Boolean;
    function Stop: Boolean;
    function SeekMs(Origin: Integer; PositionMs: LongWord): Boolean;
    function GetPositionMs(out PositionMs: LongWord): Boolean;
    function GetStreamInfo(SongIndex: Integer; out Info: TStreamInfo): Boolean;
    function GetStatus(out Status: TStreamStatus): Boolean;

    { ── Volume / Pitch ───────────────────────────────────────────────────── }
    function SetPlayerVolume(Left, Right: Integer): Boolean;
    function GetPlayerVolume(out Left, Right: Integer): Boolean;
    function GetVUData(out Left, Right: Single): Boolean;
    function MixChannels(Mono: Boolean): Boolean;
    function SetRate(Rate: Single): Boolean;
    function SetPitch(Semitones: Single): Boolean;
    function SetTempo(Tempo: Single): Boolean;
    function SetBufferSize(Milliseconds: LongWord): Boolean;

    { ── Equalizer / Echo / DSP ───────────────────────────────────────────── }
    function SetEQBandGain(Band: Integer; GainDb: Single): Boolean;
    function GetEQBandGain(Band: Integer; out GainDb: Single): Boolean;
    function ResetEqualizer: Boolean;
    function SetEchoParams(const Params: TEchoParams): Boolean;
    function GetEchoParams(out Params: TEchoParams): Boolean;
    function EnableVocalCut(Enable: Boolean): Boolean;
    function EnableSideCut(Enable: Boolean): Boolean;
    function EnableStereoEnhancer(Enable: Boolean): Boolean;
    function SetStereoWidth(Width: Single): Boolean;
    function GetStereoWidth(out Width: Single): Boolean;

    { ── FFT / BPM / Analyse ──────────────────────────────────────────────── }
    function GetFFTData(Left, Right: PSingle; Points: LongWord): Boolean;
    function DetectBPM(Algo: Integer = ZFORK_BPM_ADVANCED): Single;
    function DetectFileBPM(const APath: string; Algo: Integer = ZFORK_BPM_ADVANCED): Single;
    function AnalyzeFile(const APath: string; Flags: LongWord;
      out Analysis: TZFORK_ANALYSIS_HANDLE): Boolean;
    procedure FreeAnalysis(Analysis: TZFORK_ANALYSIS_HANDLE);
    function GetAnalysisSummary(Analysis: TZFORK_ANALYSIS_HANDLE;
      out Bpm: Single; out LengthMs, Flags: LongWord): Boolean;
    function CopyAnalysisWaveform(Analysis: TZFORK_ANALYSIS_HANDLE;
      OutBins: PByte; MaxBins: LongWord): LongWord;
    function CopyAnalysisPeaks(Analysis: TZFORK_ANALYSIS_HANDLE;
      OutTimesMs, OutValues: PSingle; MaxPeaks: LongWord): LongWord;

    { ── Playlist-Optionen ────────────────────────────────────────────────── }
    function SetPlaylistAllowDuplicates(Allow: Boolean): Boolean;
    function GetPlaylistAllowDuplicates: Boolean;

    { ── ID3 ──────────────────────────────────────────────────────────────── }
    function LoadID3Ex(SongIndex: Integer; out Info: TID3InfoEx): Boolean;
    function LoadFileID3Ex(const APath: string; out Info: TID3InfoEx): Boolean;
    function LoadFileID3Picture(const APath: string; PicIndex: Integer;
      out Data: Pointer; out Size: LongWord; out PicType: Integer): Boolean;
    procedure FreeMemory(Ptr: Pointer);

    { ── Broadcast ────────────────────────────────────────────────────────── }
    function StartBroadcast(const Config: TBroadcastConfig): Boolean;
    function StopBroadcast: Boolean;
    function PushBroadcastPCM(Interleaved: PSingle; Frames: LongWord;
      Channels, SampleRate: Integer): Boolean;
    function StartBroadcastFile(const APath: string; AFormat: Integer = ZFORK_FMT_AUTODETECT): Boolean;
    function AttachBroadcastToCurrentPlayback(Enable: Boolean): Boolean;
    function SetBroadcastMetadata(const Title, Artist, Album: string): Boolean;
    function PushBroadcastMicPCM(Interleaved: PSingle; Frames: LongWord;
      Channels, SampleRate: Integer): Boolean;
    function SetBroadcastMicActive(Active: Boolean): Boolean;
    function SetBroadcastDucking(const Config: TBroadcastDuckingConfig): Boolean;
    function GetBroadcastDucking(out Config: TBroadcastDuckingConfig): Boolean;
    function StartBroadcastMicCapture(DeviceId, SampleRate, Channels: Integer): Boolean;
    function StopBroadcastMicCapture: Boolean;
    function GetBroadcastStats(out Stats: TBroadcastStats): Boolean;
    function GetBroadcastError: string;

    { ── Raw PCM-Callback (Audio-Thread; kein LCL!) ───────────────────────── }
    function SetPCMOutputCallback(Callback: TZForkPCMOutputCallback;
      UserData: Pointer; DisableBuiltInOutput: Boolean): Boolean;

    procedure ShowAbout;
  published
    property Active: Boolean read FActive write SetActive default False;
    { Object Inspector: „…“ oeffnet Dateidialog. Laufzeit: Open / OpenFile / BrowseAndOpen. }
    property FileName: string read FFileName write SetFileName;
    property AutoPlay: Boolean read FAutoPlay write FAutoPlay default False;
    property MasterVolumeLeft: Integer read FMasterVolumeLeft write SetMasterVolumeLeft default 100;
    property MasterVolumeRight: Integer read FMasterVolumeRight write SetMasterVolumeRight default 100;
    { API: Auto / WASAPI Exclusive / WaveOut — Default Auto. }
    property OutputBackend: TZForkOutputBackend read FOutputBackend write SetOutputBackend default obAuto;
    { Geraet: -1 = Auto (Standard). Inspector: Dropdown mit Namen. }
    property OutputDevice: Integer read FOutputDevice write SetOutputDevice default -1;
    property EqualizerEnabled: Boolean read FEqualizerEnabled write SetEqualizerEnabled default False;
    property EchoEnabled: Boolean read FEchoEnabled write SetEchoEnabled default False;
    { Object Inspector: Schaltflaeche „…“ oeffnet About/Lizenzen. }
    property About: string read GetAbout write SetAbout stored False;
    property OnOpen: TNotifyEvent read FOnOpen write FOnOpen;
    property OnClose: TNotifyEvent read FOnClose write FOnClose;
    property OnPlay: TNotifyEvent read FOnPlay write FOnPlay;
    property OnPause: TNotifyEvent read FOnPause write FOnPause;
    property OnResume: TNotifyEvent read FOnResume write FOnResume;
    property OnStop: TNotifyEvent read FOnStop write FOnStop;
    property OnNextSong: TZForkIndexEvent read FOnNextSong write FOnNextSong;
    property OnPosition: TZForkPositionEvent read FOnPosition write FOnPosition;
    property OnQueueChanged: TZForkQueueEvent read FOnQueueChanged write FOnQueueChanged;
    property OnStreamDone: TNotifyEvent read FOnStreamDone write FOnStreamDone;
    property OnError: TZForkErrorEvent read FOnError write FOnError;
    property OnBroadcastConnected: TZForkParamEvent read FOnBroadcastConnected write FOnBroadcastConnected;
    property OnBroadcastDisconnected: TZForkParamEvent read FOnBroadcastDisconnected write FOnBroadcastDisconnected;
    property OnBroadcastReconnecting: TZForkParamEvent read FOnBroadcastReconnecting write FOnBroadcastReconnecting;
    property OnBroadcastAuthFailed: TNotifyEvent read FOnBroadcastAuthFailed write FOnBroadcastAuthFailed;
    property OnBroadcastUnderrun: TZForkParamEvent read FOnBroadcastUnderrun write FOnBroadcastUnderrun;
  end;

implementation

uses
  ZForkAbout;

type
  PZForkAsyncEvt = ^TZForkAsyncEvt;
  TZForkAsyncEvt = record
    Msg: Integer;
    Param1, Param2: LongWord;
  end;

const
  CAudioFilter =
    'Audio|*.mp3;*.wav;*.flac;*.ogg;*.opus;*.aiff;*.aif;*.m4a;*.aac;*.cda|' +
    'Playlists|*.m3u;*.m3u8;*.pls|' +
    'All|*.*';

{ TZForkPlayer }

constructor TZForkPlayer.Create(AOwner: TComponent);
begin
  inherited Create(AOwner);
  FHandle := nil;
  FActive := False;
  FAutoPlay := False;
  FMasterVolumeLeft := 100;
  FMasterVolumeRight := 100;
  FOutputBackend := obAuto;
  FOutputDevice := -1;
  FEqualizerEnabled := False;
  FEchoEnabled := False;
  FLastError := '';
  FAbout := '(About…)';
  FShuttingDown := False;
  FPosPending := False;
  FClients := TFPList.Create;
end;

destructor TZForkPlayer.Destroy;
begin
  FShuttingDown := True;
  if FClients <> nil then
    FClients.Clear;
  DestroyHandle;
  FreeAndNil(FClients);
  inherited Destroy;
end;

procedure TZForkPlayer.RegisterClient(AClient: TComponent);
var
  Intf: IZForkPlayerClient;
begin
  if (AClient = nil) or (FClients = nil) then Exit;
  if not Supports(AClient, IZForkPlayerClient, Intf) then Exit;
  if FClients.IndexOf(AClient) < 0 then
  begin
    FClients.Add(AClient);
    AClient.FreeNotification(Self);
  end;
end;

procedure TZForkPlayer.UnregisterClient(AClient: TComponent);
begin
  if (AClient = nil) or (FClients = nil) then Exit;
  FClients.Remove(AClient);
end;

procedure TZForkPlayer.NotifyClientsOpen;
var
  I: Integer;
  Intf: IZForkPlayerClient;
begin
  if FClients = nil then Exit;
  for I := 0 to FClients.Count - 1 do
    if Supports(TObject(FClients[I]), IZForkPlayerClient, Intf) then
      Intf.NotifyPlayerOpen(FFileName);
end;

procedure TZForkPlayer.NotifyClientsSong(SongIndex: Integer);
var
  I: Integer;
  Intf: IZForkPlayerClient;
  Path: string;
begin
  if (SongIndex >= 0) and GetQueueItemPath(LongWord(SongIndex), Path) and (Path <> '') then
    FFileName := Path;
  if FClients = nil then Exit;
  for I := 0 to FClients.Count - 1 do
    if Supports(TObject(FClients[I]), IZForkPlayerClient, Intf) then
      Intf.NotifyPlayerSong(SongIndex);
end;

procedure TZForkPlayer.NotifyClientsClose;
var
  I: Integer;
  Intf: IZForkPlayerClient;
begin
  if FClients = nil then Exit;
  for I := 0 to FClients.Count - 1 do
    if Supports(TObject(FClients[I]), IZForkPlayerClient, Intf) then
      Intf.NotifyPlayerClose;
end;

procedure TZForkPlayer.NotifyClientsStop;
var
  I: Integer;
  Intf: IZForkPlayerClient;
begin
  if FClients = nil then Exit;
  for I := 0 to FClients.Count - 1 do
    if Supports(TObject(FClients[I]), IZForkPlayerClient, Intf) then
      Intf.NotifyPlayerStop;
end;

procedure TZForkPlayer.Loaded;
begin
  inherited Loaded;
  if csDesigning in ComponentState then Exit;
  if FActive then
    StartIfReady;
end;

procedure TZForkPlayer.Notification(AComponent: TComponent; Operation: TOperation);
begin
  inherited Notification(AComponent, Operation);
  if (Operation = opRemove) and (FClients <> nil) then
    FClients.Remove(AComponent);
end;

procedure TZForkPlayer.StartIfReady;
begin
  if FShuttingDown or (csDesigning in ComponentState) or (csLoading in ComponentState) then
    Exit;
  if not FActive then Exit;
  if Trim(FFileName) = '' then
  begin
    EnsureHandle;
    Exit;
  end;
  Open;
end;

function TZForkPlayer.GetAbout: string;
begin
  Result := '(About…)';
end;

procedure TZForkPlayer.SetAbout(const AValue: string);
begin
  FAbout := AValue;
  if not (csLoading in ComponentState) and not (csDesigning in ComponentState) then
    ShowAbout;
end;

function TZForkPlayer.BackendToInt: Integer;
begin
  case FOutputBackend of
    obWasapi:  Result := ZFORK_OUTPUT_WASAPI;
    obWaveOut: Result := ZFORK_OUTPUT_WAVEOUT;
  else
    Result := ZFORK_OUTPUT_AUTO;
  end;
end;

procedure TZForkPlayer.Fail(const Msg: string);
begin
  FLastError := Msg;
  if Assigned(FOnError) then
    FOnError(Self, Msg);
end;

function TZForkPlayer.OkOrFail(Ok: Boolean): Boolean;
begin
  Result := Ok;
  if not Ok then
  begin
    if FHandle <> nil then
      Fail(ZForkGetError(FHandle))
    else
      Fail('Handle is nil');
  end;
end;

{ Polling-APIs: nur False, kein OnError-Spam (kein Song / Idle) }
function TZForkPlayer.TryOk(Ok: Boolean): Boolean;
begin
  Result := Ok;
  if not Ok then
  begin
    if FHandle <> nil then
      FLastError := ZForkGetError(FHandle)
    else
      FLastError := 'Handle is nil';
  end;
end;

procedure TZForkPlayer.DestroyHandle;
begin
  if FHandle <> nil then
  begin
    zfork_SetCallback(FHandle, nil, nil);
    zfork_Stop(FHandle);
    zfork_Close(FHandle);
    zfork_Destroy(FHandle);
    FHandle := nil;
  end;
  FActive := False;
end;

function TZForkPlayer.EnsureHandle: Boolean;
var
  Cfg: TZForkConfig;
begin
  Result := FHandle <> nil;
  if Result then Exit;
  ZForkInitConfig(Cfg);
  Cfg.outputBackend := BackendToInt;
  Cfg.waveOutDeviceId := FOutputDevice;
  Cfg.masterVolumeLeft := FMasterVolumeLeft;
  Cfg.masterVolumeRight := FMasterVolumeRight;
  FHandle := zfork_CreateWithConfig(@Cfg);
  if FHandle = nil then
  begin
    Fail('zfork_CreateWithConfig failed');
    FActive := False;
    Exit(False);
  end;
  FActive := True;
  InstallNativeCallback;
  ApplyVolume;
  ApplyEqualizer;
  ApplyEcho;
  Result := True;
end;

procedure TZForkPlayer.InstallNativeCallback;
begin
  if FHandle = nil then Exit;
  if zfork_SetCallback(FHandle, @TZForkPlayer.NativeCallback, Self) = 0 then
    Fail(ZForkGetError(FHandle));
end;

class function TZForkPlayer.NativeCallback(instance, userData: Pointer; message: Integer;
  param1, param2: LongWord): Integer; stdcall;
var
  P: TZForkPlayer;
begin
  Result := 0;
  P := TZForkPlayer(userData);
  if (P = nil) or P.FShuttingDown then Exit;
  P.HandleNativeMessage(message, param1, param2);
end;

procedure TZForkPlayer.HandleNativeMessage(Msg: Integer; Param1, Param2: LongWord);
begin
  if FShuttingDown or (csDestroying in ComponentState) then Exit;
  if Msg = ZFORK_CB_POSITION_ASYNC then
  begin
    FPosMs := Param1;
    FLenMs := Param2;
    if not FPosPending then
    begin
      FPosPending := True;
      if Application <> nil then
        Application.QueueAsyncCall(@AsyncPosition, 0)
      else
        FirePosition;
    end;
    Exit;
  end;
  DispatchOnMain(Msg, Param1, Param2);
end;

procedure TZForkPlayer.DispatchOnMain(Msg: Integer; Param1, Param2: LongWord);
var
  E: PZForkAsyncEvt;
begin
  if Application = nil then
  begin
    case Msg of
      ZFORK_CB_PLAY_ASYNC: if Assigned(FOnPlay) then FOnPlay(Self);
      ZFORK_CB_PAUSE_ASYNC: if Assigned(FOnPause) then FOnPause(Self);
      ZFORK_CB_RESUME_ASYNC: if Assigned(FOnResume) then FOnResume(Self);
      ZFORK_CB_STOP_ASYNC:
        begin
          if Assigned(FOnStop) then FOnStop(Self);
          NotifyClientsStop;
        end;
      ZFORK_CB_NEXT_SONG_ASYNC:
        begin
          if Assigned(FOnNextSong) then FOnNextSong(Self, Integer(Param1));
          NotifyClientsSong(Integer(Param1));
        end;
      ZFORK_CB_QUEUE_CHANGED_ASYNC:
        if Assigned(FOnQueueChanged) then FOnQueueChanged(Self, Param1);
      ZFORK_CB_QUEUE_ITEM_ADDED_ASYNC:
        if Assigned(FOnQueueChanged) then FOnQueueChanged(Self, Param2);
      ZFORK_CB_STREAM_BUFFER_DONE_ASYNC: if Assigned(FOnStreamDone) then FOnStreamDone(Self);
      ZFORK_CB_BROADCAST_CONNECTED_ASYNC:
        if Assigned(FOnBroadcastConnected) then FOnBroadcastConnected(Self, Param1, Param2);
      ZFORK_CB_BROADCAST_DISCONNECTED_ASYNC:
        if Assigned(FOnBroadcastDisconnected) then FOnBroadcastDisconnected(Self, Param1, Param2);
      ZFORK_CB_BROADCAST_RECONNECTING_ASYNC:
        if Assigned(FOnBroadcastReconnecting) then FOnBroadcastReconnecting(Self, Param1, Param2);
      ZFORK_CB_BROADCAST_AUTH_FAILED_ASYNC:
        if Assigned(FOnBroadcastAuthFailed) then FOnBroadcastAuthFailed(Self);
      ZFORK_CB_BROADCAST_UNDERRUN_ASYNC:
        if Assigned(FOnBroadcastUnderrun) then FOnBroadcastUnderrun(Self, Param1, Param2);
    end;
    Exit;
  end;
  New(E);
  E^.Msg := Msg;
  E^.Param1 := Param1;
  E^.Param2 := Param2;
  Application.QueueAsyncCall(@AsyncEvent, PtrInt(E));
end;

procedure TZForkPlayer.AsyncEvent(Data: PtrInt);
var
  E: PZForkAsyncEvt;
begin
  E := PZForkAsyncEvt(Pointer(Data));
  try
    if FShuttingDown or (csDestroying in ComponentState) then Exit;
    case E^.Msg of
      ZFORK_CB_PLAY_ASYNC:
        if Assigned(FOnPlay) then FOnPlay(Self);
      ZFORK_CB_PAUSE_ASYNC:
        if Assigned(FOnPause) then FOnPause(Self);
      ZFORK_CB_RESUME_ASYNC:
        if Assigned(FOnResume) then FOnResume(Self);
      ZFORK_CB_STOP_ASYNC:
        begin
          if Assigned(FOnStop) then FOnStop(Self);
          NotifyClientsStop;
        end;
      ZFORK_CB_NEXT_SONG_ASYNC:
        begin
          if Assigned(FOnNextSong) then FOnNextSong(Self, Integer(E^.Param1));
          NotifyClientsSong(Integer(E^.Param1));
        end;
      ZFORK_CB_QUEUE_CHANGED_ASYNC:
        if Assigned(FOnQueueChanged) then FOnQueueChanged(Self, E^.Param1);
      ZFORK_CB_QUEUE_ITEM_ADDED_ASYNC:
        if Assigned(FOnQueueChanged) then FOnQueueChanged(Self, E^.Param2);
      ZFORK_CB_STREAM_BUFFER_DONE_ASYNC:
        if Assigned(FOnStreamDone) then FOnStreamDone(Self);
      ZFORK_CB_BROADCAST_CONNECTED_ASYNC:
        if Assigned(FOnBroadcastConnected) then
          FOnBroadcastConnected(Self, E^.Param1, E^.Param2);
      ZFORK_CB_BROADCAST_DISCONNECTED_ASYNC:
        if Assigned(FOnBroadcastDisconnected) then
          FOnBroadcastDisconnected(Self, E^.Param1, E^.Param2);
      ZFORK_CB_BROADCAST_RECONNECTING_ASYNC:
        if Assigned(FOnBroadcastReconnecting) then
          FOnBroadcastReconnecting(Self, E^.Param1, E^.Param2);
      ZFORK_CB_BROADCAST_AUTH_FAILED_ASYNC:
        if Assigned(FOnBroadcastAuthFailed) then FOnBroadcastAuthFailed(Self);
      ZFORK_CB_BROADCAST_UNDERRUN_ASYNC:
        if Assigned(FOnBroadcastUnderrun) then
          FOnBroadcastUnderrun(Self, E^.Param1, E^.Param2);
    end;
  finally
    Dispose(E);
  end;
end;

procedure TZForkPlayer.AsyncPosition({%H-}Data: PtrInt);
begin
  if FShuttingDown or (csDestroying in ComponentState) then Exit;
  FirePosition;
end;

procedure TZForkPlayer.FirePosition;
begin
  FPosPending := False;
  if Assigned(FOnPosition) then
    FOnPosition(Self, FPosMs, FLenMs);
end;

function TZForkPlayer.BrowseFileName: Boolean;
var
  Dlg: TOpenDialog;
begin
  Result := False;
  Dlg := TOpenDialog.Create(nil);
  try
    Dlg.Title := 'Audio oeffnen';
    Dlg.Filter := CAudioFilter;
    Dlg.Options := Dlg.Options + [ofFileMustExist, ofPathMustExist];
    if FFileName <> '' then
      Dlg.FileName := FFileName;
    if Dlg.Execute then
    begin
      FFileName := Dlg.FileName;
      Result := True;
    end;
  finally
    Dlg.Free;
  end;
end;

function TZForkPlayer.BrowseAndOpen: Boolean;
begin
  Result := BrowseFileName and Open;
end;

procedure TZForkPlayer.ApplyVolume;
begin
  if FHandle = nil then Exit;
  if not ZForkSetMasterVolume(FHandle, FMasterVolumeLeft, FMasterVolumeRight) then
    Fail(ZForkGetError(FHandle));
end;

procedure TZForkPlayer.ApplyEqualizer;
begin
  if FHandle = nil then Exit;
  if zfork_EnableEqualizer(FHandle, Ord(FEqualizerEnabled)) = 0 then
    Fail(ZForkGetError(FHandle));
end;

procedure TZForkPlayer.ApplyEcho;
begin
  if FHandle = nil then Exit;
  if zfork_EnableEcho(FHandle, Ord(FEchoEnabled)) = 0 then
    Fail(ZForkGetError(FHandle));
end;

procedure TZForkPlayer.SetActive(AValue: Boolean);
begin
  if FActive = AValue then Exit;
  if csLoading in ComponentState then begin FActive := AValue; Exit; end;
  if AValue then
  begin
    if csDesigning in ComponentState then begin FActive := True; Exit; end;
    FActive := True;
    StartIfReady;
  end
  else
    DestroyHandle;
end;

procedure TZForkPlayer.SetFileName(const AValue: string);
begin
  if FFileName = AValue then Exit;
  FFileName := AValue;
  if not (csLoading in ComponentState) and not (csDesigning in ComponentState) then
    StartIfReady;
end;

procedure TZForkPlayer.SetMasterVolumeLeft(AValue: Integer);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 100 then AValue := 100;
  if FMasterVolumeLeft = AValue then Exit;
  FMasterVolumeLeft := AValue;
  ApplyVolume;
end;

procedure TZForkPlayer.SetMasterVolumeRight(AValue: Integer);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 100 then AValue := 100;
  if FMasterVolumeRight = AValue then Exit;
  FMasterVolumeRight := AValue;
  ApplyVolume;
end;

procedure TZForkPlayer.SetOutputBackend(AValue: TZForkOutputBackend);
var
  WasActive: Boolean;
begin
  if FOutputBackend = AValue then Exit;
  FOutputBackend := AValue;
  if (FHandle <> nil) and not (csDesigning in ComponentState)
     and not (csLoading in ComponentState) then
  begin
    WasActive := FActive;
    DestroyHandle;
    if WasActive then
    begin
      FActive := True;
      StartIfReady;
    end;
  end;
end;

procedure TZForkPlayer.SetOutputDevice(AValue: Integer);
var
  WasActive: Boolean;
begin
  if AValue < -1 then AValue := -1;
  if FOutputDevice = AValue then Exit;
  FOutputDevice := AValue;
  if csLoading in ComponentState then Exit;
  if csDesigning in ComponentState then Exit;
  if FHandle <> nil then
  begin
    WasActive := FActive;
    DestroyHandle;
    if WasActive then
    begin
      FActive := True;
      StartIfReady;
    end;
  end;
end;

function TZForkPlayer.GetOutputDeviceCount: Integer;
var
  List: TStringList;
begin
  if FHandle <> nil then
    Exit(zfork_EnumerateOutputDevices(FHandle));
  List := TStringList.Create;
  try
    ZForkListOutputDevices(BackendToInt, List);
    Result := List.Count - 1; { ohne Auto-Eintrag }
    if Result < 0 then Result := 0;
  finally
    List.Free;
  end;
end;

function TZForkPlayer.GetOutputDeviceName(DeviceId: Integer): string;
begin
  Result := ZForkGetOutputDeviceName(BackendToInt, DeviceId);
end;

procedure TZForkPlayer.ListOutputDevices(OutNames: TStrings);
begin
  ZForkListOutputDevices(BackendToInt, OutNames);
end;

procedure TZForkPlayer.SetEqualizerEnabled(AValue: Boolean);
begin
  if FEqualizerEnabled = AValue then Exit;
  FEqualizerEnabled := AValue;
  ApplyEqualizer;
end;

procedure TZForkPlayer.SetEchoEnabled(AValue: Boolean);
begin
  if FEchoEnabled = AValue then Exit;
  FEchoEnabled := AValue;
  ApplyEcho;
end;

function TZForkPlayer.Open: Boolean;
begin
  Result := OpenFile(FFileName);
end;

function TZForkPlayer.OpenFile(const APath: string; AFormat: Integer): Boolean;
begin
  Result := False;
  FLastError := '';
  if Trim(APath) = '' then begin Fail('FileName is empty'); Exit; end;
  if not EnsureHandle then Exit;
  FFileName := APath;
  if not ZForkOpenFile(FHandle, UnicodeString(APath), AFormat) then
  begin Fail(ZForkGetError(FHandle)); Exit; end;
  ApplyVolume; ApplyEqualizer; ApplyEcho;
  Result := True;
  if Assigned(FOnOpen) then FOnOpen(Self);
  NotifyClientsOpen;
  if FAutoPlay then Play;
end;

function TZForkPlayer.LoadPlaylist(const APath: string; AFallbackFormat: Integer): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(ZForkLoadPlaylist(FHandle, UnicodeString(APath), AFallbackFormat));
end;

function TZForkPlayer.AddFile(const APath: string; AFormat: Integer): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(ZForkAddFile(FHandle, UnicodeString(APath), AFormat));
end;

function TZForkPlayer.RemoveQueueItem(Index: LongWord): Boolean;
begin
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(zfork_RemoveQueueItem(FHandle, Index) <> 0);
end;

function TZForkPlayer.MoveQueueItem(FromIndex, ToIndex: LongWord): Boolean;
begin
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(zfork_MoveQueueItem(FHandle, FromIndex, ToIndex) <> 0);
end;

function TZForkPlayer.GetQueueCount: LongWord;
begin
  if FHandle = nil then Result := 0
  else Result := zfork_GetQueueCount(FHandle);
end;

function TZForkPlayer.GetQueueItemPath(Index: LongWord; out Path: string): Boolean;
var
  Buf: array[0..1023] of AnsiChar;
begin
  Result := False;
  Path := '';
  if FHandle = nil then Exit;
  FillChar(Buf, SizeOf(Buf), 0);
  if zfork_GetQueueItemPathA(FHandle, Index, @Buf[0], SizeOf(Buf)) = 0 then Exit;
  Path := string(AnsiString(PAnsiChar(@Buf[0])));
  Result := Path <> '';
end;

function TZForkPlayer.OpenStream(Data: Pointer; Size: LongWord; AFormat: Integer): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_OpenStream(FHandle, Data, Size, AFormat) <> 0);
end;

function TZForkPlayer.Close: Boolean;
begin
  Result := True;
  if FHandle <> nil then
  begin
    zfork_Close(FHandle);
    if Assigned(FOnClose) then FOnClose(Self);
    NotifyClientsClose;
  end;
end;

function TZForkPlayer.Play: Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(ZForkPlay(FHandle));
end;

function TZForkPlayer.PlayLoop(Count: LongWord): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_PlayLoop(FHandle, Count) <> 0);
end;

function TZForkPlayer.Pause: Boolean;
begin
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(ZForkPause(FHandle));
end;

function TZForkPlayer.Resume: Boolean;
begin
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(ZForkResume(FHandle));
end;

function TZForkPlayer.Stop: Boolean;
begin
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(ZForkStop(FHandle));
  { OnStop kommt primaer ueber ZFORK_CB_STOP_ASYNC (Hauptthread). }
end;

function TZForkPlayer.SeekMs(Origin: Integer; PositionMs: LongWord): Boolean;
begin
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(zfork_SeekMs(FHandle, Origin, PositionMs) <> 0);
end;

function TZForkPlayer.GetPositionMs(out PositionMs: LongWord): Boolean;
begin
  PositionMs := 0;
  Result := False;
  if FHandle = nil then Exit;
  Result := TryOk(zfork_GetPositionMs(FHandle, @PositionMs) <> 0);
end;

function TZForkPlayer.GetStreamInfo(SongIndex: Integer; out Info: TStreamInfo): Boolean;
begin
  FillChar(Info, SizeOf(Info), 0);
  Result := False;
  if FHandle = nil then Exit;
  Result := TryOk(zfork_GetStreamInfo(FHandle, SongIndex, @Info) <> 0);
end;

function TZForkPlayer.GetStatus(out Status: TStreamStatus): Boolean;
begin
  FillChar(Status, SizeOf(Status), 0);
  Result := False;
  if FHandle = nil then Exit;
  Result := TryOk(zfork_GetStatus(FHandle, @Status) <> 0);
end;

function TZForkPlayer.SetPlayerVolume(Left, Right: Integer): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_SetPlayerVolume(FHandle, Left, Right) <> 0);
end;

function TZForkPlayer.GetPlayerVolume(out Left, Right: Integer): Boolean;
begin
  Left := 0; Right := 0;
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(zfork_GetPlayerVolume(FHandle, @Left, @Right) <> 0);
end;

function TZForkPlayer.GetVUData(out Left, Right: Single): Boolean;
begin
  Left := 0; Right := 0;
  Result := False;
  if FHandle = nil then Exit;
  Result := TryOk(zfork_GetVUData(FHandle, @Left, @Right) <> 0);
end;

function TZForkPlayer.MixChannels(Mono: Boolean): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_MixChannels(FHandle, Ord(Mono)) <> 0);
end;

function TZForkPlayer.SetRate(Rate: Single): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_SetRate(FHandle, Rate) <> 0);
end;

function TZForkPlayer.SetPitch(Semitones: Single): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_SetPitch(FHandle, Semitones) <> 0);
end;

function TZForkPlayer.SetTempo(Tempo: Single): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_SetTempo(FHandle, Tempo) <> 0);
end;

function TZForkPlayer.SetBufferSize(Milliseconds: LongWord): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_SetBufferSize(FHandle, Milliseconds) <> 0);
end;

function TZForkPlayer.SetEQBandGain(Band: Integer; GainDb: Single): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_SetEQBandGain(FHandle, Band, GainDb) <> 0);
end;

function TZForkPlayer.GetEQBandGain(Band: Integer; out GainDb: Single): Boolean;
begin
  GainDb := 0;
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(zfork_GetEQBandGain(FHandle, Band, @GainDb) <> 0);
end;

function TZForkPlayer.ResetEqualizer: Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_ResetEqualizer(FHandle) <> 0);
end;

function TZForkPlayer.SetEchoParams(const Params: TEchoParams): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_SetEchoParams(FHandle, @Params) <> 0);
end;

function TZForkPlayer.GetEchoParams(out Params: TEchoParams): Boolean;
begin
  FillChar(Params, SizeOf(Params), 0);
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(zfork_GetEchoParams(FHandle, @Params) <> 0);
end;

function TZForkPlayer.EnableVocalCut(Enable: Boolean): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_EnableVocalCut(FHandle, Ord(Enable)) <> 0);
end;

function TZForkPlayer.EnableSideCut(Enable: Boolean): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_EnableSideCut(FHandle, Ord(Enable)) <> 0);
end;

function TZForkPlayer.EnableStereoEnhancer(Enable: Boolean): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_EnableStereoEnhancer(FHandle, Ord(Enable)) <> 0);
end;

function TZForkPlayer.SetStereoWidth(Width: Single): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_SetStereoWidth(FHandle, Width) <> 0);
end;

function TZForkPlayer.GetStereoWidth(out Width: Single): Boolean;
begin
  Width := 1;
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(zfork_GetStereoWidth(FHandle, @Width) <> 0);
end;

function TZForkPlayer.GetFFTData(Left, Right: PSingle; Points: LongWord): Boolean;
begin
  Result := False;
  if FHandle = nil then Exit;
  Result := TryOk(zfork_GetFFTData(FHandle, Left, Right, Points) <> 0);
end;

function TZForkPlayer.DetectBPM(Algo: Integer): Single;
begin
  Result := 0;
  if not EnsureHandle then Exit;
  Result := ZForkDetectBPM(FHandle, Algo);
end;

function TZForkPlayer.DetectFileBPM(const APath: string; Algo: Integer): Single;
var
  Bpm: Single;
begin
  Result := 0;
  Bpm := 0;
  if not EnsureHandle then Exit;
  if zfork_DetectFileBPMA(FHandle, PAnsiChar(AnsiString(APath)), Algo, @Bpm) <> 0 then
    Result := Bpm
  else
    Fail(ZForkGetError(FHandle));
end;

function TZForkPlayer.AnalyzeFile(const APath: string; Flags: LongWord;
  out Analysis: TZFORK_ANALYSIS_HANDLE): Boolean;
begin
  Analysis := nil;
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_AnalyzeFile(FHandle, PWideChar(UnicodeString(APath)),
    Flags, @Analysis) <> 0);
end;

procedure TZForkPlayer.FreeAnalysis(Analysis: TZFORK_ANALYSIS_HANDLE);
begin
  if (FHandle <> nil) and (Analysis <> nil) then
    zfork_FreeAnalysis(FHandle, Analysis);
end;

function TZForkPlayer.GetAnalysisSummary(Analysis: TZFORK_ANALYSIS_HANDLE;
  out Bpm: Single; out LengthMs, Flags: LongWord): Boolean;
begin
  Bpm := 0; LengthMs := 0; Flags := 0;
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(zfork_GetAnalysisSummary(FHandle, Analysis, @Bpm, @LengthMs, @Flags) <> 0);
end;

function TZForkPlayer.CopyAnalysisWaveform(Analysis: TZFORK_ANALYSIS_HANDLE;
  OutBins: PByte; MaxBins: LongWord): LongWord;
begin
  if FHandle = nil then Result := 0
  else Result := zfork_CopyAnalysisWaveform(FHandle, Analysis, OutBins, MaxBins);
end;

function TZForkPlayer.CopyAnalysisPeaks(Analysis: TZFORK_ANALYSIS_HANDLE;
  OutTimesMs, OutValues: PSingle; MaxPeaks: LongWord): LongWord;
begin
  if FHandle = nil then Result := 0
  else Result := zfork_CopyAnalysisPeaks(FHandle, Analysis, OutTimesMs, OutValues, MaxPeaks);
end;

function TZForkPlayer.SetPlaylistAllowDuplicates(Allow: Boolean): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_SetPlaylistAllowDuplicates(FHandle, Ord(Allow)) <> 0);
end;

function TZForkPlayer.GetPlaylistAllowDuplicates: Boolean;
begin
  Result := False;
  if FHandle <> nil then
    Result := zfork_GetPlaylistAllowDuplicates(FHandle) <> 0;
end;

function TZForkPlayer.LoadID3Ex(SongIndex: Integer; out Info: TID3InfoEx): Boolean;
begin
  FillChar(Info, SizeOf(Info), 0);
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_LoadID3Ex(FHandle, SongIndex, @Info) <> 0);
end;

function TZForkPlayer.LoadFileID3Ex(const APath: string; out Info: TID3InfoEx): Boolean;
begin
  FillChar(Info, SizeOf(Info), 0);
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_LoadFileID3ExA(FHandle, PAnsiChar(AnsiString(APath)), @Info) <> 0);
end;

function TZForkPlayer.LoadFileID3Picture(const APath: string; PicIndex: Integer;
  out Data: Pointer; out Size: LongWord; out PicType: Integer): Boolean;
begin
  Data := nil; Size := 0; PicType := -1;
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_LoadFileID3PictureA(FHandle, PAnsiChar(AnsiString(APath)),
    PicIndex, @Data, @Size, @PicType) <> 0);
end;

procedure TZForkPlayer.FreeMemory(Ptr: Pointer);
begin
  if Ptr <> nil then
    zfork_FreeMemory(Ptr);
end;

function TZForkPlayer.StartBroadcast(const Config: TBroadcastConfig): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_StartBroadcast(FHandle, @Config) <> 0);
  if not Result then
    Fail(ZForkGetBroadcastError(FHandle));
end;

function TZForkPlayer.StopBroadcast: Boolean;
begin
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(zfork_StopBroadcast(FHandle) <> 0);
end;

function TZForkPlayer.PushBroadcastPCM(Interleaved: PSingle; Frames: LongWord;
  Channels, SampleRate: Integer): Boolean;
begin
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(zfork_PushBroadcastPCM(FHandle, Interleaved, Frames, Channels, SampleRate) <> 0);
end;

function TZForkPlayer.StartBroadcastFile(const APath: string; AFormat: Integer): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_StartBroadcastFile(FHandle, PWideChar(UnicodeString(APath)), AFormat) <> 0);
end;

function TZForkPlayer.AttachBroadcastToCurrentPlayback(Enable: Boolean): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_AttachBroadcastToCurrentPlayback(FHandle, Ord(Enable)) <> 0);
end;

function TZForkPlayer.SetBroadcastMetadata(const Title, Artist, Album: string): Boolean;
begin
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(zfork_SetBroadcastMetadata(FHandle,
    PAnsiChar(AnsiString(Title)), PAnsiChar(AnsiString(Artist)),
    PAnsiChar(AnsiString(Album))) <> 0);
end;

function TZForkPlayer.PushBroadcastMicPCM(Interleaved: PSingle; Frames: LongWord;
  Channels, SampleRate: Integer): Boolean;
begin
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(zfork_PushBroadcastMicPCM(FHandle, Interleaved, Frames, Channels, SampleRate) <> 0);
end;

function TZForkPlayer.SetBroadcastMicActive(Active: Boolean): Boolean;
begin
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(zfork_SetBroadcastMicActive(FHandle, Ord(Active)) <> 0);
end;

function TZForkPlayer.SetBroadcastDucking(const Config: TBroadcastDuckingConfig): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_SetBroadcastDucking(FHandle, @Config) <> 0);
end;

function TZForkPlayer.GetBroadcastDucking(out Config: TBroadcastDuckingConfig): Boolean;
begin
  FillChar(Config, SizeOf(Config), 0);
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(zfork_GetBroadcastDucking(FHandle, @Config) <> 0);
end;

function TZForkPlayer.StartBroadcastMicCapture(DeviceId, SampleRate, Channels: Integer): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_StartBroadcastMicCapture(FHandle, DeviceId, SampleRate, Channels) <> 0);
end;

function TZForkPlayer.StopBroadcastMicCapture: Boolean;
begin
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(zfork_StopBroadcastMicCapture(FHandle) <> 0);
end;

function TZForkPlayer.GetBroadcastStats(out Stats: TBroadcastStats): Boolean;
begin
  FillChar(Stats, SizeOf(Stats), 0);
  Result := False;
  if FHandle = nil then Exit;
  Result := OkOrFail(zfork_GetBroadcastStats(FHandle, @Stats) <> 0);
end;

function TZForkPlayer.GetBroadcastError: string;
begin
  if FHandle = nil then Result := 'Handle is nil'
  else Result := ZForkGetBroadcastError(FHandle);
end;

function TZForkPlayer.SetPCMOutputCallback(Callback: TZForkPCMOutputCallback;
  UserData: Pointer; DisableBuiltInOutput: Boolean): Boolean;
begin
  Result := False;
  if not EnsureHandle then Exit;
  Result := OkOrFail(zfork_SetPCMOutputCallback(FHandle, Callback, UserData,
    Ord(DisableBuiltInOutput)) <> 0);
end;

procedure TZForkPlayer.ShowAbout;
begin
  ShowZForkAbout;
end;

end.
