{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  TZForkSoundBank — SoundBank V1 an TZForkAudioEngine

  Was die Komponente kann:
  - Bank-Datei (.txt / kompiliertes .bank) ueber FileName laden
  - AutoLoad wenn Engine Active wird; Load / Unload / Reload
  - PlayEvent / PlayEventOnGroup; EntryCount und SkippedCount
  - Design-Time Dateiauswahl (BrowseFileName)
  - Reagiert als IZForkEngineClient auf Engine-Start/Stop
******************************************************************************}
unit ZForkBank;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, LibZFork2, ZForkIntf, ZForkEngine;

type
  TZForkSoundBank = class(TComponent, IZForkEngineClient)
  private
    FEngine: TZForkAudioEngine;
    FFileName: string;
    FAutoLoad: Boolean;
    FBank: TZF2_BANK;
    FLastError: string;
    FAbout: string;
    FOnLoaded: TNotifyEvent;
    FOnUnloaded: TNotifyEvent;
    FOnError: TZFork2ErrorEvent;
    procedure SetEngine(AValue: TZForkAudioEngine);
    procedure SetFileName(const AValue: string);
    procedure SetAutoLoad(AValue: Boolean);
    function GetAbout: string;
    procedure SetAbout(const AValue: string);
    function GetIsLoaded: Boolean;
    function GetEntryCount: LongWord;
    function GetSkippedCount: LongWord;
    procedure Fail(const Msg: string);
  protected
    procedure Loaded; override;
    procedure Notification(AComponent: TComponent; Operation: TOperation); override;
  public
    constructor Create(AOwner: TComponent); override;
    destructor Destroy; override;

    { IZForkEngineClient }
    procedure NotifyEngineStart;
    procedure NotifyEngineStop;

    function Load: Boolean;
    procedure Unload;
    function PlayEvent(const EventId: string): TZF2_VOICE;
    function PlayEventOnGroup(const EventId, GroupName: string): TZF2_VOICE;
    function BrowseFileName: Boolean;
    procedure ShowAbout;

    property Bank: TZF2_BANK read FBank;
    property IsLoaded: Boolean read GetIsLoaded;
    property EntryCount: LongWord read GetEntryCount;
    property SkippedCount: LongWord read GetSkippedCount;
    property LastError: string read FLastError;
  published
    property About: string read GetAbout write SetAbout stored False;
    { Object Inspector: auf TZForkAudioEngine zeigen. }
    property Engine: TZForkAudioEngine read FEngine write SetEngine;
    property FileName: string read FFileName write SetFileName;
    { Bei Engine-Start automatisch laden. }
    property AutoLoad: Boolean read FAutoLoad write SetAutoLoad default True;
    property OnLoaded: TNotifyEvent read FOnLoaded write FOnLoaded;
    property OnUnloaded: TNotifyEvent read FOnUnloaded write FOnUnloaded;
    property OnError: TZFork2ErrorEvent read FOnError write FOnError;
  end;

implementation

uses
  Dialogs, ZForkAbout;

constructor TZForkSoundBank.Create(AOwner: TComponent);
begin
  inherited Create(AOwner);
  FAutoLoad := True;
  FAbout := '(About…)';
  FBank := nil;
end;

destructor TZForkSoundBank.Destroy;
begin
  if FEngine <> nil then
  begin
    FEngine.UnregisterClient(Self);
    FEngine := nil;
  end;
  Unload;
  inherited Destroy;
end;

function TZForkSoundBank.GetAbout: string;
begin
  Result := '(About…)';
end;

procedure TZForkSoundBank.SetAbout(const AValue: string);
begin
  if AValue = '' then
    Exit;
  ShowAbout;
end;

procedure TZForkSoundBank.ShowAbout;
begin
  ShowZForkAbout;
end;

function TZForkSoundBank.GetIsLoaded: Boolean;
begin
  Result := FBank <> nil;
end;

function TZForkSoundBank.GetEntryCount: LongWord;
begin
  if FBank <> nil then
    Result := zfork2_GetBankEntryCount(FBank)
  else
    Result := 0;
end;

function TZForkSoundBank.GetSkippedCount: LongWord;
begin
  if FBank <> nil then
    Result := zfork2_GetBankSkippedCount(FBank)
  else
    Result := 0;
end;

procedure TZForkSoundBank.Fail(const Msg: string);
begin
  FLastError := Msg;
  if Assigned(FOnError) then
    FOnError(Self, Msg);
end;

procedure TZForkSoundBank.Loaded;
begin
  inherited Loaded;
  if FAutoLoad and (FEngine <> nil) and FEngine.Active and
     not (csDesigning in ComponentState) and (FFileName <> '') then
    Load;
end;

procedure TZForkSoundBank.Notification(AComponent: TComponent; Operation: TOperation);
begin
  inherited Notification(AComponent, Operation);
  if (Operation = opRemove) and (AComponent = FEngine) then
  begin
    Unload;
    FEngine := nil;
  end;
end;

procedure TZForkSoundBank.SetEngine(AValue: TZForkAudioEngine);
begin
  if FEngine = AValue then
    Exit;
  if FEngine <> nil then
  begin
    FEngine.UnregisterClient(Self);
    Unload;
  end;
  FEngine := AValue;
  if FEngine <> nil then
  begin
    FEngine.FreeNotification(Self);
    FEngine.RegisterClient(Self);
    if FAutoLoad and FEngine.Active and (FFileName <> '') and
       not (csDesigning in ComponentState) then
      Load;
  end;
end;

procedure TZForkSoundBank.SetFileName(const AValue: string);
begin
  if FFileName = AValue then
    Exit;
  FFileName := AValue;
  if FAutoLoad and (FEngine <> nil) and FEngine.Active and
     (FFileName <> '') and not (csDesigning in ComponentState) then
    Load;
end;

procedure TZForkSoundBank.SetAutoLoad(AValue: Boolean);
begin
  FAutoLoad := AValue;
end;

procedure TZForkSoundBank.NotifyEngineStart;
begin
  if FAutoLoad and (FFileName <> '') then
    Load;
end;

procedure TZForkSoundBank.NotifyEngineStop;
begin
  Unload;
end;

function TZForkSoundBank.Load: Boolean;
begin
  Result := False;
  FLastError := '';
  if csDesigning in ComponentState then
  begin
    Result := True;
    Exit;
  end;
  if FEngine = nil then
  begin
    Fail('SoundBank: Engine nicht gesetzt');
    Exit;
  end;
  if not FEngine.Active then
  begin
    Fail('SoundBank: Engine nicht Active');
    Exit;
  end;
  if FFileName = '' then
  begin
    Fail('SoundBank: FileName leer');
    Exit;
  end;
  Unload;
  FBank := FEngine.LoadBank(FFileName);
  if FBank = nil then
  begin
    Fail(FEngine.LastError);
    if FLastError = '' then
      Fail('LoadBank fehlgeschlagen: ' + FFileName);
    Exit;
  end;
  if Assigned(FOnLoaded) then
    FOnLoaded(Self);
  Result := True;
end;

procedure TZForkSoundBank.Unload;
begin
  if FBank = nil then
    Exit;
  if (FEngine <> nil) and FEngine.Active then
    FEngine.UnloadBank(FBank)
  else
    zfork2_UnloadBankV1(FBank);
  FBank := nil;
  if Assigned(FOnUnloaded) then
    FOnUnloaded(Self);
end;

function TZForkSoundBank.PlayEvent(const EventId: string): TZF2_VOICE;
begin
  Result := ZF2_INVALID_VOICE;
  if (FEngine = nil) or (FBank = nil) then
  begin
    Fail('PlayEvent: Bank nicht geladen');
    Exit;
  end;
  Result := FEngine.PlayEvent(FBank, EventId);
end;

function TZForkSoundBank.PlayEventOnGroup(const EventId, GroupName: string): TZF2_VOICE;
var
  G: TZF2_GROUP;
begin
  Result := ZF2_INVALID_VOICE;
  if (FEngine = nil) or (FBank = nil) then
  begin
    Fail('PlayEventOnGroup: Bank nicht geladen');
    Exit;
  end;
  G := FEngine.GetGroup(GroupName);
  Result := FEngine.PlayEventOnGroup(FBank, EventId, G);
end;

function TZForkSoundBank.BrowseFileName: Boolean;
var
  Dlg: TOpenDialog;
begin
  Result := False;
  Dlg := TOpenDialog.Create(nil);
  try
    Dlg.Title := 'Soundbank V1';
    Dlg.Filter := 'Soundbank|*.bank;*.txt|All|*.*';
    Dlg.Options := Dlg.Options + [ofFileMustExist];
    if FFileName <> '' then
      Dlg.FileName := FFileName;
    if Dlg.Execute then
    begin
      FileName := Dlg.FileName;
      Result := True;
    end;
  finally
    Dlg.Free;
  end;
end;

end.
