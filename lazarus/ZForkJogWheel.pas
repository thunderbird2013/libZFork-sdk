{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  TZForkJogWheel — Drehrad zum Seeken oder Nudgen

  Was die Komponente kann:
  - Modus jmSeek (Position springen) oder jmNudge (kurz vor/zurueck)
  - Sensitivity einstellbar; Nudge(Ms) programmierbar
  - Anbindung an TZForkPlayer; Maus-/Touch-Drehung
******************************************************************************}
unit ZForkJogWheel;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Math, Controls, Graphics, Forms, LCLType,
  ZForkPlayer, ZForkAbout, LibZFork;

type
  TZForkJogMode = (jmSeek, jmNudge);

  TZForkJogWheel = class(TCustomControl)
  private
    FPlayer: TZForkPlayer;
    FActive: Boolean;
    FMode: TZForkJogMode;
    FSensitivity: Single;
    FBackColor: TColor;
    FAccentColor: TColor;
    FAbout: string;
    FAngle: Single;
    FDragging: Boolean;
    FLastAng: Single;
    FOnChange: TNotifyEvent;
    procedure SetPlayer(AValue: TZForkPlayer);
    procedure SetActive(AValue: Boolean);
    procedure SetMode(AValue: TZForkJogMode);
    procedure SetSensitivity(AValue: Single);
    procedure SetBackColor(AValue: TColor);
    procedure SetAccentColor(AValue: TColor);
    function GetAbout: string;
    procedure SetAbout(const AValue: string);
    function AngleAt(X, Y: Integer): Single;
    procedure ApplyDelta(DeltaAng: Single);
  protected
    procedure Notification(AComponent: TComponent; Operation: TOperation); override;
    procedure Paint; override;
    procedure MouseDown(Button: TMouseButton; Shift: TShiftState; X, Y: Integer); override;
    procedure MouseMove(Shift: TShiftState; X, Y: Integer); override;
    procedure MouseUp(Button: TMouseButton; Shift: TShiftState; X, Y: Integer); override;
  public
    constructor Create(AOwner: TComponent); override;
    procedure ShowAbout;
    procedure Nudge(Ms: Integer);
  published
    property About: string read GetAbout write SetAbout stored False;
    property Align;
    property Anchors;
    property Constraints;
    property Enabled;
    property Visible;
    property OnMouseDown;
    property OnMouseMove;
    property OnMouseUp;

    property Player: TZForkPlayer read FPlayer write SetPlayer;
    property Active: Boolean read FActive write SetActive default True;
    property Mode: TZForkJogMode read FMode write SetMode default jmSeek;
    property Sensitivity: Single read FSensitivity write SetSensitivity;
    property BackColor: TColor read FBackColor write SetBackColor default clBlack;
    property AccentColor: TColor read FAccentColor write SetAccentColor default clAqua;
    property OnChange: TNotifyEvent read FOnChange write FOnChange;
  end;

implementation

constructor TZForkJogWheel.Create(AOwner: TComponent);
begin
  inherited Create(AOwner);
  ControlStyle := ControlStyle + [csOpaque, csCaptureMouse];
  Width := 120;
  Height := 120;
  FActive := True;
  FMode := jmSeek;
  FSensitivity := 1.0;
  FBackColor := RGBToColor(24, 24, 28);
  FAccentColor := clAqua;
  FAbout := '(About…)';
  FAngle := 0;
  Color := FBackColor;
  DoubleBuffered := True;
end;

procedure TZForkJogWheel.Notification(AComponent: TComponent; Operation: TOperation);
begin
  inherited Notification(AComponent, Operation);
  if (Operation = opRemove) and (AComponent = FPlayer) then FPlayer := nil;
end;

function TZForkJogWheel.GetAbout: string;
begin
  Result := FAbout;
end;

procedure TZForkJogWheel.SetAbout(const AValue: string);
begin
  ShowAbout;
end;

procedure TZForkJogWheel.ShowAbout;
begin
  ShowZForkAbout;
end;

procedure TZForkJogWheel.SetPlayer(AValue: TZForkPlayer);
begin
  if FPlayer = AValue then Exit;
  if FPlayer <> nil then FPlayer.RemoveFreeNotification(Self);
  FPlayer := AValue;
  if FPlayer <> nil then FPlayer.FreeNotification(Self);
end;

procedure TZForkJogWheel.SetActive(AValue: Boolean);
begin
  FActive := AValue;
end;

procedure TZForkJogWheel.SetMode(AValue: TZForkJogMode);
begin
  if FMode = AValue then Exit;
  FMode := AValue;
  Invalidate;
end;

procedure TZForkJogWheel.SetSensitivity(AValue: Single);
begin
  if AValue < 0.1 then AValue := 0.1;
  if AValue > 5 then AValue := 5;
  FSensitivity := AValue;
end;

procedure TZForkJogWheel.SetBackColor(AValue: TColor);
begin
  if FBackColor = AValue then Exit;
  FBackColor := AValue;
  Color := AValue;
  Invalidate;
end;

procedure TZForkJogWheel.SetAccentColor(AValue: TColor);
begin
  if FAccentColor = AValue then Exit;
  FAccentColor := AValue;
  Invalidate;
end;

function TZForkJogWheel.AngleAt(X, Y: Integer): Single;
var
  CX, CY: Integer;
begin
  CX := Width div 2;
  CY := Height div 2;
  Result := ArcTan2(Y - CY, X - CX);
end;

procedure TZForkJogWheel.Nudge(Ms: Integer);
var
  Pos: LongWord;
  NewPos: Int64;
begin
  if (not FActive) or (FPlayer = nil) then Exit;
  if not FPlayer.GetPositionMs(Pos) then Exit;
  NewPos := Int64(Pos) + Ms;
  if NewPos < 0 then NewPos := 0;
  FPlayer.SeekMs(ZFORK_SEEK_BEGIN, LongWord(NewPos));
end;

procedure TZForkJogWheel.ApplyDelta(DeltaAng: Single);
var
  Ms: Integer;
  Pos: LongWord;
  NewPos: Int64;
begin
  if (not FActive) or (FPlayer = nil) or (csDesigning in ComponentState) then Exit;
  { ~1 Umdrehung ≈ 2 s Seek bei Sensitivity 1 }
  Ms := Round(DeltaAng / (2 * Pi) * 2000 * FSensitivity);
  if Ms = 0 then Exit;
  if FMode = jmNudge then
  begin
    { kurzer Tempo-Kick }
    if Ms > 0 then FPlayer.SetTempo(1.0 + 0.02 * FSensitivity)
    else FPlayer.SetTempo(1.0 - 0.02 * FSensitivity);
  end
  else
  begin
    if not FPlayer.GetPositionMs(Pos) then Exit;
    NewPos := Int64(Pos) + Ms;
    if NewPos < 0 then NewPos := 0;
    FPlayer.SeekMs(ZFORK_SEEK_BEGIN, LongWord(NewPos));
  end;
  if Assigned(FOnChange) then FOnChange(Self);
end;

procedure TZForkJogWheel.Paint;
var
  CX, CY, R, R2: Integer;
  X2, Y2: Integer;
begin
  Canvas.Brush.Color := FBackColor;
  Canvas.FillRect(ClientRect);
  CX := Width div 2;
  CY := Height div 2;
  R := Min(CX, CY) - 4;
  R2 := R - 14;
  Canvas.Brush.Color := RGBToColor(40, 40, 48);
  Canvas.Pen.Color := FAccentColor;
  Canvas.Pen.Width := 2;
  Canvas.Ellipse(CX - R, CY - R, CX + R, CY + R);
  Canvas.Brush.Color := RGBToColor(28, 28, 32);
  Canvas.Ellipse(CX - R2, CY - R2, CX + R2, CY + R2);
  X2 := CX + Round(Cos(FAngle) * (R - 8));
  Y2 := CY + Round(Sin(FAngle) * (R - 8));
  Canvas.Pen.Color := FAccentColor;
  Canvas.Pen.Width := 3;
  Canvas.MoveTo(CX, CY);
  Canvas.LineTo(X2, Y2);
  Canvas.Font.Color := clSilver;
  Canvas.Font.Height := -10;
  Canvas.Brush.Style := bsClear;
  if FMode = jmSeek then
    Canvas.TextOut(4, Height - 14, 'Seek')
  else
    Canvas.TextOut(4, Height - 14, 'Nudge');
  Canvas.Brush.Style := bsSolid;
end;

procedure TZForkJogWheel.MouseDown(Button: TMouseButton; Shift: TShiftState; X, Y: Integer);
begin
  inherited MouseDown(Button, Shift, X, Y);
  if Button <> mbLeft then Exit;
  if ssCtrl in Shift then
  begin
    if FMode = jmSeek then SetMode(jmNudge) else SetMode(jmSeek);
    Exit;
  end;
  FDragging := True;
  FLastAng := AngleAt(X, Y);
end;

procedure TZForkJogWheel.MouseMove(Shift: TShiftState; X, Y: Integer);
var
  A, D: Single;
begin
  inherited MouseMove(Shift, X, Y);
  if not FDragging or not (ssLeft in Shift) then Exit;
  A := AngleAt(X, Y);
  D := A - FLastAng;
  if D > Pi then D := D - 2 * Pi;
  if D < -Pi then D := D + 2 * Pi;
  FAngle := A;
  FLastAng := A;
  ApplyDelta(D);
  Invalidate;
end;

procedure TZForkJogWheel.MouseUp(Button: TMouseButton; Shift: TShiftState; X, Y: Integer);
begin
  inherited MouseUp(Button, Shift, X, Y);
  FDragging := False;
  if (FMode = jmNudge) and (FPlayer <> nil) then
    FPlayer.SetTempo(1.0);
end;

end.
