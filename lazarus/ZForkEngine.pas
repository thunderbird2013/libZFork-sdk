{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  TZForkAudioEngine — Game-Audio-Engine als LCL-Komponente (libZFork2)

  Was die Komponente kann:
  - Active startet Context + periodisches Update (Voice-Events auf UI-Thread)
  - OutputBackend waehlen; Standard-Gruppen music/sfx/ui
  - LoadSound / UnloadSound / PlaySound; Bank laden und PlayEvent
  - Voice-Steuerung (Volume/Pan/Pitch/Tempo/Loop/Position/Marker)
  - Gruppen: Volume/Mute/Levels, Echo/Reverb setzen oder loeschen
  - Listener 2D/3D; Events VoiceStart/Finished/Loop/PositionCrossed
  - Client-Anbindung (IZForkEngineClient) z.B. fuer TZForkSoundBank
******************************************************************************}
unit ZForkEngine;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, ExtCtrls, LibZFork2, ZForkIntf;

type
  TZFork2OutputBackend = (
    zbAuto,
    zbWasapi,
    zbWaveOut
  );

  TZFork2SoundType = (
    zstPreDecoded,
    zstStreaming
  );

  TZFork2VoiceEvent = procedure(Sender: TObject; EventType: Integer;
    Voice: TZF2_VOICE; Param1: LongWord; Param2: Single) of object;

  TZFork2ErrorEvent = procedure(Sender: TObject; const Msg: string) of object;

  { TZForkAudioEngine — Context + Update-Timer + Events }
  TZForkAudioEngine = class(TComponent)
  private
    FActive: Boolean;
    FOutputBackend: TZFork2OutputBackend;
    FAutoUpdate: Boolean;
    FUpdateInterval: Integer;
    FAutoDefaultGroups: Boolean;
    FGroups: TStrings;
    FAbout: string;
    FLastError: string;
    FContext: TZF2_CONTEXT;
    FTimer: TTimer;
    FSounds: TList;
    FBanks: TList;
    FClients: TFPList;
    FOnVoiceEvent: TZFork2VoiceEvent;
    FOnVoiceStart: TZFork2VoiceEvent;
    FOnVoiceFinished: TZFork2VoiceEvent;
    FOnVoiceLoop: TZFork2VoiceEvent;
    FOnPositionCrossed: TZFork2VoiceEvent;
    FOnStarted: TNotifyEvent;
    FOnStopped: TNotifyEvent;
    FOnError: TZFork2ErrorEvent;
    procedure SetActive(AValue: Boolean);
    procedure SetOutputBackend(AValue: TZFork2OutputBackend);
    procedure SetAutoUpdate(AValue: Boolean);
    procedure SetUpdateInterval(AValue: Integer);
    procedure SetGroups(AValue: TStrings);
    function GetAbout: string;
    procedure SetAbout(const AValue: string);
    procedure Fail(const Msg: string);
    procedure TimerTick(Sender: TObject);
    procedure UpdateTimer;
    procedure ApplyCallback;
    class procedure EngineCallback(eventType: Integer; voice: TZF2_VOICE;
      param1: LongWord; param2: Single; userData: Pointer); cdecl; static;
    procedure HandleCallback(eventType: Integer; voice: TZF2_VOICE;
      param1: LongWord; param2: Single);
    function BackendToInt: Integer;
    procedure TrackSound(S: TZF2_SOUND);
    procedure TrackBank(B: TZF2_BANK);
    procedure ClearTracked;
    procedure NotifyClientsStart;
    procedure NotifyClientsStop;
  protected
    procedure Notification(AComponent: TComponent; Operation: TOperation); override;
    procedure Loaded; override;
  public
    constructor Create(AOwner: TComponent); override;
    destructor Destroy; override;

    procedure RegisterClient(AClient: TComponent);
    procedure UnregisterClient(AClient: TComponent);

    function Start: Boolean;
    function Stop: Boolean;
    procedure Update;

    function RegisterGroup(const AName: string): TZF2_GROUP;
    function GetGroup(const AName: string): TZF2_GROUP;

    function LoadSound(const APath: string;
      AType: TZFork2SoundType = zstPreDecoded): TZF2_SOUND;
    procedure UnloadSound(Sound: TZF2_SOUND);

    function PlaySound(Sound: TZF2_SOUND; const GroupName: string = 'sfx'): TZF2_VOICE;
    function PlaySoundOnGroup(Sound: TZF2_SOUND; Group: TZF2_GROUP): TZF2_VOICE;
    procedure StopVoice(Voice: TZF2_VOICE);
    procedure PauseVoice(Voice: TZF2_VOICE);
    procedure ResumeVoice(Voice: TZF2_VOICE);
    function IsVoiceActive(Voice: TZF2_VOICE): Boolean;
    procedure SetVoiceVolume(Voice: TZF2_VOICE; Volume: Single);
    procedure SetVoicePan(Voice: TZF2_VOICE; Pan: Single);
    procedure SetVoiceLoop(Voice: TZF2_VOICE; LoopCount: Integer);
    procedure SetVoicePitch(Voice: TZF2_VOICE; Semitones: Single);
    procedure SetVoicePriority(Voice: TZF2_VOICE; Priority: Integer);
    procedure SetVoicePosition2D(Voice: TZF2_VOICE; X, Y: Single);
    procedure SetVoicePosition3D(Voice: TZF2_VOICE; X, Y, Z: Single);

    function LoadBank(const APath: string): TZF2_BANK;
    procedure UnloadBank(Bank: TZF2_BANK);
    function PlayEvent(Bank: TZF2_BANK; const EventId: string): TZF2_VOICE;
    function PlayEventOnGroup(Bank: TZF2_BANK; const EventId: string;
      Group: TZF2_GROUP): TZF2_VOICE;

    procedure SetGroupVolume(const GroupName: string; Volume: Single);
    procedure SetGroupMute(const GroupName: string; Muted: Boolean);
    procedure SetListenerPosition2D(X, Y: Single);
    procedure SetListenerPosition3D(X, Y, Z: Single);
    procedure SetListenerRange(MinDist, MaxDist, Rolloff: Single);

    function GetOutputBackendInfo(out BackendName: string;
      out PeriodFrames: LongWord): Boolean;

    procedure ShowAbout;

    property Context: TZF2_CONTEXT read FContext;
    property LastError: string read FLastError;
  published
    property About: string read GetAbout write SetAbout stored False;
    { True = Context Start + Update-Timer; False = Stop/Destroy. }
    property Active: Boolean read FActive write SetActive default False;
    property OutputBackend: TZFork2OutputBackend read FOutputBackend
      write SetOutputBackend default zbAuto;
    { music / sfx / ui vor Start registrieren. }
    property AutoDefaultGroups: Boolean read FAutoDefaultGroups
      write FAutoDefaultGroups default True;
    { Zusaetzliche Gruppen-Namen (eine pro Zeile), vor Start. }
    property Groups: TStrings read FGroups write SetGroups;
    property AutoUpdate: Boolean read FAutoUpdate write SetAutoUpdate default True;
    property UpdateInterval: Integer read FUpdateInterval
      write SetUpdateInterval default 16;
    property OnVoiceEvent: TZFork2VoiceEvent read FOnVoiceEvent write FOnVoiceEvent;
    property OnVoiceStart: TZFork2VoiceEvent read FOnVoiceStart write FOnVoiceStart;
    property OnVoiceFinished: TZFork2VoiceEvent read FOnVoiceFinished write FOnVoiceFinished;
    property OnVoiceLoop: TZFork2VoiceEvent read FOnVoiceLoop write FOnVoiceLoop;
    property OnPositionCrossed: TZFork2VoiceEvent read FOnPositionCrossed write FOnPositionCrossed;
    property OnStarted: TNotifyEvent read FOnStarted write FOnStarted;
    property OnStopped: TNotifyEvent read FOnStopped write FOnStopped;
    property OnError: TZFork2ErrorEvent read FOnError write FOnError;
  end;

implementation

uses
  ZForkAbout;

constructor TZForkAudioEngine.Create(AOwner: TComponent);
begin
  inherited Create(AOwner);
  FActive := False;
  FAbout := '(About…)';
  FOutputBackend := zbAuto;
  FAutoUpdate := True;
  FUpdateInterval := 16;
  FAutoDefaultGroups := True;
  FGroups := TStringList.Create;
  FSounds := TList.Create;
  FBanks := TList.Create;
  FClients := TFPList.Create;
  FTimer := TTimer.Create(Self);
  FTimer.Enabled := False;
  FTimer.Interval := FUpdateInterval;
  FTimer.OnTimer := @TimerTick;
end;

destructor TZForkAudioEngine.Destroy;
begin
  FTimer.Enabled := False;
  if FActive and not (csDesigning in ComponentState) then
    Stop;
  FreeAndNil(FGroups);
  FreeAndNil(FSounds);
  FreeAndNil(FBanks);
  if FClients <> nil then
    FClients.Clear;
  FreeAndNil(FClients);
  inherited Destroy;
end;

function TZForkAudioEngine.GetAbout: string;
begin
  Result := '(About…)';
end;

procedure TZForkAudioEngine.SetAbout(const AValue: string);
begin
  if AValue = '' then
    Exit;
  ShowAbout;
end;

procedure TZForkAudioEngine.ShowAbout;
begin
  ShowZForkAbout;
end;

procedure TZForkAudioEngine.RegisterClient(AClient: TComponent);
begin
  if (AClient = nil) or (FClients = nil) then
    Exit;
  if FClients.IndexOf(AClient) < 0 then
  begin
    FClients.Add(AClient);
    AClient.FreeNotification(Self);
  end;
end;

procedure TZForkAudioEngine.UnregisterClient(AClient: TComponent);
begin
  if (AClient = nil) or (FClients = nil) then
    Exit;
  FClients.Remove(AClient);
end;

procedure TZForkAudioEngine.NotifyClientsStart;
var
  I: Integer;
  Intf: IZForkEngineClient;
begin
  if FClients = nil then
    Exit;
  for I := 0 to FClients.Count - 1 do
    if Supports(TObject(FClients[I]), IZForkEngineClient, Intf) then
      Intf.NotifyEngineStart;
end;

procedure TZForkAudioEngine.NotifyClientsStop;
var
  I: Integer;
  Intf: IZForkEngineClient;
begin
  if FClients = nil then
    Exit;
  for I := 0 to FClients.Count - 1 do
    if Supports(TObject(FClients[I]), IZForkEngineClient, Intf) then
      Intf.NotifyEngineStop;
end;

procedure TZForkAudioEngine.Notification(AComponent: TComponent; Operation: TOperation);
begin
  inherited Notification(AComponent, Operation);
  if (Operation = opRemove) and (FClients <> nil) then
    FClients.Remove(AComponent);
end;

procedure TZForkAudioEngine.Loaded;
begin
  inherited Loaded;
  if FActive and not (csDesigning in ComponentState) then
  begin
    FActive := False;
    SetActive(True);
  end;
end;

procedure TZForkAudioEngine.Fail(const Msg: string);
begin
  FLastError := Msg;
  if Assigned(FOnError) then
    FOnError(Self, Msg);
end;

function TZForkAudioEngine.BackendToInt: Integer;
begin
  case FOutputBackend of
    zbWasapi: Result := ZF2_OUTPUT_WASAPI;
    zbWaveOut: Result := ZF2_OUTPUT_WAVEOUT;
  else
    Result := ZF2_OUTPUT_AUTO;
  end;
end;

procedure TZForkAudioEngine.TrackSound(S: TZF2_SOUND);
begin
  if (S <> nil) and (FSounds.IndexOf(S) < 0) then
    FSounds.Add(S);
end;

procedure TZForkAudioEngine.TrackBank(B: TZF2_BANK);
begin
  if (B <> nil) and (FBanks.IndexOf(B) < 0) then
    FBanks.Add(B);
end;

procedure TZForkAudioEngine.ClearTracked;
var
  I: Integer;
begin
  for I := 0 to FBanks.Count - 1 do
    if FBanks[I] <> nil then
      zfork2_UnloadBankV1(TZF2_BANK(FBanks[I]));
  FBanks.Clear;
  for I := 0 to FSounds.Count - 1 do
    if FSounds[I] <> nil then
      zfork2_UnloadSound(TZF2_SOUND(FSounds[I]));
  FSounds.Clear;
end;

class procedure TZForkAudioEngine.EngineCallback(eventType: Integer;
  voice: TZF2_VOICE; param1: LongWord; param2: Single; userData: Pointer); cdecl;
begin
  if userData <> nil then
    TZForkAudioEngine(userData).HandleCallback(eventType, voice, param1, param2);
end;

procedure TZForkAudioEngine.HandleCallback(eventType: Integer; voice: TZF2_VOICE;
  param1: LongWord; param2: Single);
begin
  if Assigned(FOnVoiceEvent) then
    FOnVoiceEvent(Self, eventType, voice, param1, param2);
  case eventType of
    ZF2_EVENT_VOICE_START:
      if Assigned(FOnVoiceStart) then
        FOnVoiceStart(Self, eventType, voice, param1, param2);
    ZF2_EVENT_VOICE_FINISHED:
      if Assigned(FOnVoiceFinished) then
        FOnVoiceFinished(Self, eventType, voice, param1, param2);
    ZF2_EVENT_VOICE_LOOP:
      if Assigned(FOnVoiceLoop) then
        FOnVoiceLoop(Self, eventType, voice, param1, param2);
    ZF2_EVENT_POSITION_CROSSED:
      if Assigned(FOnPositionCrossed) then
        FOnPositionCrossed(Self, eventType, voice, param1, param2);
  end;
end;

procedure TZForkAudioEngine.ApplyCallback;
begin
  if FContext = nil then
    Exit;
  zfork2_SetCallback(FContext, @EngineCallback, Self);
end;

procedure TZForkAudioEngine.UpdateTimer;
begin
  FTimer.Interval := FUpdateInterval;
  FTimer.Enabled := FActive and FAutoUpdate and not (csDesigning in ComponentState);
end;

procedure TZForkAudioEngine.TimerTick(Sender: TObject);
begin
  Update;
end;

procedure TZForkAudioEngine.Update;
begin
  if FContext <> nil then
    zfork2_Update(FContext);
end;

procedure TZForkAudioEngine.SetOutputBackend(AValue: TZFork2OutputBackend);
begin
  if FOutputBackend = AValue then
    Exit;
  if FActive then
  begin
    Fail('OutputBackend nur aenderbar wenn Active=False');
    Exit;
  end;
  FOutputBackend := AValue;
end;

procedure TZForkAudioEngine.SetAutoUpdate(AValue: Boolean);
begin
  if FAutoUpdate = AValue then
    Exit;
  FAutoUpdate := AValue;
  UpdateTimer;
end;

procedure TZForkAudioEngine.SetUpdateInterval(AValue: Integer);
begin
  if AValue < 1 then
    AValue := 1;
  if FUpdateInterval = AValue then
    Exit;
  FUpdateInterval := AValue;
  UpdateTimer;
end;

procedure TZForkAudioEngine.SetGroups(AValue: TStrings);
begin
  FGroups.Assign(AValue);
end;

function TZForkAudioEngine.Start: Boolean;
var
  I: Integer;
  G: string;
begin
  Result := False;
  FLastError := '';
  if csDesigning in ComponentState then
  begin
    FActive := True;
    Result := True;
    Exit;
  end;
  if FActive and (FContext <> nil) then
  begin
    Result := True;
    Exit;
  end;

  FContext := zfork2_CreateContextWithBackend(BackendToInt);
  if FContext = nil then
  begin
    Fail('zfork2_CreateContextWithBackend fehlgeschlagen');
    Exit;
  end;

  if FAutoDefaultGroups then
  begin
    zfork2_RegisterGroup(FContext, 'music');
    zfork2_RegisterGroup(FContext, 'sfx');
    zfork2_RegisterGroup(FContext, 'ui');
  end;
  for I := 0 to FGroups.Count - 1 do
  begin
    G := Trim(FGroups[I]);
    if G <> '' then
      zfork2_RegisterGroup(FContext, PAnsiChar(AnsiString(G)));
  end;

  ApplyCallback;

  if zfork2_Start(FContext) = 0 then
  begin
    zfork2_DestroyContext(FContext);
    FContext := nil;
    Fail('zfork2_Start fehlgeschlagen (Backend/Geraet?)');
    Exit;
  end;

  FActive := True;
  UpdateTimer;
  NotifyClientsStart;
  if Assigned(FOnStarted) then
    FOnStarted(Self);
  Result := True;
end;

function TZForkAudioEngine.Stop: Boolean;
begin
  Result := True;
  FTimer.Enabled := False;
  if csDesigning in ComponentState then
  begin
    FActive := False;
    Exit;
  end;
  if FContext = nil then
  begin
    FActive := False;
    Exit;
  end;

  NotifyClientsStop;
  ClearTracked;
  zfork2_SetCallback(FContext, nil, nil);
  zfork2_Stop(FContext);
  zfork2_DestroyContext(FContext);
  FContext := nil;
  FActive := False;
  if Assigned(FOnStopped) then
    FOnStopped(Self);
end;

procedure TZForkAudioEngine.SetActive(AValue: Boolean);
begin
  if FActive = AValue then
    Exit;
  if AValue then
  begin
    if not Start then
      Exit;
  end
  else
    Stop;
end;

function TZForkAudioEngine.RegisterGroup(const AName: string): TZF2_GROUP;
begin
  Result := ZF2_INVALID_GROUP;
  if FContext = nil then
  begin
    Fail('RegisterGroup: Engine nicht gestartet');
    Exit;
  end;
  Result := zfork2_RegisterGroup(FContext, PAnsiChar(AnsiString(AName)));
end;

function TZForkAudioEngine.GetGroup(const AName: string): TZF2_GROUP;
begin
  Result := ZF2_INVALID_GROUP;
  if FContext = nil then
    Exit;
  Result := zfork2_GetGroup(FContext, PAnsiChar(AnsiString(AName)));
end;

function TZForkAudioEngine.LoadSound(const APath: string;
  AType: TZFork2SoundType): TZF2_SOUND;
var
  St: Integer;
  W: UnicodeString;
begin
  Result := nil;
  if FContext = nil then
  begin
    Fail('LoadSound: Engine nicht gestartet (Active=True)');
    Exit;
  end;
  if AType = zstStreaming then
    St := ZF2_SOUND_STREAMING
  else
    St := ZF2_SOUND_PREDECODED;
  W := UnicodeString(APath);
  Result := zfork2_LoadSound(FContext, PWideChar(W), St);
  if Result = nil then
  begin
    Fail('LoadSound fehlgeschlagen: ' + APath);
    Exit;
  end;
  TrackSound(Result);
end;

procedure TZForkAudioEngine.UnloadSound(Sound: TZF2_SOUND);
var
  I: Integer;
begin
  if Sound = nil then
    Exit;
  I := FSounds.IndexOf(Sound);
  if I >= 0 then
    FSounds.Delete(I);
  zfork2_UnloadSound(Sound);
end;

function TZForkAudioEngine.PlaySoundOnGroup(Sound: TZF2_SOUND;
  Group: TZF2_GROUP): TZF2_VOICE;
begin
  Result := ZF2_INVALID_VOICE;
  if (FContext = nil) or (Sound = nil) then
    Exit;
  Result := zfork2_PlaySound(FContext, Sound, Group);
end;

function TZForkAudioEngine.PlaySound(Sound: TZF2_SOUND;
  const GroupName: string): TZF2_VOICE;
var
  G: TZF2_GROUP;
begin
  Result := ZF2_INVALID_VOICE;
  if FContext = nil then
    Exit;
  G := GetGroup(GroupName);
  if G = ZF2_INVALID_GROUP then
    G := GetGroup('sfx');
  Result := PlaySoundOnGroup(Sound, G);
end;

procedure TZForkAudioEngine.StopVoice(Voice: TZF2_VOICE);
begin
  if FContext <> nil then
    zfork2_StopVoice(FContext, Voice);
end;

procedure TZForkAudioEngine.PauseVoice(Voice: TZF2_VOICE);
begin
  if FContext <> nil then
    zfork2_PauseVoice(FContext, Voice);
end;

procedure TZForkAudioEngine.ResumeVoice(Voice: TZF2_VOICE);
begin
  if FContext <> nil then
    zfork2_ResumeVoice(FContext, Voice);
end;

function TZForkAudioEngine.IsVoiceActive(Voice: TZF2_VOICE): Boolean;
begin
  Result := (FContext <> nil) and (zfork2_IsVoiceActive(FContext, Voice) <> 0);
end;

procedure TZForkAudioEngine.SetVoiceVolume(Voice: TZF2_VOICE; Volume: Single);
begin
  if FContext <> nil then
    zfork2_SetVoiceVolume(FContext, Voice, Volume);
end;

procedure TZForkAudioEngine.SetVoicePan(Voice: TZF2_VOICE; Pan: Single);
begin
  if FContext <> nil then
    zfork2_SetVoicePan(FContext, Voice, Pan);
end;

procedure TZForkAudioEngine.SetVoiceLoop(Voice: TZF2_VOICE; LoopCount: Integer);
begin
  if FContext <> nil then
    zfork2_SetVoiceLoop(FContext, Voice, LoopCount);
end;

procedure TZForkAudioEngine.SetVoicePitch(Voice: TZF2_VOICE; Semitones: Single);
begin
  if FContext <> nil then
    zfork2_SetVoicePitch(FContext, Voice, Semitones);
end;

procedure TZForkAudioEngine.SetVoicePriority(Voice: TZF2_VOICE; Priority: Integer);
begin
  if FContext <> nil then
    zfork2_SetVoicePriority(FContext, Voice, Priority);
end;

procedure TZForkAudioEngine.SetVoicePosition2D(Voice: TZF2_VOICE; X, Y: Single);
begin
  if FContext <> nil then
    zfork2_SetVoicePosition2D(FContext, Voice, X, Y);
end;

procedure TZForkAudioEngine.SetVoicePosition3D(Voice: TZF2_VOICE; X, Y, Z: Single);
begin
  if FContext <> nil then
    zfork2_SetVoicePosition3D(FContext, Voice, X, Y, Z);
end;

function TZForkAudioEngine.LoadBank(const APath: string): TZF2_BANK;
var
  W: UnicodeString;
begin
  Result := nil;
  if FContext = nil then
  begin
    Fail('LoadBank: Engine nicht gestartet');
    Exit;
  end;
  W := UnicodeString(APath);
  Result := zfork2_LoadBankV1(FContext, PWideChar(W));
  if Result = nil then
  begin
    Fail('LoadBank fehlgeschlagen: ' + APath);
    Exit;
  end;
  TrackBank(Result);
end;

procedure TZForkAudioEngine.UnloadBank(Bank: TZF2_BANK);
var
  I: Integer;
begin
  if Bank = nil then
    Exit;
  I := FBanks.IndexOf(Bank);
  if I >= 0 then
    FBanks.Delete(I);
  zfork2_UnloadBankV1(Bank);
end;

function TZForkAudioEngine.PlayEvent(Bank: TZF2_BANK;
  const EventId: string): TZF2_VOICE;
begin
  Result := ZF2_INVALID_VOICE;
  if (FContext = nil) or (Bank = nil) then
    Exit;
  Result := zfork2_PlayEventV1(FContext, Bank, PAnsiChar(AnsiString(EventId)));
end;

function TZForkAudioEngine.PlayEventOnGroup(Bank: TZF2_BANK;
  const EventId: string; Group: TZF2_GROUP): TZF2_VOICE;
begin
  Result := ZF2_INVALID_VOICE;
  if (FContext = nil) or (Bank = nil) then
    Exit;
  Result := zfork2_PlayEventOnGroupV1(FContext, Bank, PAnsiChar(AnsiString(EventId)), Group);
end;

procedure TZForkAudioEngine.SetGroupVolume(const GroupName: string; Volume: Single);
var
  G: TZF2_GROUP;
begin
  G := GetGroup(GroupName);
  if (FContext <> nil) and (G <> ZF2_INVALID_GROUP) then
    zfork2_SetGroupVolume(FContext, G, Volume);
end;

procedure TZForkAudioEngine.SetGroupMute(const GroupName: string; Muted: Boolean);
var
  G: TZF2_GROUP;
begin
  G := GetGroup(GroupName);
  if (FContext <> nil) and (G <> ZF2_INVALID_GROUP) then
    zfork2_SetGroupMute(FContext, G, Ord(Muted));
end;

procedure TZForkAudioEngine.SetListenerPosition2D(X, Y: Single);
begin
  if FContext <> nil then
    zfork2_SetListenerPosition2D(FContext, X, Y);
end;

procedure TZForkAudioEngine.SetListenerPosition3D(X, Y, Z: Single);
begin
  if FContext <> nil then
    zfork2_SetListenerPosition3D(FContext, X, Y, Z);
end;

procedure TZForkAudioEngine.SetListenerRange(MinDist, MaxDist, Rolloff: Single);
begin
  if FContext <> nil then
    zfork2_SetListenerRange(FContext, MinDist, MaxDist, Rolloff);
end;

function TZForkAudioEngine.GetOutputBackendInfo(out BackendName: string;
  out PeriodFrames: LongWord): Boolean;
var
  Buf: array[0..127] of AnsiChar;
begin
  Result := False;
  BackendName := '';
  PeriodFrames := 0;
  if FContext = nil then
    Exit;
  FillChar(Buf, SizeOf(Buf), 0);
  if zfork2_GetOutputBackendInfo(FContext, @Buf[0], SizeOf(Buf), @PeriodFrames) = 0 then
    Exit;
  BackendName := string(AnsiString(PAnsiChar(@Buf[0])));
  Result := True;
end;

end.
