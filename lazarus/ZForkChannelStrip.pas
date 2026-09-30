{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  TZForkChannelStrip — Gain + 3-Band-EQ als Drehregler (DJ-Channel)

  Was die Komponente kann:
  - Gain und Low/Mid/High live am gebundenen TZForkPlayer
  - Mitte = 0 dB (flat), links Richtung Kill, rechts Boost
  - Optional VU-Anzeige; ResetEQ auf Flat
  - OnChange bei Reglerbewegung
******************************************************************************}
unit ZForkChannelStrip;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Math, Types, Controls, Graphics, ExtCtrls, Forms, LCLType,
  ZForkPlayer, ZForkAbout;

type
  TZForkStripDrag = (
    sdNone,
    sdGain,
    sdEqLow,
    sdEqMid,
    sdEqHigh
  );

  TZForkChannelStrip = class(TCustomControl)
  private
    FPlayer: TZForkPlayer;
    FActive: Boolean;
    FGain: Integer;          { 0..100 → MasterVolume }
    FEqLow: Single;          { dB, Mitte = 0 }
    FEqMid: Single;
    FEqHigh: Single;
    FEqEnabled: Boolean;
    FShowVU: Boolean;
    FBackColor: TColor;
    FAccentColor: TColor;
    FCaption: string;
    FAbout: string;
    FVuL: Single;
    FVuR: Single;
    FTimer: TTimer;
    FDrag: TZForkStripDrag;
    FDragLastY: Integer;
    FAntiAlias: Boolean;
    FKnobCache: TBitmap;
    FKnobDirty: Boolean;
    FLastVu: Single;
    FOnChange: TNotifyEvent;
    procedure SetPlayer(AValue: TZForkPlayer);
    procedure SetActive(AValue: Boolean);
    procedure SetGain(AValue: Integer);
    procedure SetEqLow(AValue: Single);
    procedure SetEqMid(AValue: Single);
    procedure SetEqHigh(AValue: Single);
    procedure SetEqEnabled(AValue: Boolean);
    procedure SetShowVU(AValue: Boolean);
    procedure SetBackColor(AValue: TColor);
    procedure SetAccentColor(AValue: TColor);
    procedure SetCaption(const AValue: string);
    procedure SetAntiAlias(AValue: Boolean);
    function GetAbout: string;
    procedure SetAbout(const AValue: string);
    procedure ApplyGain;
    procedure ApplyEQ;
    procedure ClampEq(var V: Single);
    procedure TimerTick(Sender: TObject);
    procedure UpdateTimer;
    procedure MarkKnobsDirty;
    function KnobCell(Index: Integer): TRect; { 0=Gain, 1=L, 2=M, 3=H }
    function VuRect: TRect;
    function HitTest(X, Y: Integer): TZForkStripDrag;
    procedure DragDelta(ADrag: TZForkStripDrag; DY: Integer);
    procedure HardDisk(ACanvas: TCanvas; CX, CY, R: Integer; Col: TColor);
    procedure HardRing(ACanvas: TCanvas; CX, CY, ROuter, RInner: Integer;
      OuterCol, InnerCol: TColor);
    procedure HardLine(ACanvas: TCanvas; X1, Y1, X2, Y2: Integer; Col: TColor; W: Integer);
    procedure DrawKnob(ACanvas: TCanvas; const Cell: TRect; Norm: Single;
      const ALabel: string; Accent: TColor; CenterDetent: Boolean);
    procedure RebuildKnobCache;
    procedure DrawVU(ACanvas: TCanvas);
    procedure Changed;
  protected
    procedure Notification(AComponent: TComponent; Operation: TOperation); override;
    procedure Paint; override;
    procedure MouseDown(Button: TMouseButton; Shift: TShiftState; X, Y: Integer); override;
    procedure MouseMove(Shift: TShiftState; X, Y: Integer); override;
    procedure MouseUp(Button: TMouseButton; Shift: TShiftState; X, Y: Integer); override;
    procedure DblClick; override;
    function DoMouseWheel(Shift: TShiftState; WheelDelta: Integer;
      MousePos: TPoint): Boolean; override;
    procedure Loaded; override;
  public
    constructor Create(AOwner: TComponent); override;
    destructor Destroy; override;
    procedure ShowAbout;
    procedure ResetEQ;
  published
    property About: string read GetAbout write SetAbout stored False;
    property Align;
    property Anchors;
    property Constraints;
    property Enabled;
    property Hint;
    property ParentShowHint;
    property PopupMenu;
    property ShowHint;
    property Visible;
    property OnClick;
    property OnDblClick;
    property OnMouseDown;
    property OnMouseMove;
    property OnMouseUp;

    property Player: TZForkPlayer read FPlayer write SetPlayer;
    property Active: Boolean read FActive write SetActive default True;
    property Caption: string read FCaption write SetCaption;
    property Gain: Integer read FGain write SetGain default 100;
    property EqLow: Single read FEqLow write SetEqLow;
    property EqMid: Single read FEqMid write SetEqMid;
    property EqHigh: Single read FEqHigh write SetEqHigh;
    property EqEnabled: Boolean read FEqEnabled write SetEqEnabled default True;
    property ShowVU: Boolean read FShowVU write SetShowVU default True;
    property BackColor: TColor read FBackColor write SetBackColor default clBlack;
    property AccentColor: TColor read FAccentColor write SetAccentColor default clAqua;
    property AntiAlias: Boolean read FAntiAlias write SetAntiAlias default False;
    property OnChange: TNotifyEvent read FOnChange write FOnChange;
  end;

implementation

const
  EQ_BAND_LOW  = 2;
  EQ_BAND_MID  = 12;
  EQ_BAND_HIGH = 24;
  { DJ-Console: voll links ≈ Kill, Mitte flat, rechts Boost }
  EQ_MIN_DB    = -26.0;
  EQ_MAX_DB    = 6.0;
  EQ_SNAP_DB   = 0.35;
  KNOB_TRAVEL  = 270.0; { Grad, −135 … +135 }
  DRAG_SENS    = 1.8;   { Pixel → Norm }

function MixColor(C1, C2: TColor; T: Single): TColor;
var
  R1, G1, B1, R2, G2, B2: Byte;
begin
  T := EnsureRange(T, 0, 1);
  RedGreenBlue(ColorToRGB(C1), R1, G1, B1);
  RedGreenBlue(ColorToRGB(C2), R2, G2, B2);
  Result := RGBToColor(
    Round(R1 + (R2 - R1) * T),
    Round(G1 + (G2 - G1) * T),
    Round(B1 + (B2 - B1) * T));
end;

{ Norm 0..1: EQ asymmetric (0.5 = 0 dB), Gain linear }
function EqDbToNorm(Db: Single): Single;
begin
  if Db >= 0 then
    Result := 0.5 + 0.5 * EnsureRange(Db / EQ_MAX_DB, 0, 1)
  else
    Result := 0.5 * (1.0 - EnsureRange((-Db) / (-EQ_MIN_DB), 0, 1));
end;

function EqNormToDb(Norm: Single): Single;
begin
  Norm := EnsureRange(Norm, 0, 1);
  if Norm >= 0.5 then
    Result := ((Norm - 0.5) / 0.5) * EQ_MAX_DB
  else
    Result := -((0.5 - Norm) / 0.5) * (-EQ_MIN_DB);
  if Abs(Result) < EQ_SNAP_DB then
    Result := 0;
end;

function NormToAngleRad(Norm: Single): Single;
begin
  { 0 → −135°, 0.5 → 0° (oben), 1 → +135° }
  Result := DegToRad(-135.0 + EnsureRange(Norm, 0, 1) * KNOB_TRAVEL);
end;

{ TZForkChannelStrip }

constructor TZForkChannelStrip.Create(AOwner: TComponent);
begin
  inherited Create(AOwner);
  ControlStyle := ControlStyle + [csOpaque, csCaptureMouse, csClickEvents,
    csDoubleClicks];
  Width := 200;
  Height := 110;
  FActive := True;
  FGain := 100;
  FEqLow := 0;
  FEqMid := 0;
  FEqHigh := 0;
  FEqEnabled := True;
  FShowVU := True;
  FBackColor := RGBToColor(28, 28, 30);
  FAccentColor := RGBToColor(0, 220, 80);
  FCaption := 'CH';
  FAbout := '(About…)';
  FAntiAlias := False; { scharf + schnell; AA optional via StretchDraw }
  FDrag := sdNone;
  FKnobDirty := True;
  FLastVu := -1;
  FKnobCache := TBitmap.Create;
  FKnobCache.PixelFormat := pf24bit;
  FTimer := TTimer.Create(Self);
  FTimer.Enabled := False;
  FTimer.Interval := 50; { VU ~20 Hz — reicht, ohne UI zu blockieren }
  FTimer.OnTimer := @TimerTick;
  Color := FBackColor;
  DoubleBuffered := True;
end;

destructor TZForkChannelStrip.Destroy;
begin
  FTimer.Enabled := False;
  FreeAndNil(FKnobCache);
  if FPlayer <> nil then
  begin
    FPlayer.RemoveFreeNotification(Self);
    FPlayer := nil;
  end;
  inherited Destroy;
end;

function TZForkChannelStrip.GetAbout: string;
begin
  Result := FAbout;
end;

procedure TZForkChannelStrip.SetAbout(const AValue: string);
begin
  ShowAbout;
end;

procedure TZForkChannelStrip.ShowAbout;
begin
  ShowZForkAbout;
end;

procedure TZForkChannelStrip.Changed;
begin
  if Assigned(FOnChange) then FOnChange(Self);
end;

procedure TZForkChannelStrip.Loaded;
begin
  inherited Loaded;
  if FActive and not (csDesigning in ComponentState) then
  begin
    ApplyGain;
    ApplyEQ;
  end;
  UpdateTimer;
end;

procedure TZForkChannelStrip.Notification(AComponent: TComponent; Operation: TOperation);
begin
  inherited Notification(AComponent, Operation);
  if (Operation = opRemove) and (AComponent = FPlayer) then
    FPlayer := nil;
end;

procedure TZForkChannelStrip.SetPlayer(AValue: TZForkPlayer);
begin
  if FPlayer = AValue then Exit;
  if FPlayer <> nil then FPlayer.RemoveFreeNotification(Self);
  FPlayer := AValue;
  if FPlayer <> nil then FPlayer.FreeNotification(Self);
  if FActive then
  begin
    ApplyGain;
    ApplyEQ;
  end;
  UpdateTimer;
end;

procedure TZForkChannelStrip.SetActive(AValue: Boolean);
begin
  if FActive = AValue then Exit;
  FActive := AValue;
  if FActive then
  begin
    ApplyGain;
    ApplyEQ;
  end;
  UpdateTimer;
  MarkKnobsDirty;
end;

procedure TZForkChannelStrip.ClampEq(var V: Single);
begin
  if V < EQ_MIN_DB then V := EQ_MIN_DB;
  if V > EQ_MAX_DB then V := EQ_MAX_DB;
  if Abs(V) < EQ_SNAP_DB then V := 0;
end;

procedure TZForkChannelStrip.SetGain(AValue: Integer);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 100 then AValue := 100;
  if FGain = AValue then Exit;
  FGain := AValue;
  if FActive then ApplyGain;
  MarkKnobsDirty;
  Changed;
end;

procedure TZForkChannelStrip.SetEqLow(AValue: Single);
begin
  ClampEq(AValue);
  if Abs(FEqLow - AValue) < 0.01 then Exit;
  FEqLow := AValue;
  if FActive then ApplyEQ;
  MarkKnobsDirty;
  Changed;
end;

procedure TZForkChannelStrip.SetEqMid(AValue: Single);
begin
  ClampEq(AValue);
  if Abs(FEqMid - AValue) < 0.01 then Exit;
  FEqMid := AValue;
  if FActive then ApplyEQ;
  MarkKnobsDirty;
  Changed;
end;

procedure TZForkChannelStrip.SetEqHigh(AValue: Single);
begin
  ClampEq(AValue);
  if Abs(FEqHigh - AValue) < 0.01 then Exit;
  FEqHigh := AValue;
  if FActive then ApplyEQ;
  MarkKnobsDirty;
  Changed;
end;

procedure TZForkChannelStrip.SetEqEnabled(AValue: Boolean);
begin
  if FEqEnabled = AValue then Exit;
  FEqEnabled := AValue;
  if FActive then ApplyEQ;
  MarkKnobsDirty;
  Changed;
end;

procedure TZForkChannelStrip.SetShowVU(AValue: Boolean);
begin
  if FShowVU = AValue then Exit;
  FShowVU := AValue;
  UpdateTimer;
  MarkKnobsDirty;
end;

procedure TZForkChannelStrip.SetBackColor(AValue: TColor);
begin
  if FBackColor = AValue then Exit;
  FBackColor := AValue;
  Color := AValue;
  MarkKnobsDirty;
end;

procedure TZForkChannelStrip.SetAccentColor(AValue: TColor);
begin
  if FAccentColor = AValue then Exit;
  FAccentColor := AValue;
  MarkKnobsDirty;
end;

procedure TZForkChannelStrip.SetCaption(const AValue: string);
begin
  if FCaption = AValue then Exit;
  FCaption := AValue;
  MarkKnobsDirty;
end;

procedure TZForkChannelStrip.SetAntiAlias(AValue: Boolean);
begin
  if FAntiAlias = AValue then Exit;
  FAntiAlias := AValue;
  MarkKnobsDirty;
end;

procedure TZForkChannelStrip.ApplyGain;
begin
  if csDesigning in ComponentState then Exit;
  if FPlayer = nil then Exit;
  FPlayer.MasterVolumeLeft := FGain;
  FPlayer.MasterVolumeRight := FGain;
end;

procedure TZForkChannelStrip.ApplyEQ;
begin
  if csDesigning in ComponentState then Exit;
  if FPlayer = nil then Exit;
  if FPlayer.Handle = nil then Exit;
  FPlayer.EqualizerEnabled := FEqEnabled and FActive;
  if not FEqEnabled then Exit;
  FPlayer.SetEQBandGain(EQ_BAND_LOW, FEqLow);
  FPlayer.SetEQBandGain(EQ_BAND_MID, FEqMid);
  FPlayer.SetEQBandGain(EQ_BAND_HIGH, FEqHigh);
end;

procedure TZForkChannelStrip.ResetEQ;
begin
  FEqLow := 0;
  FEqMid := 0;
  FEqHigh := 0;
  if FActive then ApplyEQ;
  MarkKnobsDirty;
  Changed;
end;

procedure TZForkChannelStrip.MarkKnobsDirty;
begin
  FKnobDirty := True;
  Invalidate;
end;

procedure TZForkChannelStrip.UpdateTimer;
begin
  if csDesigning in ComponentState then
  begin
    FTimer.Enabled := False;
    Exit;
  end;
  FTimer.Enabled := FActive and FShowVU and (FPlayer <> nil) and
    not (csLoading in ComponentState);
end;

procedure TZForkChannelStrip.TimerTick(Sender: TObject);
var
  L, R, VU: Single;
begin
  if (FPlayer = nil) or (FPlayer.Handle = nil) then
  begin
    if FLastVu <> 0 then
    begin
      FVuL := 0;
      FVuR := 0;
      FLastVu := 0;
      Invalidate; { nur VU ändert sich — Paint blittet Cache }
    end;
    Exit;
  end;
  if FPlayer.GetVUData(L, R) then
  begin
    FVuL := EnsureRange(L, 0, 1);
    FVuR := EnsureRange(R, 0, 1);
  end
  else
  begin
    FVuL := 0;
    FVuR := 0;
  end;
  VU := Max(FVuL, FVuR);
  { nur bei merklicher Änderung neuzeichnen }
  if Abs(VU - FLastVu) < 0.03 then Exit;
  FLastVu := VU;
  Invalidate;
end;

function TZForkChannelStrip.KnobCell(Index: Integer): TRect;
var
  TopY, BotY, CellW, X0: Integer;
begin
  TopY := 18;
  if FShowVU then
    BotY := Height - 16
  else
    BotY := Height - 4;
  CellW := Max(36, (Width - 10) div 4);
  X0 := 5 + Index * CellW;
  Result := Rect(X0, TopY, X0 + CellW - 2, BotY);
end;

function TZForkChannelStrip.VuRect: TRect;
begin
  if FShowVU then
    Result := Rect(6, Height - 12, Width - 6, Height - 4)
  else
    Result := Rect(0, 0, 0, 0);
end;

function TZForkChannelStrip.HitTest(X, Y: Integer): TZForkStripDrag;
var
  I: Integer;
begin
  for I := 0 to 3 do
    if PtInRect(KnobCell(I), Point(X, Y)) then
      case I of
        0: Exit(sdGain);
        1: Exit(sdEqLow);
        2: Exit(sdEqMid);
      else
        Exit(sdEqHigh);
      end;
  Result := sdNone;
end;

procedure TZForkChannelStrip.DragDelta(ADrag: TZForkStripDrag; DY: Integer);
var
  Norm, D: Single;
begin
  D := -DY / (40.0 * DRAG_SENS); { hoch = mehr }
  case ADrag of
    sdGain:
      begin
        Norm := EnsureRange(FGain / 100.0 + D, 0, 1);
        SetGain(Round(Norm * 100));
      end;
    sdEqLow:
      begin
        Norm := EnsureRange(EqDbToNorm(FEqLow) + D, 0, 1);
        SetEqLow(EqNormToDb(Norm));
      end;
    sdEqMid:
      begin
        Norm := EnsureRange(EqDbToNorm(FEqMid) + D, 0, 1);
        SetEqMid(EqNormToDb(Norm));
      end;
    sdEqHigh:
      begin
        Norm := EnsureRange(EqDbToNorm(FEqHigh) + D, 0, 1);
        SetEqHigh(EqNormToDb(Norm));
      end;
  end;
end;

procedure TZForkChannelStrip.MouseDown(Button: TMouseButton; Shift: TShiftState;
  X, Y: Integer);
begin
  inherited MouseDown(Button, Shift, X, Y);
  if Button <> mbLeft then Exit;
  if not Enabled then Exit;
  FDrag := HitTest(X, Y);
  if FDrag <> sdNone then
  begin
    FDragLastY := Y;
    MouseCapture := True;
  end;
end;

procedure TZForkChannelStrip.MouseMove(Shift: TShiftState; X, Y: Integer);
var
  DY: Integer;
begin
  inherited MouseMove(Shift, X, Y);
  if FDrag = sdNone then Exit;
  DY := Y - FDragLastY;
  if DY = 0 then Exit;
  FDragLastY := Y;
  DragDelta(FDrag, DY);
end;

procedure TZForkChannelStrip.MouseUp(Button: TMouseButton; Shift: TShiftState;
  X, Y: Integer);
begin
  inherited MouseUp(Button, Shift, X, Y);
  if FDrag <> sdNone then
  begin
    FDrag := sdNone;
    MouseCapture := False;
  end;
end;

procedure TZForkChannelStrip.DblClick;
var
  P: TPoint;
  H: TZForkStripDrag;
begin
  inherited DblClick;
  P := ScreenToClient(Mouse.CursorPos);
  H := HitTest(P.X, P.Y);
  case H of
    sdGain: SetGain(100);
    sdEqLow: SetEqLow(0);
    sdEqMid: SetEqMid(0);
    sdEqHigh: SetEqHigh(0);
  end;
end;

function TZForkChannelStrip.DoMouseWheel(Shift: TShiftState; WheelDelta: Integer;
  MousePos: TPoint): Boolean;
var
  H: TZForkStripDrag;
  Steps: Integer;
begin
  Result := inherited DoMouseWheel(Shift, WheelDelta, MousePos);
  H := HitTest(MousePos.X, MousePos.Y);
  if H = sdNone then Exit;
  Steps := -Sign(WheelDelta);
  if ssShift in Shift then
    DragDelta(H, Steps * 2)
  else
    DragDelta(H, Steps * 6);
  Result := True;
end;

procedure TZForkChannelStrip.DrawKnob(ACanvas: TCanvas; const Cell: TRect;
  Norm: Single; const ALabel: string; Accent: TColor; CenterDetent: Boolean);
var
  CX, CY, RWell, RKnob, PX, PY, NX, NY, TW, CW, CH: Integer;
  Ang, GlowAng: Single;
  InnerWell: TColor;
begin
  CW := Max(1, Cell.Right - Cell.Left);
  CH := Max(1, Cell.Bottom - Cell.Top);
  CX := Cell.Left + CW div 2;
  CY := Cell.Top + (CH - 14) div 2;
  RWell := Max(14, Min(CW, CH - 16) div 2 - 2);
  RKnob := Max(10, RWell - 6);
  Ang := NormToAngleRad(Norm);
  GlowAng := Ang - Pi / 2;
  InnerWell := RGBToColor(18, 18, 20);

  HardDisk(ACanvas, CX, CY, RWell, RGBToColor(12, 12, 14));
  HardRing(ACanvas, CX, CY, RWell - 1, RWell - 3, RGBToColor(52, 52, 56), InnerWell);
  HardDisk(ACanvas, CX, CY, RWell - 4, InnerWell);

  if CenterDetent then
    HardLine(ACanvas, CX - 4, CY + RWell - 3, CX + 4, CY + RWell - 3,
      RGBToColor(90, 90, 94), 1);
  HardLine(ACanvas, CX, CY - RWell - 1, CX, CY - RWell + 4,
    RGBToColor(190, 190, 195), 1);

  NX := CX + Round(Cos(GlowAng) * (RWell - 2));
  NY := CY + Round(Sin(GlowAng) * (RWell - 2));
  HardDisk(ACanvas, NX, NY, 2, Accent);

  HardDisk(ACanvas, CX, CY + 1, RKnob, RGBToColor(42, 42, 46));
  HardDisk(ACanvas, CX, CY, RKnob - 1, RGBToColor(88, 88, 92));
  HardDisk(ACanvas, CX, CY - 1, RKnob - 3, RGBToColor(110, 110, 114));
  HardDisk(ACanvas, CX, CY, RKnob - 5, RGBToColor(72, 72, 76));

  PX := CX + Round(Cos(GlowAng) * (RKnob - 3));
  PY := CY + Round(Sin(GlowAng) * (RKnob - 3));
  HardLine(ACanvas, CX, CY, PX, PY, RGBToColor(22, 22, 24), 2);
  HardLine(ACanvas, CX, CY, PX, PY, RGBToColor(230, 230, 235), 1);

  ACanvas.Brush.Style := bsClear;
  ACanvas.Font.Color := clSilver;
  ACanvas.Font.Height := -10;
  ACanvas.Font.Style := [fsBold];
  TW := ACanvas.TextWidth(ALabel);
  ACanvas.TextOut(Cell.Left + (CW - TW) div 2, Cell.Bottom - 13, ALabel);
  ACanvas.Brush.Style := bsSolid;
  ACanvas.Font.Style := [];
  ACanvas.Pen.Style := psSolid;
  ACanvas.Pen.Width := 1;
end;

procedure TZForkChannelStrip.HardDisk(ACanvas: TCanvas; CX, CY, R: Integer; Col: TColor);
begin
  if R < 1 then Exit;
  ACanvas.Pen.Style := psSolid;
  ACanvas.Pen.Width := 1;
  ACanvas.Pen.Color := Col;
  ACanvas.Brush.Style := bsSolid;
  ACanvas.Brush.Color := Col;
  ACanvas.Ellipse(CX - R, CY - R, CX + R + 1, CY + R + 1);
end;

procedure TZForkChannelStrip.HardRing(ACanvas: TCanvas; CX, CY, ROuter, RInner: Integer;
  OuterCol, InnerCol: TColor);
begin
  if ROuter <= RInner then Exit;
  HardDisk(ACanvas, CX, CY, ROuter, OuterCol);
  HardDisk(ACanvas, CX, CY, RInner, InnerCol);
end;

procedure TZForkChannelStrip.HardLine(ACanvas: TCanvas; X1, Y1, X2, Y2: Integer;
  Col: TColor; W: Integer);
begin
  ACanvas.Pen.Style := psSolid;
  ACanvas.Pen.Width := Max(1, W);
  ACanvas.Pen.Color := Col;
  ACanvas.MoveTo(X1, Y1);
  ACanvas.LineTo(X2, Y2);
  ACanvas.Pen.Width := 1;
end;

procedure TZForkChannelStrip.RebuildKnobCache;
var
  AC: TCanvas;
  EqAccent: TColor;
begin
  if (Width < 8) or (Height < 8) then Exit;
  FKnobCache.SetSize(Width, Height);
  AC := FKnobCache.Canvas;
  AC.Brush.Color := FBackColor;
  AC.FillRect(0, 0, Width, Height);
  AC.Font.Color := clWhite;
  AC.Font.Height := -12;
  AC.Font.Style := [fsBold];
  AC.Brush.Style := bsClear;
  AC.TextOut(6, 2, FCaption);
  AC.Brush.Style := bsSolid;
  if FEqEnabled then EqAccent := FAccentColor else EqAccent := clGray;
  DrawKnob(AC, KnobCell(0), FGain / 100.0, 'GAIN', FAccentColor, False);
  DrawKnob(AC, KnobCell(1), EqDbToNorm(FEqLow), 'LOW', EqAccent, True);
  DrawKnob(AC, KnobCell(2), EqDbToNorm(FEqMid), 'MID', EqAccent, True);
  DrawKnob(AC, KnobCell(3), EqDbToNorm(FEqHigh), 'HIGH', EqAccent, True);
  if FShowVU then
  begin
    AC.Brush.Color := FBackColor;
    AC.FillRect(VuRect);
  end;
  FKnobDirty := False;
end;

procedure TZForkChannelStrip.DrawVU(ACanvas: TCanvas);
var
  VR: TRect;
  VU: Single;
begin
  if not FShowVU then Exit;
  VR := VuRect;
  if (VR.Right <= VR.Left) or (VR.Bottom <= VR.Top) then Exit;
  ACanvas.Brush.Color := RGBToColor(20, 20, 22);
  ACanvas.FillRect(VR);
  VU := Max(FVuL, FVuR);
  if csDesigning in ComponentState then VU := 0.45;
  ACanvas.Brush.Color := MixColor(RGBToColor(40, 180, 60), RGBToColor(220, 40, 40), VU);
  ACanvas.FillRect(VR.Left, VR.Top,
    VR.Left + Round((VR.Right - VR.Left) * EnsureRange(VU, 0, 1)), VR.Bottom);
end;

procedure TZForkChannelStrip.Paint;
begin
  if FKnobDirty or (FKnobCache.Width <> Width) or (FKnobCache.Height <> Height) then
    RebuildKnobCache;
  Canvas.Draw(0, 0, FKnobCache);
  DrawVU(Canvas);
end;

end.
