{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  TZForkEQPanel — 3-Band-EQ als vertikale Fader

  Was die Komponente kann:
  - Low / Mid / High in dB live am TZForkPlayer
  - EqualizerEnabled; ResetFlat (z.B. Doppelklick auf Fader)
  - Kompaktes Layout fuer Mixer-/Booth-UIs
******************************************************************************}
unit ZForkEQPanel;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Math, Controls, Graphics, Forms, LCLType,
  ZForkPlayer, ZForkAbout;

type
  TZForkEQPanel = class(TCustomControl)
  private
    FPlayer: TZForkPlayer;
    FActive: Boolean;
    FEnabledEQ: Boolean;
    FLow, FMid, FHigh: Single;
    FBackColor: TColor;
    FAccentColor: TColor;
    FAbout: string;
    FDrag: Integer; { 0 none, 1 L, 2 M, 3 H }
    FOnChange: TNotifyEvent;
    procedure SetPlayer(AValue: TZForkPlayer);
    procedure SetActive(AValue: Boolean);
    procedure SetEnabledEQ(AValue: Boolean);
    procedure SetLow(AValue: Single);
    procedure SetMid(AValue: Single);
    procedure SetHigh(AValue: Single);
    procedure SetBackColor(AValue: TColor);
    procedure SetAccentColor(AValue: TColor);
    function GetAbout: string;
    procedure SetAbout(const AValue: string);
    procedure ClampDb(var V: Single);
    procedure ApplyEQ;
    procedure Changed;
    function BandRect(Index: Integer): TRect;
    function HitBand(X, Y: Integer): Integer;
    function DbToNorm(Db: Single): Single;
    function NormToDb(N: Single): Single;
  protected
    procedure Notification(AComponent: TComponent; Operation: TOperation); override;
    procedure Paint; override;
    procedure MouseDown(Button: TMouseButton; Shift: TShiftState; X, Y: Integer); override;
    procedure MouseMove(Shift: TShiftState; X, Y: Integer); override;
    procedure MouseUp(Button: TMouseButton; Shift: TShiftState; X, Y: Integer); override;
    procedure DblClick; override;
    procedure Loaded; override;
  public
    constructor Create(AOwner: TComponent); override;
    procedure ShowAbout;
    procedure ResetFlat;
  published
    property About: string read GetAbout write SetAbout stored False;
    property Align;
    property Anchors;
    property Constraints;
    property Enabled;
    property Visible;
    property OnClick;
    property OnDblClick;
    property OnMouseDown;
    property OnMouseMove;
    property OnMouseUp;

    property Player: TZForkPlayer read FPlayer write SetPlayer;
    property Active: Boolean read FActive write SetActive default True;
    property EqualizerEnabled: Boolean read FEnabledEQ write SetEnabledEQ default True;
    property LowDb: Single read FLow write SetLow;
    property MidDb: Single read FMid write SetMid;
    property HighDb: Single read FHigh write SetHigh;
    property BackColor: TColor read FBackColor write SetBackColor default clBlack;
    property AccentColor: TColor read FAccentColor write SetAccentColor default clFuchsia;
    property OnChange: TNotifyEvent read FOnChange write FOnChange;
  end;

implementation

const
  EQ_BAND_LOW  = 2;
  EQ_BAND_MID  = 12;
  EQ_BAND_HIGH = 24;

function MkRect(L, T, R, B: Integer): TRect;
begin
  Result.Left := L; Result.Top := T; Result.Right := R; Result.Bottom := B;
end;

constructor TZForkEQPanel.Create(AOwner: TComponent);
begin
  inherited Create(AOwner);
  ControlStyle := ControlStyle + [csOpaque, csCaptureMouse, csDoubleClicks];
  Width := 140;
  Height := 160;
  FActive := True;
  FEnabledEQ := True;
  FLow := 0; FMid := 0; FHigh := 0;
  FBackColor := RGBToColor(24, 24, 28);
  FAccentColor := clFuchsia;
  FAbout := '(About…)';
  Color := FBackColor;
  DoubleBuffered := True;
end;

procedure TZForkEQPanel.Loaded;
begin
  inherited Loaded;
  ApplyEQ;
end;

procedure TZForkEQPanel.Notification(AComponent: TComponent; Operation: TOperation);
begin
  inherited Notification(AComponent, Operation);
  if (Operation = opRemove) and (AComponent = FPlayer) then FPlayer := nil;
end;

function TZForkEQPanel.GetAbout: string;
begin
  Result := FAbout;
end;

procedure TZForkEQPanel.SetAbout(const AValue: string);
begin
  ShowAbout;
end;

procedure TZForkEQPanel.ShowAbout;
begin
  ShowZForkAbout;
end;

procedure TZForkEQPanel.Changed;
begin
  if Assigned(FOnChange) then FOnChange(Self);
end;

procedure TZForkEQPanel.ClampDb(var V: Single);
begin
  if V < -12 then V := -12;
  if V > 12 then V := 12;
end;

function TZForkEQPanel.DbToNorm(Db: Single): Single;
begin
  Result := (Db + 12) / 24.0;
end;

function TZForkEQPanel.NormToDb(N: Single): Single;
begin
  Result := N * 24.0 - 12.0;
end;

procedure TZForkEQPanel.SetPlayer(AValue: TZForkPlayer);
begin
  if FPlayer = AValue then Exit;
  if FPlayer <> nil then FPlayer.RemoveFreeNotification(Self);
  FPlayer := AValue;
  if FPlayer <> nil then FPlayer.FreeNotification(Self);
  ApplyEQ;
end;

procedure TZForkEQPanel.SetActive(AValue: Boolean);
begin
  FActive := AValue;
  if FActive then ApplyEQ;
end;

procedure TZForkEQPanel.SetEnabledEQ(AValue: Boolean);
begin
  if FEnabledEQ = AValue then Exit;
  FEnabledEQ := AValue;
  ApplyEQ;
  Invalidate;
  Changed;
end;

procedure TZForkEQPanel.SetLow(AValue: Single);
begin
  ClampDb(AValue);
  if Abs(FLow - AValue) < 0.01 then Exit;
  FLow := AValue;
  ApplyEQ;
  Invalidate;
  Changed;
end;

procedure TZForkEQPanel.SetMid(AValue: Single);
begin
  ClampDb(AValue);
  if Abs(FMid - AValue) < 0.01 then Exit;
  FMid := AValue;
  ApplyEQ;
  Invalidate;
  Changed;
end;

procedure TZForkEQPanel.SetHigh(AValue: Single);
begin
  ClampDb(AValue);
  if Abs(FHigh - AValue) < 0.01 then Exit;
  FHigh := AValue;
  ApplyEQ;
  Invalidate;
  Changed;
end;

procedure TZForkEQPanel.SetBackColor(AValue: TColor);
begin
  if FBackColor = AValue then Exit;
  FBackColor := AValue;
  Color := AValue;
  Invalidate;
end;

procedure TZForkEQPanel.SetAccentColor(AValue: TColor);
begin
  if FAccentColor = AValue then Exit;
  FAccentColor := AValue;
  Invalidate;
end;

procedure TZForkEQPanel.ApplyEQ;
begin
  if (not FActive) or (FPlayer = nil) or (csDesigning in ComponentState) then Exit;
  FPlayer.EqualizerEnabled := FEnabledEQ;
  if not FEnabledEQ then Exit;
  FPlayer.SetEQBandGain(EQ_BAND_LOW, FLow);
  FPlayer.SetEQBandGain(EQ_BAND_MID, FMid);
  FPlayer.SetEQBandGain(EQ_BAND_HIGH, FHigh);
end;

procedure TZForkEQPanel.ResetFlat;
begin
  FLow := 0; FMid := 0; FHigh := 0;
  ApplyEQ;
  Invalidate;
  Changed;
end;

function TZForkEQPanel.BandRect(Index: Integer): TRect;
var
  W, X: Integer;
begin
  W := (Width - 20) div 3;
  X := 8 + Index * (W + 2);
  Result := MkRect(X, 24, X + W, Height - 24);
end;

function TZForkEQPanel.HitBand(X, Y: Integer): Integer;
var
  I: Integer;
  R: TRect;
begin
  Result := 0;
  for I := 0 to 2 do
  begin
    R := BandRect(I);
    if (X >= R.Left) and (X < R.Right) and (Y >= R.Top) and (Y < R.Bottom) then
      Exit(I + 1);
  end;
end;

procedure TZForkEQPanel.Paint;
var
  I: Integer;
  R: TRect;
  Labels: array[0..2] of string = ('LOW', 'MID', 'HIGH');
  Dbs: array[0..2] of Single;
  Norm: Single;
  MidY, FillTop: Integer;
begin
  Canvas.Brush.Color := FBackColor;
  Canvas.FillRect(ClientRect);
  Canvas.Font.Color := clWhite;
  Canvas.Font.Height := -10;
  Canvas.Brush.Style := bsClear;
  if FEnabledEQ then
    Canvas.TextOut(8, 4, 'EQ')
  else
    Canvas.TextOut(8, 4, 'EQ off');
  Canvas.Brush.Style := bsSolid;

  Dbs[0] := FLow; Dbs[1] := FMid; Dbs[2] := FHigh;
  for I := 0 to 2 do
  begin
    R := BandRect(I);
    Canvas.Brush.Color := RGBToColor(36, 36, 42);
    Canvas.FillRect(R);
    MidY := (R.Top + R.Bottom) div 2;
    Canvas.Pen.Color := clGray;
    Canvas.MoveTo(R.Left, MidY);
    Canvas.LineTo(R.Right, MidY);
    Norm := DbToNorm(Dbs[I]);
    FillTop := R.Bottom - Round(Norm * (R.Bottom - R.Top));
    Canvas.Brush.Color := FAccentColor;
    if FillTop < MidY then
      Canvas.FillRect(MkRect(R.Left + 2, FillTop, R.Right - 2, MidY))
    else
      Canvas.FillRect(MkRect(R.Left + 2, MidY, R.Right - 2, FillTop));
    Canvas.Font.Color := clSilver;
    Canvas.Brush.Style := bsClear;
    Canvas.TextOut(R.Left + 2, Height - 18, Labels[I]);
    Canvas.Brush.Style := bsSolid;
  end;
end;

procedure TZForkEQPanel.MouseDown(Button: TMouseButton; Shift: TShiftState; X, Y: Integer);
var
  R: TRect;
  Norm: Single;
begin
  inherited MouseDown(Button, Shift, X, Y);
  if Button <> mbLeft then Exit;
  if Y < 20 then
  begin
    SetEnabledEQ(not FEnabledEQ);
    Exit;
  end;
  FDrag := HitBand(X, Y);
  if FDrag > 0 then
  begin
    R := BandRect(FDrag - 1);
    Norm := EnsureRange(1.0 - (Y - R.Top) / Max(1.0, R.Bottom - R.Top), 0, 1);
    case FDrag of
      1: SetLow(NormToDb(Norm));
      2: SetMid(NormToDb(Norm));
      3: SetHigh(NormToDb(Norm));
    end;
  end;
end;

procedure TZForkEQPanel.MouseMove(Shift: TShiftState; X, Y: Integer);
var
  R: TRect;
  Norm: Single;
begin
  inherited MouseMove(Shift, X, Y);
  if (not (ssLeft in Shift)) or (FDrag = 0) then Exit;
  R := BandRect(FDrag - 1);
  Norm := EnsureRange(1.0 - (Y - R.Top) / Max(1.0, R.Bottom - R.Top), 0, 1);
  case FDrag of
    1: SetLow(NormToDb(Norm));
    2: SetMid(NormToDb(Norm));
    3: SetHigh(NormToDb(Norm));
  end;
end;

procedure TZForkEQPanel.MouseUp(Button: TMouseButton; Shift: TShiftState; X, Y: Integer);
begin
  inherited MouseUp(Button, Shift, X, Y);
  FDrag := 0;
end;

procedure TZForkEQPanel.DblClick;
begin
  inherited DblClick;
  ResetFlat;
end;

end.
