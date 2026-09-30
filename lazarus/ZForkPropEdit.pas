{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  ZForkPropEdit — Design-Time Property-Editoren (Object Inspector)

  Was dieses Unit kann:
  - About-Property oeffnet den About-Dialog
  - FileName: Dateiauswahl-Dialog
  - OutputDevice: Dropdown der Ausgabegeraete je Backend
  - Wird von ZForkReg beim Package-Install registriert
******************************************************************************}
unit ZForkPropEdit;

{$mode objfpc}{$H+}

interface

procedure Register;

implementation

uses
  Classes, SysUtils, PropEdits, Dialogs,
  ZForkPlayer, ZForkID3, ZForkSpectrum, ZForkBroadcast, ZForkEngine, ZForkBank,
  ZForkWaveform, ZForkCrossfader, ZForkChannelStrip, ZForkAbout, LibZFork;

type
  TZForkAboutPropertyEditor = class(TStringPropertyEditor)
  public
    function GetAttributes: TPropertyAttributes; override;
    function GetValue: ansistring; override;
    procedure SetValue(const NewValue: ansistring); override;
    procedure Edit; override;
  end;

  TZForkFileNamePropertyEditor = class(TStringPropertyEditor)
  public
    function GetAttributes: TPropertyAttributes; override;
    procedure Edit; override;
  end;

  TZForkOutputDevicePropertyEditor = class(TIntegerPropertyEditor)
  public
    function GetAttributes: TPropertyAttributes; override;
    function GetValue: ansistring; override;
    procedure GetValues(Proc: TGetStrProc); override;
    procedure SetValue(const NewValue: ansistring); override;
  end;

function TZForkAboutPropertyEditor.GetAttributes: TPropertyAttributes;
begin
  Result := [paDialog, paReadOnly, paMultiSelect];
end;

function TZForkAboutPropertyEditor.GetValue: ansistring;
begin
  Result := '(About…)';
end;

procedure TZForkAboutPropertyEditor.SetValue(const NewValue: ansistring);
begin
end;

procedure TZForkAboutPropertyEditor.Edit;
begin
  ShowZForkAbout;
end;

function TZForkFileNamePropertyEditor.GetAttributes: TPropertyAttributes;
begin
  Result := [paDialog, paMultiSelect];
end;

procedure TZForkFileNamePropertyEditor.Edit;
var
  Player: TZForkPlayer;
  Tags: TZForkID3;
  Bank: TZForkSoundBank;
  Dlg: TOpenDialog;
begin
  if GetComponent(0) is TZForkPlayer then
  begin
    Player := TZForkPlayer(GetComponent(0));
    if Player.BrowseFileName then
      Modified;
    Exit;
  end;
  if GetComponent(0) is TZForkID3 then
  begin
    Tags := TZForkID3(GetComponent(0));
    Dlg := TOpenDialog.Create(nil);
    try
      Dlg.Title := 'Audio / Tags';
      Dlg.Filter :=
        'Audio|*.mp3;*.wav;*.flac;*.ogg;*.opus;*.aiff;*.aif;*.m4a;*.aac|' +
        'All|*.*';
      Dlg.Options := Dlg.Options + [ofFileMustExist];
      if Tags.FileName <> '' then
        Dlg.FileName := Tags.FileName;
      if Dlg.Execute then
      begin
        SetStrValue(Dlg.FileName);
        Modified;
      end;
    finally
      Dlg.Free;
    end;
    Exit;
  end;
  if GetComponent(0) is TZForkSoundBank then
  begin
    Bank := TZForkSoundBank(GetComponent(0));
    if Bank.BrowseFileName then
      Modified;
  end;
end;

function TZForkOutputDevicePropertyEditor.GetAttributes: TPropertyAttributes;
begin
  Result := [paValueList, paMultiSelect, paRevertable];
end;

function TZForkOutputDevicePropertyEditor.GetValue: ansistring;
var
  Player: TZForkPlayer;
  Id: Integer;
  Nm: string;
begin
  Id := GetOrdValue;
  if Id < 0 then
    Exit('Auto (Standard)');
  Player := GetComponent(0) as TZForkPlayer;
  if Player <> nil then
    Nm := Player.GetOutputDeviceName(Id)
  else
    Nm := '';
  if Nm = '' then
    Result := IntToStr(Id)
  else
    Result := Format('%d: %s', [Id, Nm]);
end;

procedure TZForkOutputDevicePropertyEditor.GetValues(Proc: TGetStrProc);
var
  Player: TZForkPlayer;
  List: TStringList;
  I: Integer;
begin
  Player := GetComponent(0) as TZForkPlayer;
  List := TStringList.Create;
  try
    if Player <> nil then
      Player.ListOutputDevices(List)
    else
      List.Add('Auto (Standard)');
    for I := 0 to List.Count - 1 do
      Proc(List[I]);
  finally
    List.Free;
  end;
end;

procedure TZForkOutputDevicePropertyEditor.SetValue(const NewValue: ansistring);
var
  S: string;
  P: Integer;
  Id: Integer;
begin
  S := Trim(NewValue);
  if (S = '') or (CompareText(Copy(S, 1, 4), 'Auto') = 0) then
  begin
    SetOrdValue(-1);
    Exit;
  end;
  P := Pos(':', S);
  if P > 1 then
    S := Copy(S, 1, P - 1);
  if TryStrToInt(Trim(S), Id) then
    SetOrdValue(Id);
end;

procedure Register;
begin
  RegisterPropertyEditor(TypeInfo(string), TZForkPlayer, 'About',
    TZForkAboutPropertyEditor);
  RegisterPropertyEditor(TypeInfo(string), TZForkID3, 'About',
    TZForkAboutPropertyEditor);
  RegisterPropertyEditor(TypeInfo(string), TZForkSpectrum, 'About',
    TZForkAboutPropertyEditor);
  RegisterPropertyEditor(TypeInfo(string), TZForkBroadcast, 'About',
    TZForkAboutPropertyEditor);
  RegisterPropertyEditor(TypeInfo(string), TZForkAudioEngine, 'About',
    TZForkAboutPropertyEditor);
  RegisterPropertyEditor(TypeInfo(string), TZForkSoundBank, 'About',
    TZForkAboutPropertyEditor);
  RegisterPropertyEditor(TypeInfo(string), TZForkWaveform, 'About',
    TZForkAboutPropertyEditor);
  RegisterPropertyEditor(TypeInfo(string), TZForkCrossfader, 'About',
    TZForkAboutPropertyEditor);
  RegisterPropertyEditor(TypeInfo(string), TZForkChannelStrip, 'About',
    TZForkAboutPropertyEditor);
  RegisterPropertyEditor(TypeInfo(string), TZForkPlayer, 'FileName',
    TZForkFileNamePropertyEditor);
  RegisterPropertyEditor(TypeInfo(Integer), TZForkPlayer, 'OutputDevice',
    TZForkOutputDevicePropertyEditor);
  RegisterPropertyEditor(TypeInfo(string), TZForkID3, 'FileName',
    TZForkFileNamePropertyEditor);
  RegisterPropertyEditor(TypeInfo(string), TZForkSoundBank, 'FileName',
    TZForkFileNamePropertyEditor);
end;

end.
