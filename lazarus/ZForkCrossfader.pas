{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  TZForkCrossfader — A/B-Mix (DJ und Radio Auto-Fade)

  Was die Komponente kann:
  - Position 0.0 = nur A … 1.0 = nur B; Kurven Linear oder ConstPower
  - Setzt PlayerA/PlayerB-Volumen live (Master oder Player-Vol)
  - FadeTo / FadeToA / FadeToB / FadeToCenter mit AutoFadeMs
  - Events OnChange und OnFadeDone
******************************************************************************}
unit ZForkCrossfader;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Math, Types, Controls, Graphics, ExtCtrls, Forms, LCLType,
  ZForkPlayer, ZForkAbout;

type
  TZForkCrossfadeCurve = (
    cfLinear,
    cfConstPower
  );

  TZForkCrossfader = class(TCustomControl)
  private
    FPlayerA: TZForkPlayer;
    FPlayerB: TZForkPlayer;
    FActive: Boolean;
    FPosition: Single;       { 0 = nur A, 1 = nur B }
    FCurve: TZForkCrossfadeCurve;
    FAutoFadeMs: Integer;
    FBackColor: TColor;
    FTrackColor: TColor;
    FThumbColor: TColor;
    FLabelColor: TColor;
    FAbout: string;
    FDragging: Boolean;
    FFadeTimer: TTimer;
    FFadeFrom: Single;
    FFadeTo: Single;
    FFadeStartTick: QWord;
    FFadeDuration: Integer;
    FFading: Boolean;
    FOnChange: TNotifyEvent;
    FOnFadeDone: TNotifyEvent;
    procedure SetPlayerA(AValue: TZForkPlayer);
    procedure SetPlayerB(AValue: TZForkPlayer);
    procedure SetActive(AValue: Boolean);
    procedure SetPosition(AValue: Single);
    procedure SetCurve(AValue: TZForkCrossfadeCurve);
    procedure SetAutoFadeMs(AValue: Integer);
    procedure SetBackColor(AValue: TColor);
    procedure SetTrackColor(AValue: TColor);
    procedure SetThumbColor(AValue: TColor);
    function GetAbout: string;
    procedure SetAbout(const AValue: string);
    procedure ApplyVolumes;
    procedure CalcVolumes(T: Single; out VolA, VolB: Single);
    procedure ApplyVolumeToPlayer(Player: TZForkPlayer; Vol: Single);
    function PosFromX(X: Integer): Single;
    function XFromPos(T: Single): Integer;
    procedure FadeTick(Sender: TObject);
    procedure StopFade;
  protected
    procedure Notification(AComponent: TComponent; Operation: TOperation); override;
    procedure Paint; override;
    procedure MouseDown(Button: TMouseButton; Shift: TShiftState; X, Y: Integer); override;
    procedure MouseMove(Shift: TShiftState; X, Y: Integer); override;
    procedure MouseUp(Button: TMouseButton; Shift: TShiftState; X, Y: Integer); override;
    function DoMouseWheel(Shift: TShiftState; WheelDelta: Integer;
      MousePos: TPoint): Boolean; override;
    procedure Loaded; override;
  public
    constructor Create(AOwner: TComponent); override;
    destructor Destroy; override;
    procedure ShowAbout;
    { Sofort oder per AutoFadeMs (wenn DurationMs < 0). }
    procedure FadeTo(Target: Single; DurationMs: Integer = -1);
    procedure FadeToA(DurationMs: Integer = -1);
    procedure FadeToB(DurationMs: Integer = -1);
    procedure FadeToCenter(DurationMs: Integer = -1);
    property Fading: Boolean read FFading;
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

    property PlayerA: TZForkPlayer read FPlayerA write SetPlayerA;
    property PlayerB: TZForkPlayer read FPlayerB write SetPlayerB;
    property Active: Boolean read FActive write SetActive default True;
    { 0.0 = nur A, 1.0 = nur B }
    property Position: Single read FPosition write SetPosition;
    property Curve: TZForkCrossfadeCurve read FCurve write SetCurve default cfConstPower;
    property AutoFadeMs: Integer read FAutoFadeMs write SetAutoFadeMs default 4000;
    property BackColor: TColor read FBackColor write SetBackColor default clBlack;
    property TrackColor: TColor read FTrackColor write SetTrackColor default clGray;
    property ThumbColor: TColor read FThumbColor write SetThumbColor default clWhite;
    property OnChange: TNotifyEvent read FOnChange write FOnChange;
    property OnFadeDone: TNotifyEvent read FOnFadeDone write FOnFadeDone;
  end;

implementation

{ TZForkCrossfader }

constructor TZForkCrossfader.Create(AOwner: TComponent);
begin
  inherited Create(AOwner);
  ControlStyle := ControlStyle + [csOpaque, csCaptureMouse];
  Width := 280;
  Height := 36;
  FActive := True;
  FPosition := 0.0;
  FCurve := cfConstPower;
  FAutoFadeMs := 4000;
  FBackColor := clBlack;
  FTrackColor := $606060;
  FThumbColor := clWhite;
  FLabelColor := clSilver;
  FAbout := '(About…)';
  FFadeTimer := TTimer.Create(Self);
  FFadeTimer.Enabled := False;
  FFadeTimer.Interval := 16;
  FFadeTimer.OnTimer := @FadeTick;
  Color := FBackColor;
end;

destructor TZForkCrossfader.Destroy;
begin
  FFadeTimer.Enabled := False;
  if FPlayerA <> nil then
  begin
    FPlayerA.RemoveFreeNotification(Self);
    FPlayerA := nil;
  end;
  if FPlayerB <> nil then
  begin
    FPlayerB.RemoveFreeNotification(Self);
    FPlayerB := nil;
  end;
  inherited Destroy;
end;

function TZForkCrossfader.GetAbout: string;
begin
  Result := FAbout;
end;

procedure TZForkCrossfader.SetAbout(const AValue: string);
begin
  ShowAbout;
end;

procedure TZForkCrossfader.ShowAbout;
begin
  ShowZForkAbout;
end;

procedure TZForkCrossfader.Loaded;
begin
  inherited Loaded;
  if FActive and not (csDesigning in ComponentState) then
    ApplyVolumes;
end;

procedure TZForkCrossfader.Notification(AComponent: TComponent; Operation: TOperation);
begin
  inherited Notification(AComponent, Operation);
  if Operation = opRemove then
  begin
    if AComponent = FPlayerA then FPlayerA := nil;
    if AComponent = FPlayerB then FPlayerB := nil;
  end;
end;

procedure TZForkCrossfader.SetPlayerA(AValue: TZForkPlayer);
begin
  if FPlayerA = AValue then Exit;
  if FPlayerA <> nil then FPlayerA.RemoveFreeNotification(Self);
  FPlayerA := AValue;
  if FPlayerA <> nil then FPlayerA.FreeNotification(Self);
  if FActive then ApplyVolumes;
end;

procedure TZForkCrossfader.SetPlayerB(AValue: TZForkPlayer);
begin
  if FPlayerB = AValue then Exit;
  if FPlayerB <> nil then FPlayerB.RemoveFreeNotification(Self);
  FPlayerB := AValue;
  if FPlayerB <> nil then FPlayerB.FreeNotification(Self);
  if FActive then ApplyVolumes;
end;

procedure TZForkCrossfader.SetActive(AValue: Boolean);
begin
  if FActive = AValue then Exit;
  FActive := AValue;
  if FActive then ApplyVolumes;
  Invalidate;
end;

procedure TZForkCrossfader.SetPosition(AValue: Single);
begin
  AValue := EnsureRange(AValue, 0.0, 1.0);
  if Abs(FPosition - AValue) < 0.0001 then Exit;
  if FFading and not FDragging then
    StopFade;
  FPosition := AValue;
  if FActive then ApplyVolumes;
  Invalidate;
  if Assigned(FOnChange) then FOnChange(Self);
end;

procedure TZForkCrossfader.SetCurve(AValue: TZForkCrossfadeCurve);
begin
  if FCurve = AValue then Exit;
  FCurve := AValue;
  if FActive then ApplyVolumes;
  Invalidate;
end;

procedure TZForkCrossfader.SetAutoFadeMs(AValue: Integer);
begin
  if AValue < 50 then AValue := 50;
  if AValue > 120000 then AValue := 120000;
  FAutoFadeMs := AValue;
end;

procedure TZForkCrossfader.SetBackColor(AValue: TColor);
begin
  if FBackColor = AValue then Exit;
  FBackColor := AValue;
  Color := AValue;
  Invalidate;
end;

procedure TZForkCrossfader.SetTrackColor(AValue: TColor);
begin
  if FTrackColor = AValue then Exit;
  FTrackColor := AValue;
  Invalidate;
end;

procedure TZForkCrossfader.SetThumbColor(AValue: TColor);
begin
  if FThumbColor = AValue then Exit;
  FThumbColor := AValue;
  Invalidate;
end;

procedure TZForkCrossfader.CalcVolumes(T: Single; out VolA, VolB: Single);
begin
  T := EnsureRange(T, 0.0, 1.0);
  case FCurve of
    cfConstPower:
      begin
        VolA := Cos(T * Pi * 0.5);
        VolB := Sin(T * Pi * 0.5);
      end;
  else
    begin
      VolA := 1.0 - T;
      VolB := T;
    end;
  end;
end;

procedure TZForkCrossfader.ApplyVolumeToPlayer(Player: TZForkPlayer; Vol: Single);
var
  V: Integer;
begin
  if Player = nil then Exit;
  if Player.Handle = nil then Exit;
  V := Round(EnsureRange(Vol, 0.0, 1.0) * 100.0);
  Player.SetPlayerVolume(V, V);
end;

procedure TZForkCrossfader.ApplyVolumes;
var
  VA, VB: Single;
begin
  if csDesigning in ComponentState then Exit;
  if not FActive then Exit;
  CalcVolumes(FPosition, VA, VB);
  ApplyVolumeToPlayer(FPlayerA, VA);
  ApplyVolumeToPlayer(FPlayerB, VB);
end;

procedure TZForkCrossfader.StopFade;
begin
  FFading := False;
  FFadeTimer.Enabled := False;
end;

procedure TZForkCrossfader.FadeTo(Target: Single; DurationMs: Integer);
begin
  Target := EnsureRange(Target, 0.0, 1.0);
  if DurationMs < 0 then DurationMs := FAutoFadeMs;
  if DurationMs <= 0 then
  begin
    StopFade;
    SetPosition(Target);
    if Assigned(FOnFadeDone) then FOnFadeDone(Self);
    Exit;
  end;
  FFadeFrom := FPosition;
  FFadeTo := Target;
  FFadeDuration := DurationMs;
  FFadeStartTick := GetTickCount64;
  FFading := True;
  FFadeTimer.Enabled := True;
end;

procedure TZForkCrossfader.FadeToA(DurationMs: Integer);
begin
  FadeTo(0.0, DurationMs);
end;

procedure TZForkCrossfader.FadeToB(DurationMs: Integer);
begin
  FadeTo(1.0, DurationMs);
end;

procedure TZForkCrossfader.FadeToCenter(DurationMs: Integer);
begin
  FadeTo(0.5, DurationMs);
end;

procedure TZForkCrossfader.FadeTick(Sender: TObject);
var
  Elapsed: QWord;
  U, T: Single;
begin
  if not FFading then
  begin
    FFadeTimer.Enabled := False;
    Exit;
  end;
  Elapsed := GetTickCount64 - FFadeStartTick;
  if Elapsed >= QWord(FFadeDuration) then
  begin
    StopFade;
    FPosition := FFadeTo;
    if FActive then ApplyVolumes;
    Invalidate;
    if Assigned(FOnChange) then FOnChange(Self);
    if Assigned(FOnFadeDone) then FOnFadeDone(Self);
    Exit;
  end;
  U := Elapsed / FFadeDuration;
  { smoothstep }
  U := U * U * (3.0 - 2.0 * U);
  T := FFadeFrom + (FFadeTo - FFadeFrom) * U;
  FPosition := EnsureRange(T, 0.0, 1.0);
  if FActive then ApplyVolumes;
  Invalidate;
  if Assigned(FOnChange) then FOnChange(Self);
end;

function TZForkCrossfader.PosFromX(X: Integer): Single;
var
  M, W: Integer;
begin
  M := 8;
  W := Max(1, Width - 2 * M);
  Result := EnsureRange((X - M) / W, 0.0, 1.0);
end;

function TZForkCrossfader.XFromPos(T: Single): Integer;
var
  M, W: Integer;
begin
  M := 8;
  W := Max(1, Width - 2 * M);
  Result := M + Round(EnsureRange(T, 0.0, 1.0) * W);
end;

procedure TZForkCrossfader.MouseDown(Button: TMouseButton; Shift: TShiftState; X, Y: Integer);
begin
  inherited MouseDown(Button, Shift, X, Y);
  if Button <> mbLeft then Exit;
  if not Enabled then Exit;
  StopFade;
  FDragging := True;
  MouseCapture := True;
  SetPosition(PosFromX(X));
end;

procedure TZForkCrossfader.MouseMove(Shift: TShiftState; X, Y: Integer);
begin
  inherited MouseMove(Shift, X, Y);
  if FDragging then
    SetPosition(PosFromX(X));
end;

procedure TZForkCrossfader.MouseUp(Button: TMouseButton; Shift: TShiftState; X, Y: Integer);
begin
  inherited MouseUp(Button, Shift, X, Y);
  if FDragging then
  begin
    FDragging := False;
    MouseCapture := False;
    SetPosition(PosFromX(X));
  end;
end;

function TZForkCrossfader.DoMouseWheel(Shift: TShiftState; WheelDelta: Integer;
  MousePos: TPoint): Boolean;
var
  Step: Single;
begin
  if not Enabled then
  begin
    Result := inherited DoMouseWheel(Shift, WheelDelta, MousePos);
    Exit;
  end;
  if ssCtrl in Shift then Step := 0.02 else Step := 0.05;
  if WheelDelta > 0 then
    SetPosition(FPosition - Step)
  else
    SetPosition(FPosition + Step);
  Result := True;
end;

procedure TZForkCrossfader.Paint;
var
  R, Track: TRect;
  CX, ThumbW, MidY: Integer;
  VA, VB: Single;
begin
  R := ClientRect;
  Canvas.Brush.Color := FBackColor;
  Canvas.FillRect(R);

  MidY := Height div 2;
  Track := Rect(8, MidY - 3, Width - 8, MidY + 3);
  Canvas.Brush.Color := FTrackColor;
  Canvas.Pen.Style := psClear;
  Canvas.FillRect(Track);

  { A/B Fills }
  CalcVolumes(FPosition, VA, VB);
  CX := XFromPos(FPosition);
  Canvas.Brush.Color := RGBToColor(40, 160, 255);
  Canvas.FillRect(Track.Left, Track.Top, CX, Track.Bottom);
  Canvas.Brush.Color := RGBToColor(255, 120, 40);
  Canvas.FillRect(CX, Track.Top, Track.Right, Track.Bottom);

  ThumbW := 10;
  Canvas.Brush.Color := FThumbColor;
  Canvas.FillRect(CX - ThumbW div 2, MidY - 10, CX - ThumbW div 2 + ThumbW, MidY + 10);

  Canvas.Font.Color := FLabelColor;
  Canvas.Font.Size := 7;
  Canvas.Brush.Style := bsClear;
  Canvas.TextOut(8, 2, Format('A %.0f%%', [VA * 100]));
  Canvas.TextOut(Width - 52, 2, Format('B %.0f%%', [VB * 100]));
  if FFading then
    Canvas.TextOut(Width div 2 - 16, Height - 12, 'FADE');
  Canvas.Brush.Style := bsSolid;
  Canvas.Pen.Style := psSolid;
end;

end.
