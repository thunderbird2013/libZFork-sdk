{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  TZForkMixer — Master- und Kanalpegel A/B/C

  Was die Komponente kann:
  - Master-Fader + Kanaele A/B/C mit Mute
  - Steuert die gebundenen Player-Volumen live
  - Optional VU-Anzeige; OnChange bei Pegelwechsel
******************************************************************************}
unit ZForkMixer;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Math, Controls, Graphics, ExtCtrls, Forms, LCLType,
  ZForkPlayer, ZForkAbout;

type
  TZForkMixer = class(TCustomControl)
  private
    FPlayerA: TZForkPlayer;
    FPlayerB: TZForkPlayer;
    FPlayerC: TZForkPlayer;
    FActive: Boolean;
    FMaster: Integer;
    FVolA, FVolB, FVolC: Integer;
    FMuteA, FMuteB, FMuteC: Boolean;
    FBackColor: TColor;
    FAccentColor: TColor;
    FAbout: string;
    FTimer: TTimer;
    FVuA, FVuB, FVuC: Single;
    FDrag: Integer; { 0 none, 1 master, 2 A, 3 B, 4 C }
    FOnChange: TNotifyEvent;
    procedure SetPlayerA(AValue: TZForkPlayer);
    procedure SetPlayerB(AValue: TZForkPlayer);
    procedure SetPlayerC(AValue: TZForkPlayer);
    procedure SetActive(AValue: Boolean);
    procedure SetMaster(AValue: Integer);
    procedure SetVolA(AValue: Integer);
    procedure SetVolB(AValue: Integer);
    procedure SetVolC(AValue: Integer);
    procedure SetMuteA(AValue: Boolean);
    procedure SetMuteB(AValue: Boolean);
    procedure SetMuteC(AValue: Boolean);
    procedure SetBackColor(AValue: TColor);
    procedure SetAccentColor(AValue: TColor);
    function GetAbout: string;
    procedure SetAbout(const AValue: string);
    procedure ApplyAll;
    procedure ApplyOne(P: TZForkPlayer; Vol: Integer; Mute: Boolean);
    procedure TimerTick(Sender: TObject);
    procedure UpdateTimer;
    procedure Changed;
    function FaderRect(Index: Integer): TRect; { 0 master 1 A 2 B 3 C }
    function HitFader(X, Y: Integer): Integer;
  protected
    procedure Notification(AComponent: TComponent; Operation: TOperation); override;
    procedure Paint; override;
    procedure MouseDown(Button: TMouseButton; Shift: TShiftState; X, Y: Integer); override;
    procedure MouseMove(Shift: TShiftState; X, Y: Integer); override;
    procedure MouseUp(Button: TMouseButton; Shift: TShiftState; X, Y: Integer); override;
    procedure Loaded; override;
  public
    constructor Create(AOwner: TComponent); override;
    destructor Destroy; override;
    procedure ShowAbout;
  published
    property About: string read GetAbout write SetAbout stored False;
    property Align;
    property Anchors;
    property Constraints;
    property Enabled;
    property Visible;
    property OnClick;
    property OnMouseDown;
    property OnMouseMove;
    property OnMouseUp;

    property PlayerA: TZForkPlayer read FPlayerA write SetPlayerA;
    property PlayerB: TZForkPlayer read FPlayerB write SetPlayerB;
    property PlayerC: TZForkPlayer read FPlayerC write SetPlayerC;
    property Active: Boolean read FActive write SetActive default True;
    property MasterVolume: Integer read FMaster write SetMaster default 100;
    property VolumeA: Integer read FVolA write SetVolA default 100;
    property VolumeB: Integer read FVolB write SetVolB default 100;
    property VolumeC: Integer read FVolC write SetVolC default 100;
    property MuteA: Boolean read FMuteA write SetMuteA default False;
    property MuteB: Boolean read FMuteB write SetMuteB default False;
    property MuteC: Boolean read FMuteC write SetMuteC default False;
    property BackColor: TColor read FBackColor write SetBackColor default clBlack;
    property AccentColor: TColor read FAccentColor write SetAccentColor default clLime;
    property OnChange: TNotifyEvent read FOnChange write FOnChange;
  end;

implementation

function MkRect(L, T, R, B: Integer): TRect;
begin
  Result.Left := L; Result.Top := T; Result.Right := R; Result.Bottom := B;
end;

constructor TZForkMixer.Create(AOwner: TComponent);
begin
  inherited Create(AOwner);
  ControlStyle := ControlStyle + [csOpaque, csCaptureMouse];
  Width := 220;
  Height := 180;
  FActive := True;
  FMaster := 100;
  FVolA := 100; FVolB := 100; FVolC := 100;
  FBackColor := RGBToColor(24, 24, 28);
  FAccentColor := clLime;
  FAbout := '(About…)';
  FTimer := TTimer.Create(Self);
  FTimer.Enabled := False;
  FTimer.Interval := 50;
  FTimer.OnTimer := @TimerTick;
  Color := FBackColor;
  DoubleBuffered := True;
end;

destructor TZForkMixer.Destroy;
begin
  FTimer.Enabled := False;
  inherited Destroy;
end;

procedure TZForkMixer.Loaded;
begin
  inherited Loaded;
  ApplyAll;
  UpdateTimer;
end;

procedure TZForkMixer.Notification(AComponent: TComponent; Operation: TOperation);
begin
  inherited Notification(AComponent, Operation);
  if Operation = opRemove then
  begin
    if AComponent = FPlayerA then FPlayerA := nil;
    if AComponent = FPlayerB then FPlayerB := nil;
    if AComponent = FPlayerC then FPlayerC := nil;
  end;
end;

function TZForkMixer.GetAbout: string;
begin
  Result := FAbout;
end;

procedure TZForkMixer.SetAbout(const AValue: string);
begin
  ShowAbout;
end;

procedure TZForkMixer.ShowAbout;
begin
  ShowZForkAbout;
end;

procedure TZForkMixer.Changed;
begin
  if Assigned(FOnChange) then FOnChange(Self);
end;

procedure TZForkMixer.SetPlayerA(AValue: TZForkPlayer);
begin
  if FPlayerA = AValue then Exit;
  if FPlayerA <> nil then FPlayerA.RemoveFreeNotification(Self);
  FPlayerA := AValue;
  if FPlayerA <> nil then FPlayerA.FreeNotification(Self);
  ApplyAll;
end;

procedure TZForkMixer.SetPlayerB(AValue: TZForkPlayer);
begin
  if FPlayerB = AValue then Exit;
  if FPlayerB <> nil then FPlayerB.RemoveFreeNotification(Self);
  FPlayerB := AValue;
  if FPlayerB <> nil then FPlayerB.FreeNotification(Self);
  ApplyAll;
end;

procedure TZForkMixer.SetPlayerC(AValue: TZForkPlayer);
begin
  if FPlayerC = AValue then Exit;
  if FPlayerC <> nil then FPlayerC.RemoveFreeNotification(Self);
  FPlayerC := AValue;
  if FPlayerC <> nil then FPlayerC.FreeNotification(Self);
  ApplyAll;
end;

procedure TZForkMixer.SetActive(AValue: Boolean);
begin
  FActive := AValue;
  UpdateTimer;
  if FActive then ApplyAll;
end;

procedure TZForkMixer.SetMaster(AValue: Integer);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 100 then AValue := 100;
  if FMaster = AValue then Exit;
  FMaster := AValue;
  ApplyAll;
  Invalidate;
  Changed;
end;

procedure TZForkMixer.SetVolA(AValue: Integer);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 100 then AValue := 100;
  if FVolA = AValue then Exit;
  FVolA := AValue;
  ApplyAll;
  Invalidate;
  Changed;
end;

procedure TZForkMixer.SetVolB(AValue: Integer);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 100 then AValue := 100;
  if FVolB = AValue then Exit;
  FVolB := AValue;
  ApplyAll;
  Invalidate;
  Changed;
end;

procedure TZForkMixer.SetVolC(AValue: Integer);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 100 then AValue := 100;
  if FVolC = AValue then Exit;
  FVolC := AValue;
  ApplyAll;
  Invalidate;
  Changed;
end;

procedure TZForkMixer.SetMuteA(AValue: Boolean);
begin
  if FMuteA = AValue then Exit;
  FMuteA := AValue;
  ApplyAll;
  Invalidate;
  Changed;
end;

procedure TZForkMixer.SetMuteB(AValue: Boolean);
begin
  if FMuteB = AValue then Exit;
  FMuteB := AValue;
  ApplyAll;
  Invalidate;
  Changed;
end;

procedure TZForkMixer.SetMuteC(AValue: Boolean);
begin
  if FMuteC = AValue then Exit;
  FMuteC := AValue;
  ApplyAll;
  Invalidate;
  Changed;
end;

procedure TZForkMixer.SetBackColor(AValue: TColor);
begin
  if FBackColor = AValue then Exit;
  FBackColor := AValue;
  Color := AValue;
  Invalidate;
end;

procedure TZForkMixer.SetAccentColor(AValue: TColor);
begin
  if FAccentColor = AValue then Exit;
  FAccentColor := AValue;
  Invalidate;
end;

procedure TZForkMixer.ApplyOne(P: TZForkPlayer; Vol: Integer; Mute: Boolean);
var
  G: Integer;
begin
  if P = nil then Exit;
  if Mute then G := 0
  else G := (Vol * FMaster) div 100;
  P.SetPlayerVolume(G, G);
end;

procedure TZForkMixer.ApplyAll;
begin
  if not FActive or (csDesigning in ComponentState) then Exit;
  ApplyOne(FPlayerA, FVolA, FMuteA);
  ApplyOne(FPlayerB, FVolB, FMuteB);
  ApplyOne(FPlayerC, FVolC, FMuteC);
end;

procedure TZForkMixer.UpdateTimer;
begin
  FTimer.Enabled := FActive and not (csDesigning in ComponentState)
    and not (csLoading in ComponentState);
end;

procedure TZForkMixer.TimerTick(Sender: TObject);
var
  L, R: Single;
begin
  if FPlayerA <> nil then
  begin
    FPlayerA.GetVUData(L, R);
    FVuA := Max(L, R);
  end;
  if FPlayerB <> nil then
  begin
    FPlayerB.GetVUData(L, R);
    FVuB := Max(L, R);
  end;
  if FPlayerC <> nil then
  begin
    FPlayerC.GetVUData(L, R);
    FVuC := Max(L, R);
  end;
  Invalidate;
end;

function TZForkMixer.FaderRect(Index: Integer): TRect;
var
  W, X: Integer;
begin
  W := (Width - 20) div 4;
  X := 8 + Index * (W + 4);
  Result := MkRect(X, 28, X + W, Height - 28);
end;

function TZForkMixer.HitFader(X, Y: Integer): Integer;
var
  I: Integer;
  R: TRect;
begin
  Result := 0;
  for I := 0 to 3 do
  begin
    R := FaderRect(I);
    if (X >= R.Left) and (X < R.Right) and (Y >= R.Top) and (Y < R.Bottom) then
      Exit(I + 1);
  end;
end;

procedure TZForkMixer.Paint;
var
  I: Integer;
  R: TRect;
  Labels: array[0..3] of string = ('MST', 'A', 'B', 'C');
  Vols: array[0..3] of Integer;
  Vus: array[0..3] of Single;
  H, FillH: Integer;
  Mute: Boolean;
begin
  Canvas.Brush.Color := FBackColor;
  Canvas.FillRect(ClientRect);
  Canvas.Font.Color := clWhite;
  Canvas.Font.Height := -11;
  Canvas.Brush.Style := bsClear;
  Canvas.TextOut(8, 6, 'Mixer');
  Canvas.Brush.Style := bsSolid;

  Vols[0] := FMaster; Vols[1] := FVolA; Vols[2] := FVolB; Vols[3] := FVolC;
  Vus[0] := Max(FVuA, Max(FVuB, FVuC));
  Vus[1] := FVuA; Vus[2] := FVuB; Vus[3] := FVuC;

  for I := 0 to 3 do
  begin
    R := FaderRect(I);
    Canvas.Brush.Color := RGBToColor(36, 36, 42);
    Canvas.FillRect(R);
    H := R.Bottom - R.Top;
    FillH := Round((Vols[I] / 100.0) * H);
    Canvas.Brush.Color := FAccentColor;
    Canvas.FillRect(MkRect(R.Left, R.Bottom - FillH, R.Right - 6, R.Bottom));
    FillH := Round(EnsureRange(Vus[I], 0, 1) * H);
    Canvas.Brush.Color := clYellow;
    Canvas.FillRect(MkRect(R.Right - 5, R.Bottom - FillH, R.Right, R.Bottom));
    Mute := ((I = 1) and FMuteA) or ((I = 2) and FMuteB) or ((I = 3) and FMuteC);
    Canvas.Font.Color := clWhite;
    if Mute then Canvas.Font.Color := clRed;
    Canvas.Brush.Style := bsClear;
    Canvas.TextOut(R.Left + 2, Height - 22, Labels[I]);
    Canvas.Brush.Style := bsSolid;
  end;
end;

procedure TZForkMixer.MouseDown(Button: TMouseButton; Shift: TShiftState; X, Y: Integer);
var
  Hit: Integer;
  R: TRect;
  Norm: Single;
begin
  inherited MouseDown(Button, Shift, X, Y);
  if Button <> mbLeft then Exit;
  { Doppelklick-Mute auf Labels unten }
  if Y > Height - 24 then
  begin
    Hit := HitFader(X, Height div 2);
    case Hit of
      2: SetMuteA(not FMuteA);
      3: SetMuteB(not FMuteB);
      4: SetMuteC(not FMuteC);
    end;
    Exit;
  end;
  FDrag := HitFader(X, Y);
  if FDrag > 0 then
  begin
    R := FaderRect(FDrag - 1);
    Norm := EnsureRange(1.0 - (Y - R.Top) / Max(1.0, R.Bottom - R.Top), 0, 1);
    case FDrag of
      1: SetMaster(Round(Norm * 100));
      2: SetVolA(Round(Norm * 100));
      3: SetVolB(Round(Norm * 100));
      4: SetVolC(Round(Norm * 100));
    end;
  end;
end;

procedure TZForkMixer.MouseMove(Shift: TShiftState; X, Y: Integer);
var
  R: TRect;
  Norm: Single;
begin
  inherited MouseMove(Shift, X, Y);
  if (not (ssLeft in Shift)) or (FDrag = 0) then Exit;
  R := FaderRect(FDrag - 1);
  Norm := EnsureRange(1.0 - (Y - R.Top) / Max(1.0, R.Bottom - R.Top), 0, 1);
  case FDrag of
    1: SetMaster(Round(Norm * 100));
    2: SetVolA(Round(Norm * 100));
    3: SetVolB(Round(Norm * 100));
    4: SetVolC(Round(Norm * 100));
  end;
end;

procedure TZForkMixer.MouseUp(Button: TMouseButton; Shift: TShiftState; X, Y: Integer);
begin
  inherited MouseUp(Button, Shift, X, Y);
  FDrag := 0;
end;

end.
