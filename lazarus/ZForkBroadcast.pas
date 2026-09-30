{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  TZForkBroadcast — Icecast/Shoutcast-Streaming am Player

  Was die Komponente kann:
  - Verbindung: Host, Port, Mount, User/Password, StreamName
  - Encoder-Parameter: Bitrate, SampleRate, Channels, StereoMode, ServerType
  - AutoReconnect + Delay; AttachToPlayback (mit laufender Wiedergabe)
  - Metadata setzen (Title/Artist/Album); Stats abfragen
  - Mic-Capture starten/stoppen, MicActive, Ducking-Config
  - Events: Connected / Disconnected / Reconnecting / Error / Stats
******************************************************************************}
unit ZForkBroadcast;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, ExtCtrls, ZForkPlayer, ZForkIntf, LibZFork;

type
  TZForkBroadcastServer = (
    bsIcecast,
    bsShoutcast
  );

  TZForkBroadcastStereo = (
    bssAuto,
    bssStereo,
    bssJointStereo,
    bssMono
  );

  TZForkBroadcastStatsEvent = procedure(Sender: TObject; const Stats: TBroadcastStats) of object;

  { TZForkBroadcast }
  TZForkBroadcast = class(TComponent, IZForkPlayerClient)
  private
    FPlayer: TZForkPlayer;
    FActive: Boolean;
    FHost: string;
    FPort: Word;
    FMount: string;
    FUser: string;
    FPassword: string;
    FStreamName: string;
    FBitrateKbps: Integer;
    FSampleRate: Integer;
    FChannels: Integer;
    FStereoMode: TZForkBroadcastStereo;
    FServerType: TZForkBroadcastServer;
    FAutoReconnect: Boolean;
    FReconnectDelayMs: LongWord;
    FAttachToPlayback: Boolean;
    FMetaTitle: string;
    FMetaArtist: string;
    FMetaAlbum: string;
    FLastError: string;
    FAbout: string;
    FPollStats: Boolean;
    FStatsInterval: Integer;
    FTimer: TTimer;
    FOnConnected: TNotifyEvent;
    FOnDisconnected: TNotifyEvent;
    FOnError: TZForkErrorEvent;
    FOnStats: TZForkBroadcastStatsEvent;
    procedure SetPlayer(AValue: TZForkPlayer);
    procedure SetActive(AValue: Boolean);
    procedure SetHost(const AValue: string);
    procedure SetPort(AValue: Word);
    procedure SetMount(const AValue: string);
    procedure SetUser(const AValue: string);
    procedure SetPassword(const AValue: string);
    procedure SetStreamName(const AValue: string);
    procedure SetBitrateKbps(AValue: Integer);
    procedure SetSampleRate(AValue: Integer);
    procedure SetChannels(AValue: Integer);
    procedure SetStereoMode(AValue: TZForkBroadcastStereo);
    procedure SetServerType(AValue: TZForkBroadcastServer);
    procedure SetAutoReconnect(AValue: Boolean);
    procedure SetReconnectDelayMs(AValue: LongWord);
    procedure SetAttachToPlayback(AValue: Boolean);
    procedure SetPollStats(AValue: Boolean);
    procedure SetStatsInterval(AValue: Integer);
    function GetAbout: string;
    procedure SetAbout(const AValue: string);
    procedure Fail(const Msg: string);
    function EnsurePlayerHandle: Boolean;
    procedure BuildConfig(out Cfg: TBroadcastConfig);
    procedure ApplyMetadata;
    procedure TimerTick(Sender: TObject);
    procedure UpdateTimer;
  protected
    procedure Notification(AComponent: TComponent; Operation: TOperation); override;
    procedure Loaded; override;
  public
    constructor Create(AOwner: TComponent); override;
    destructor Destroy; override;

    { IZForkPlayerClient }
    procedure NotifyPlayerOpen(const AFileName: string);
    procedure NotifyPlayerSong(SongIndex: Integer);
    procedure NotifyPlayerClose;
    procedure NotifyPlayerStop;

    function Start: Boolean;
    function Stop: Boolean;
    function SetMetadata(const Title, Artist, Album: string): Boolean;
    function GetStats(out Stats: TBroadcastStats): Boolean;
    function StartMicCapture(DeviceId: Integer = -1; SampleRate: Integer = 44100;
      Channels: Integer = 1): Boolean;
    function StopMicCapture: Boolean;
    function SetMicActive(Active: Boolean): Boolean;
    procedure ShowAbout;

    property LastError: string read FLastError;
  published
    { Object Inspector: Schaltflaeche „…“ oeffnet About/Lizenzen. }
    property About: string read GetAbout write SetAbout stored False;
    property Player: TZForkPlayer read FPlayer write SetPlayer;
    { True = StartBroadcast; False = Stop. }
    property Active: Boolean read FActive write SetActive default False;
    property Host: string read FHost write SetHost;
    property Port: Word read FPort write SetPort default 8000;
    property Mount: string read FMount write SetMount;
    property User: string read FUser write SetUser;
    property Password: string read FPassword write SetPassword;
    property StreamName: string read FStreamName write SetStreamName;
    property BitrateKbps: Integer read FBitrateKbps write SetBitrateKbps default 192;
    property SampleRate: Integer read FSampleRate write SetSampleRate default 44100;
    property Channels: Integer read FChannels write SetChannels default 2;
    property StereoMode: TZForkBroadcastStereo read FStereoMode write SetStereoMode default bssJointStereo;
    property ServerType: TZForkBroadcastServer read FServerType write SetServerType default bsIcecast;
    property AutoReconnect: Boolean read FAutoReconnect write SetAutoReconnect default True;
    property ReconnectDelayMs: LongWord read FReconnectDelayMs write SetReconnectDelayMs default 3000;
    property AttachToPlayback: Boolean read FAttachToPlayback write SetAttachToPlayback default True;
    property MetaTitle: string read FMetaTitle write FMetaTitle;
    property MetaArtist: string read FMetaArtist write FMetaArtist;
    property MetaAlbum: string read FMetaAlbum write FMetaAlbum;
    property PollStats: Boolean read FPollStats write SetPollStats default False;
    property StatsInterval: Integer read FStatsInterval write SetStatsInterval default 1000;
    property OnConnected: TNotifyEvent read FOnConnected write FOnConnected;
    property OnDisconnected: TNotifyEvent read FOnDisconnected write FOnDisconnected;
    property OnError: TZForkErrorEvent read FOnError write FOnError;
    property OnStats: TZForkBroadcastStatsEvent read FOnStats write FOnStats;
  end;

implementation

uses
  ZForkAbout;

procedure CopyToAnsiBuf(const S: string; var Buf: array of AnsiChar);
var
  A: AnsiString;
  N: Integer;
begin
  FillChar(Buf, SizeOf(Buf), 0);
  A := AnsiString(S);
  N := Length(A);
  if N > High(Buf) then
    N := High(Buf);
  if N > 0 then
    Move(A[1], Buf[0], N);
end;

constructor TZForkBroadcast.Create(AOwner: TComponent);
begin
  inherited Create(AOwner);
  FActive := False;
  FAbout := '(About…)';
  FHost := '127.0.0.1';
  FPort := 8000;
  FMount := '/live.mp3';
  FUser := 'source';
  FPassword := 'hackme';
  FStreamName := 'libZFork';
  FBitrateKbps := 192;
  FSampleRate := 44100;
  FChannels := 2;
  FStereoMode := bssJointStereo;
  FServerType := bsIcecast;
  FAutoReconnect := True;
  FReconnectDelayMs := 3000;
  FAttachToPlayback := True;
  FPollStats := False;
  FStatsInterval := 1000;
  FTimer := TTimer.Create(Self);
  FTimer.Enabled := False;
  FTimer.Interval := FStatsInterval;
  FTimer.OnTimer := @TimerTick;
end;

destructor TZForkBroadcast.Destroy;
begin
  FTimer.Enabled := False;
  if FPlayer <> nil then
  begin
    FPlayer.UnregisterClient(Self);
    FPlayer := nil;
  end;
  if FActive and not (csDesigning in ComponentState) then
    Stop;
  inherited Destroy;
end;

function TZForkBroadcast.GetAbout: string;
begin
  Result := '(About…)';
end;

procedure TZForkBroadcast.SetAbout(const AValue: string);
begin
  FAbout := AValue;
  if not (csLoading in ComponentState) then
    ShowAbout;
end;

procedure TZForkBroadcast.ShowAbout;
begin
  ShowZForkAbout;
end;

procedure TZForkBroadcast.Loaded;
begin
  inherited Loaded;
  if csDesigning in ComponentState then Exit;
  if FPlayer <> nil then
    FPlayer.RegisterClient(Self);
  if FActive then
    Start;
end;

procedure TZForkBroadcast.Notification(AComponent: TComponent; Operation: TOperation);
begin
  inherited Notification(AComponent, Operation);
  if (Operation = opRemove) and (AComponent = FPlayer) then
  begin
    FPlayer := nil;
    FActive := False;
  end;
end;

procedure TZForkBroadcast.Fail(const Msg: string);
begin
  FLastError := Msg;
  if Assigned(FOnError) then
    FOnError(Self, Msg);
end;

function TZForkBroadcast.EnsurePlayerHandle: Boolean;
begin
  Result := False;
  if FPlayer = nil then
  begin
    Fail('Player is nil');
    Exit;
  end;
  if FPlayer.Handle = nil then
  begin
    FPlayer.Active := True;
    if FPlayer.Handle = nil then
    begin
      Fail('Player handle unavailable');
      Exit;
    end;
  end;
  Result := True;
end;

procedure TZForkBroadcast.SetPlayer(AValue: TZForkPlayer);
begin
  if FPlayer = AValue then Exit;
  if FPlayer <> nil then
  begin
    FPlayer.UnregisterClient(Self);
    FPlayer.RemoveFreeNotification(Self);
  end;
  FPlayer := AValue;
  if FPlayer <> nil then
  begin
    FPlayer.FreeNotification(Self);
    if not (csLoading in ComponentState) then
      FPlayer.RegisterClient(Self);
  end;
end;

procedure TZForkBroadcast.NotifyPlayerOpen(const AFileName: string);
begin
  if FActive then
    ApplyMetadata;
end;

procedure TZForkBroadcast.NotifyPlayerSong(SongIndex: Integer);
begin
  if FActive then
    ApplyMetadata;
end;

procedure TZForkBroadcast.NotifyPlayerClose;
begin
end;

procedure TZForkBroadcast.NotifyPlayerStop;
begin
end;

procedure TZForkBroadcast.BuildConfig(out Cfg: TBroadcastConfig);
begin
  ZForkInitBroadcastConfig(Cfg);
  CopyToAnsiBuf(FHost, Cfg.host);
  Cfg.port := FPort;
  CopyToAnsiBuf(FMount, Cfg.mount);
  CopyToAnsiBuf(FUser, Cfg.user);
  CopyToAnsiBuf(FPassword, Cfg.password);
  CopyToAnsiBuf(FStreamName, Cfg.streamName);
  Cfg.bitrateKbps := FBitrateKbps;
  Cfg.sampleRate := FSampleRate;
  Cfg.channels := FChannels;
  Cfg.stereoMode := Ord(FStereoMode);
  Cfg.serverType := Ord(FServerType);
  Cfg.autoReconnect := FAutoReconnect;
  Cfg.reconnectDelayMs := FReconnectDelayMs;
end;

procedure TZForkBroadcast.ApplyMetadata;
begin
  if (FPlayer = nil) or (FPlayer.Handle = nil) then Exit;
  if (FMetaTitle = '') and (FMetaArtist = '') and (FMetaAlbum = '') then Exit;
  FPlayer.SetBroadcastMetadata(FMetaTitle, FMetaArtist, FMetaAlbum);
end;

function TZForkBroadcast.Start: Boolean;
var
  Cfg: TBroadcastConfig;
begin
  Result := False;
  FLastError := '';
  if csDesigning in ComponentState then
  begin
    FActive := True;
    Exit(True);
  end;
  if not EnsurePlayerHandle then Exit;
  BuildConfig(Cfg);
  if not FPlayer.StartBroadcast(Cfg) then
  begin
    Fail(FPlayer.LastError);
    if FLastError = '' then
      Fail(FPlayer.GetBroadcastError);
    FActive := False;
    Exit;
  end;
  if FAttachToPlayback then
    FPlayer.AttachBroadcastToCurrentPlayback(True);
  ApplyMetadata;
  FActive := True;
  UpdateTimer;
  Result := True;
  if Assigned(FOnConnected) then
    FOnConnected(Self);
end;

function TZForkBroadcast.Stop: Boolean;
begin
  Result := True;
  if csDesigning in ComponentState then
  begin
    FActive := False;
    Exit;
  end;
  if (FPlayer <> nil) and (FPlayer.Handle <> nil) then
  begin
    FPlayer.AttachBroadcastToCurrentPlayback(False);
    Result := FPlayer.StopBroadcast;
    if not Result then
      Fail(FPlayer.LastError);
  end;
  FActive := False;
  FTimer.Enabled := False;
  if Assigned(FOnDisconnected) then
    FOnDisconnected(Self);
end;

procedure TZForkBroadcast.SetActive(AValue: Boolean);
begin
  if FActive = AValue then Exit;
  if csLoading in ComponentState then
  begin
    FActive := AValue;
    Exit;
  end;
  if AValue then
    Start
  else
    Stop;
end;

function TZForkBroadcast.SetMetadata(const Title, Artist, Album: string): Boolean;
begin
  FMetaTitle := Title;
  FMetaArtist := Artist;
  FMetaAlbum := Album;
  Result := False;
  if not EnsurePlayerHandle then Exit;
  Result := FPlayer.SetBroadcastMetadata(Title, Artist, Album);
  if not Result then
    Fail(FPlayer.LastError);
end;

function TZForkBroadcast.GetStats(out Stats: TBroadcastStats): Boolean;
begin
  FillChar(Stats, SizeOf(Stats), 0);
  Result := False;
  if (FPlayer = nil) or (FPlayer.Handle = nil) then Exit;
  Result := FPlayer.GetBroadcastStats(Stats);
end;

function TZForkBroadcast.StartMicCapture(DeviceId, SampleRate, Channels: Integer): Boolean;
begin
  Result := False;
  if not EnsurePlayerHandle then Exit;
  Result := FPlayer.StartBroadcastMicCapture(DeviceId, SampleRate, Channels);
  if not Result then
    Fail(FPlayer.LastError);
end;

function TZForkBroadcast.StopMicCapture: Boolean;
begin
  Result := False;
  if (FPlayer = nil) or (FPlayer.Handle = nil) then Exit;
  Result := FPlayer.StopBroadcastMicCapture;
end;

function TZForkBroadcast.SetMicActive(Active: Boolean): Boolean;
begin
  Result := False;
  if not EnsurePlayerHandle then Exit;
  Result := FPlayer.SetBroadcastMicActive(Active);
end;

procedure TZForkBroadcast.SetHost(const AValue: string);
begin
  FHost := AValue;
end;

procedure TZForkBroadcast.SetPort(AValue: Word);
begin
  if AValue = 0 then AValue := 8000;
  FPort := AValue;
end;

procedure TZForkBroadcast.SetMount(const AValue: string);
begin
  FMount := AValue;
end;

procedure TZForkBroadcast.SetUser(const AValue: string);
begin
  FUser := AValue;
end;

procedure TZForkBroadcast.SetPassword(const AValue: string);
begin
  FPassword := AValue;
end;

procedure TZForkBroadcast.SetStreamName(const AValue: string);
begin
  FStreamName := AValue;
end;

procedure TZForkBroadcast.SetBitrateKbps(AValue: Integer);
begin
  if AValue < 32 then AValue := 32;
  if AValue > 320 then AValue := 320;
  FBitrateKbps := AValue;
end;

procedure TZForkBroadcast.SetSampleRate(AValue: Integer);
begin
  if AValue < 8000 then AValue := 8000;
  FSampleRate := AValue;
end;

procedure TZForkBroadcast.SetChannels(AValue: Integer);
begin
  if AValue < 1 then AValue := 1;
  if AValue > 2 then AValue := 2;
  FChannels := AValue;
end;

procedure TZForkBroadcast.SetStereoMode(AValue: TZForkBroadcastStereo);
begin
  FStereoMode := AValue;
end;

procedure TZForkBroadcast.SetServerType(AValue: TZForkBroadcastServer);
begin
  FServerType := AValue;
end;

procedure TZForkBroadcast.SetAutoReconnect(AValue: Boolean);
begin
  FAutoReconnect := AValue;
end;

procedure TZForkBroadcast.SetReconnectDelayMs(AValue: LongWord);
begin
  if AValue < 500 then AValue := 500;
  FReconnectDelayMs := AValue;
end;

procedure TZForkBroadcast.SetAttachToPlayback(AValue: Boolean);
begin
  FAttachToPlayback := AValue;
  if FActive and (FPlayer <> nil) and (FPlayer.Handle <> nil)
     and not (csDesigning in ComponentState) then
    FPlayer.AttachBroadcastToCurrentPlayback(AValue);
end;

procedure TZForkBroadcast.SetPollStats(AValue: Boolean);
begin
  if FPollStats = AValue then Exit;
  FPollStats := AValue;
  UpdateTimer;
end;

procedure TZForkBroadcast.SetStatsInterval(AValue: Integer);
begin
  if AValue < 200 then AValue := 200;
  if AValue > 10000 then AValue := 10000;
  FStatsInterval := AValue;
  FTimer.Interval := FStatsInterval;
end;

procedure TZForkBroadcast.UpdateTimer;
begin
  FTimer.Enabled := FPollStats and FActive and Assigned(FOnStats)
    and not (csDesigning in ComponentState)
    and not (csLoading in ComponentState);
end;

procedure TZForkBroadcast.TimerTick(Sender: TObject);
var
  Stats: TBroadcastStats;
begin
  if not Assigned(FOnStats) then Exit;
  if GetStats(Stats) then
    FOnStats(Self, Stats);
end;

end.
