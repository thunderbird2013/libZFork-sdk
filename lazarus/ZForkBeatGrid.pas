{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  TZForkBeatGrid — BPM-Anzeige und Tempo-Sync A/B

  Was die Komponente kann:
  - BPM von Player A und/oder B anzeigen; DetectNow
  - SyncAtoB / SyncBtoA (Tempo angleichen, MaxTempoPct-Limit)
  - Optional AutoDetect beim Oeffnen neuer Tracks
******************************************************************************}
unit ZForkBeatGrid;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Math, Controls, Graphics, ExtCtrls, Forms, LCLType,
  ZForkPlayer, ZForkAbout;

type
  TZForkBeatGrid = class(TCustomControl)
  private
    FPlayerA: TZForkPlayer;
    FPlayerB: TZForkPlayer;
    FActive: Boolean;
    FBpmA: Single;
    FBpmB: Single;
    FMaxTempoPct: Integer;
    FAutoDetect: Boolean;
    FBackColor: TColor;
    FAccentColor: TColor;
    FAbout: string;
    FTimer: TTimer;
    FOnChange: TNotifyEvent;
    procedure SetPlayerA(AValue: TZForkPlayer);
    procedure SetPlayerB(AValue: TZForkPlayer);
    procedure SetActive(AValue: Boolean);
    procedure SetMaxTempoPct(AValue: Integer);
    procedure SetAutoDetect(AValue: Boolean);
    procedure SetBackColor(AValue: TColor);
    procedure SetAccentColor(AValue: TColor);
    function GetAbout: string;
    procedure SetAbout(const AValue: string);
    procedure TimerTick(Sender: TObject);
    procedure UpdateTimer;
    procedure Changed;
    function SyncButtonRect(Which: Integer): TRect; { 0 detect 1 A→B 2 B→A }
    function HitButton(X, Y: Integer): Integer;
    function ClampTempo(Tempo: Single): Single;
  protected
    procedure Notification(AComponent: TComponent; Operation: TOperation); override;
    procedure Paint; override;
    procedure MouseDown(Button: TMouseButton; Shift: TShiftState; X, Y: Integer); override;
    procedure Loaded; override;
  public
    constructor Create(AOwner: TComponent); override;
    destructor Destroy; override;
    procedure ShowAbout;
    procedure DetectNow;
    procedure SyncAtoB;
    procedure SyncBtoA;
    property BpmA: Single read FBpmA write FBpmA;
    property BpmB: Single read FBpmB write FBpmB;
  published
    property About: string read GetAbout write SetAbout stored False;
    property Align;
    property Anchors;
    property Constraints;
    property Enabled;
    property Visible;
    property OnClick;
    property OnMouseDown;

    property PlayerA: TZForkPlayer read FPlayerA write SetPlayerA;
    property PlayerB: TZForkPlayer read FPlayerB write SetPlayerB;
    property Active: Boolean read FActive write SetActive default True;
    property MaxTempoPct: Integer read FMaxTempoPct write SetMaxTempoPct default 8;
    property AutoDetect: Boolean read FAutoDetect write SetAutoDetect default False;
    property BackColor: TColor read FBackColor write SetBackColor default clBlack;
    property AccentColor: TColor read FAccentColor write SetAccentColor default clYellow;
    property OnChange: TNotifyEvent read FOnChange write FOnChange;
  end;

implementation

function MkRect(L, T, R, B: Integer): TRect;
begin
  Result.Left := L; Result.Top := T; Result.Right := R; Result.Bottom := B;
end;

constructor TZForkBeatGrid.Create(AOwner: TComponent);
begin
  inherited Create(AOwner);
  ControlStyle := ControlStyle + [csOpaque];
  Width := 260;
  Height := 72;
  FActive := True;
  FMaxTempoPct := 8;
  FAutoDetect := False;
  FBackColor := RGBToColor(24, 24, 28);
  FAccentColor := clYellow;
  FAbout := '(About…)';
  FTimer := TTimer.Create(Self);
  FTimer.Enabled := False;
  FTimer.Interval := 2000;
  FTimer.OnTimer := @TimerTick;
  Color := FBackColor;
  DoubleBuffered := True;
end;

destructor TZForkBeatGrid.Destroy;
begin
  FTimer.Enabled := False;
  inherited Destroy;
end;

procedure TZForkBeatGrid.Loaded;
begin
  inherited Loaded;
  UpdateTimer;
end;

procedure TZForkBeatGrid.Notification(AComponent: TComponent; Operation: TOperation);
begin
  inherited Notification(AComponent, Operation);
  if Operation = opRemove then
  begin
    if AComponent = FPlayerA then FPlayerA := nil;
    if AComponent = FPlayerB then FPlayerB := nil;
  end;
end;

function TZForkBeatGrid.GetAbout: string;
begin
  Result := FAbout;
end;

procedure TZForkBeatGrid.SetAbout(const AValue: string);
begin
  ShowAbout;
end;

procedure TZForkBeatGrid.ShowAbout;
begin
  ShowZForkAbout;
end;

procedure TZForkBeatGrid.Changed;
begin
  if Assigned(FOnChange) then FOnChange(Self);
end;

procedure TZForkBeatGrid.SetPlayerA(AValue: TZForkPlayer);
begin
  if FPlayerA = AValue then Exit;
  if FPlayerA <> nil then FPlayerA.RemoveFreeNotification(Self);
  FPlayerA := AValue;
  if FPlayerA <> nil then FPlayerA.FreeNotification(Self);
end;

procedure TZForkBeatGrid.SetPlayerB(AValue: TZForkPlayer);
begin
  if FPlayerB = AValue then Exit;
  if FPlayerB <> nil then FPlayerB.RemoveFreeNotification(Self);
  FPlayerB := AValue;
  if FPlayerB <> nil then FPlayerB.FreeNotification(Self);
end;

procedure TZForkBeatGrid.SetActive(AValue: Boolean);
begin
  FActive := AValue;
  UpdateTimer;
end;

procedure TZForkBeatGrid.SetMaxTempoPct(AValue: Integer);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 20 then AValue := 20;
  FMaxTempoPct := AValue;
end;

procedure TZForkBeatGrid.SetAutoDetect(AValue: Boolean);
begin
  FAutoDetect := AValue;
  UpdateTimer;
end;

procedure TZForkBeatGrid.SetBackColor(AValue: TColor);
begin
  if FBackColor = AValue then Exit;
  FBackColor := AValue;
  Color := AValue;
  Invalidate;
end;

procedure TZForkBeatGrid.SetAccentColor(AValue: TColor);
begin
  if FAccentColor = AValue then Exit;
  FAccentColor := AValue;
  Invalidate;
end;

procedure TZForkBeatGrid.UpdateTimer;
begin
  FTimer.Enabled := FActive and FAutoDetect and not (csDesigning in ComponentState)
    and not (csLoading in ComponentState);
end;

procedure TZForkBeatGrid.TimerTick(Sender: TObject);
begin
  DetectNow;
end;

function TZForkBeatGrid.ClampTempo(Tempo: Single): Single;
var
  Lo, Hi: Single;
begin
  Lo := 1.0 - FMaxTempoPct / 100.0;
  Hi := 1.0 + FMaxTempoPct / 100.0;
  if Tempo < Lo then Tempo := Lo;
  if Tempo > Hi then Tempo := Hi;
  Result := Tempo;
end;

procedure TZForkBeatGrid.DetectNow;
begin
  if csDesigning in ComponentState then Exit;
  if FPlayerA <> nil then
  begin
    FBpmA := FPlayerA.DetectBPM;
  end;
  if FPlayerB <> nil then
  begin
    FBpmB := FPlayerB.DetectBPM;
  end;
  Invalidate;
  Changed;
end;

procedure TZForkBeatGrid.SyncAtoB;
var
  Tempo: Single;
begin
  if (FPlayerB = nil) or (FBpmA < 40) or (FBpmB < 40) then Exit;
  Tempo := FBpmA / FBpmB;
  if Abs(Tempo - 2.0) < Abs(Tempo - 1.0) then Tempo := Tempo / 2.0;
  if Abs(Tempo - 0.5) < Abs(Tempo - 1.0) then Tempo := Tempo * 2.0;
  FPlayerB.SetTempo(ClampTempo(Tempo));
  Changed;
end;

procedure TZForkBeatGrid.SyncBtoA;
var
  Tempo: Single;
begin
  if (FPlayerA = nil) or (FBpmA < 40) or (FBpmB < 40) then Exit;
  Tempo := FBpmB / FBpmA;
  if Abs(Tempo - 2.0) < Abs(Tempo - 1.0) then Tempo := Tempo / 2.0;
  if Abs(Tempo - 0.5) < Abs(Tempo - 1.0) then Tempo := Tempo * 2.0;
  FPlayerA.SetTempo(ClampTempo(Tempo));
  Changed;
end;

function TZForkBeatGrid.SyncButtonRect(Which: Integer): TRect;
var
  W: Integer;
begin
  W := (Width - 16) div 3;
  Result := MkRect(8 + Which * (W + 2), Height - 28, 8 + (Which + 1) * (W + 2) - 2, Height - 6);
end;

function TZForkBeatGrid.HitButton(X, Y: Integer): Integer;
var
  I: Integer;
  R: TRect;
begin
  Result := -1;
  for I := 0 to 2 do
  begin
    R := SyncButtonRect(I);
    if (X >= R.Left) and (X < R.Right) and (Y >= R.Top) and (Y < R.Bottom) then
      Exit(I);
  end;
end;

procedure TZForkBeatGrid.Paint;
var
  R: TRect;
  Labels: array[0..2] of string = ('Detect', 'A → B', 'B → A');
  I: Integer;
begin
  Canvas.Brush.Color := FBackColor;
  Canvas.FillRect(ClientRect);
  Canvas.Font.Color := clWhite;
  Canvas.Font.Height := -12;
  Canvas.Brush.Style := bsClear;
  Canvas.TextOut(8, 8, Format('A: %.1f BPM', [FBpmA]));
  Canvas.TextOut(Width div 2, 8, Format('B: %.1f BPM', [FBpmB]));
  Canvas.Brush.Style := bsSolid;
  for I := 0 to 2 do
  begin
    R := SyncButtonRect(I);
    Canvas.Brush.Color := RGBToColor(40, 40, 48);
    Canvas.FillRect(R);
    Canvas.Font.Color := FAccentColor;
    Canvas.Brush.Style := bsClear;
    Canvas.TextOut(R.Left + 6, R.Top + 4, Labels[I]);
    Canvas.Brush.Style := bsSolid;
  end;
end;

procedure TZForkBeatGrid.MouseDown(Button: TMouseButton; Shift: TShiftState; X, Y: Integer);
begin
  inherited MouseDown(Button, Shift, X, Y);
  if Button <> mbLeft then Exit;
  case HitButton(X, Y) of
    0: DetectNow;
    1: SyncAtoB;
    2: SyncBtoA;
  end;
end;

end.
