{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  TZForkDeck — DJ-Deck: Transport, Cue, Pitch/Tempo, Wave-Strip

  Was die Komponente kann:
  - Play/Pause/Stop/Cue am gebundenen Player
  - Pitch- und Tempo-Fader; BPM-Erkennung
  - Bis zu 8 Hotcues setzen und anspringen
  - Integrierter Wave-Strip / Positionsanzeige (PosMs/LengthMs)
******************************************************************************}
unit ZForkDeck;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Math, Controls, Graphics, ExtCtrls, Forms, LCLType,
  ZForkPlayer, ZForkAbout, LibZFork;

const
  ZFORK_DECK_HOTCUES = 8;

type
  TZForkDeck = class(TCustomControl)
  private
    FPlayer: TZForkPlayer;
    FActive: Boolean;
    FCaption: string;
    FAbout: string;
    FBackColor: TColor;
    FAccentColor: TColor;
    FPitchSemitones: Single;
    FTempo: Single;
    FBpm: Single;
    FCueMs: LongWord;
    FHotcues: array[0..ZFORK_DECK_HOTCUES - 1] of LongWord;
    FPosMs: LongWord;
    FLenMs: LongWord;
    FPlaying: Boolean;
    FTimer: TTimer;
    FDragPitch: Boolean;
    FDragTempo: Boolean;
    FOnChange: TNotifyEvent;
    procedure SetPlayer(AValue: TZForkPlayer);
    procedure SetActive(AValue: Boolean);
    procedure SetCaption(const AValue: string);
    procedure SetBackColor(AValue: TColor);
    procedure SetAccentColor(AValue: TColor);
    procedure SetPitchSemitones(AValue: Single);
    procedure SetTempo(AValue: Single);
    function GetAbout: string;
    procedure SetAbout(const AValue: string);
    procedure TimerTick(Sender: TObject);
    procedure UpdateTimer;
    procedure Changed;
    procedure ApplyPitchTempo;
    procedure RefreshStatus;
    function TransportRect: TRect;
    function StripRect: TRect;
    function PitchRect: TRect;
    function TempoRect: TRect;
    function HotcueRect(Index: Integer): TRect;
    function HitHotcue(X, Y: Integer): Integer;
    function HitTransport(X, Y: Integer): Integer; { 0 play 1 pause 2 stop 3 cue }
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
    procedure Play;
    procedure Pause;
    procedure Stop;
    procedure CueJump;
    procedure CueSet;
    procedure SetHotcue(Index: Integer);
    procedure JumpHotcue(Index: Integer);
    procedure ClearHotcues;
    procedure DetectBpm;
    property PosMs: LongWord read FPosMs;
    property LengthMs: LongWord read FLenMs;
    property Bpm: Single read FBpm write FBpm;
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
    property OnMouseDown;
    property OnMouseMove;
    property OnMouseUp;

    property Player: TZForkPlayer read FPlayer write SetPlayer;
    property Active: Boolean read FActive write SetActive default True;
    property Caption: string read FCaption write SetCaption;
    property BackColor: TColor read FBackColor write SetBackColor default clBlack;
    property AccentColor: TColor read FAccentColor write SetAccentColor default clAqua;
    property PitchSemitones: Single read FPitchSemitones write SetPitchSemitones;
    property Tempo: Single read FTempo write SetTempo;
    property OnChange: TNotifyEvent read FOnChange write FOnChange;
  end;

implementation

function MkRect(L, T, R, B: Integer): TRect;
begin
  Result.Left := L; Result.Top := T; Result.Right := R; Result.Bottom := B;
end;

constructor TZForkDeck.Create(AOwner: TComponent);
var
  I: Integer;
begin
  inherited Create(AOwner);
  ControlStyle := ControlStyle + [csOpaque, csCaptureMouse];
  Width := 280;
  Height := 160;
  FActive := True;
  FCaption := 'Deck';
  FAbout := '(About…)';
  FBackColor := RGBToColor(24, 24, 28);
  FAccentColor := clAqua;
  FPitchSemitones := 0;
  FTempo := 1.0;
  FBpm := 0;
  FCueMs := 0;
  for I := 0 to High(FHotcues) do FHotcues[I] := High(LongWord);
  FTimer := TTimer.Create(Self);
  FTimer.Interval := 50;
  FTimer.OnTimer := @TimerTick;
  FTimer.Enabled := False;
  Color := FBackColor;
  DoubleBuffered := True;
end;

destructor TZForkDeck.Destroy;
begin
  FTimer.Enabled := False;
  inherited Destroy;
end;

procedure TZForkDeck.Loaded;
begin
  inherited Loaded;
  UpdateTimer;
end;

procedure TZForkDeck.Notification(AComponent: TComponent; Operation: TOperation);
begin
  inherited Notification(AComponent, Operation);
  if (Operation = opRemove) and (AComponent = FPlayer) then FPlayer := nil;
end;

function TZForkDeck.GetAbout: string;
begin
  Result := FAbout;
end;

procedure TZForkDeck.SetAbout(const AValue: string);
begin
  ShowAbout;
end;

procedure TZForkDeck.ShowAbout;
begin
  ShowZForkAbout;
end;

procedure TZForkDeck.Changed;
begin
  if Assigned(FOnChange) then FOnChange(Self);
end;

procedure TZForkDeck.SetPlayer(AValue: TZForkPlayer);
begin
  if FPlayer = AValue then Exit;
  if FPlayer <> nil then FPlayer.RemoveFreeNotification(Self);
  FPlayer := AValue;
  if FPlayer <> nil then FPlayer.FreeNotification(Self);
  ApplyPitchTempo;
  UpdateTimer;
  Invalidate;
end;

procedure TZForkDeck.SetActive(AValue: Boolean);
begin
  FActive := AValue;
  UpdateTimer;
end;

procedure TZForkDeck.SetCaption(const AValue: string);
begin
  if FCaption = AValue then Exit;
  FCaption := AValue;
  Invalidate;
end;

procedure TZForkDeck.SetBackColor(AValue: TColor);
begin
  if FBackColor = AValue then Exit;
  FBackColor := AValue;
  Color := AValue;
  Invalidate;
end;

procedure TZForkDeck.SetAccentColor(AValue: TColor);
begin
  if FAccentColor = AValue then Exit;
  FAccentColor := AValue;
  Invalidate;
end;

procedure TZForkDeck.SetPitchSemitones(AValue: Single);
begin
  if AValue < -12 then AValue := -12;
  if AValue > 12 then AValue := 12;
  if Abs(FPitchSemitones - AValue) < 0.01 then Exit;
  FPitchSemitones := AValue;
  ApplyPitchTempo;
  Invalidate;
  Changed;
end;

procedure TZForkDeck.SetTempo(AValue: Single);
begin
  if AValue < 0.5 then AValue := 0.5;
  if AValue > 2.0 then AValue := 2.0;
  if Abs(FTempo - AValue) < 0.001 then Exit;
  FTempo := AValue;
  ApplyPitchTempo;
  Invalidate;
  Changed;
end;

procedure TZForkDeck.ApplyPitchTempo;
begin
  if (FPlayer = nil) or (csDesigning in ComponentState) then Exit;
  FPlayer.SetPitch(FPitchSemitones);
  FPlayer.SetTempo(FTempo);
end;

procedure TZForkDeck.UpdateTimer;
begin
  FTimer.Enabled := FActive and (FPlayer <> nil) and not (csDesigning in ComponentState)
    and not (csLoading in ComponentState);
end;

procedure TZForkDeck.RefreshStatus;
var
  St: TStreamStatus;
  Info: TStreamInfo;
  Pos: LongWord;
begin
  if FPlayer = nil then Exit;
  if FPlayer.GetStatus(St) then
    FPlaying := St.playing;
  if FPlayer.GetPositionMs(Pos) then
    FPosMs := Pos;
  if FPlayer.GetStreamInfo(-1, Info) then
    FLenMs := Info.length.sec * 1000 + Info.length.ms;
end;

procedure TZForkDeck.TimerTick(Sender: TObject);
begin
  RefreshStatus;
  Invalidate;
end;

procedure TZForkDeck.Play;
begin
  if FPlayer <> nil then FPlayer.Play;
end;

procedure TZForkDeck.Pause;
begin
  if FPlayer <> nil then FPlayer.Pause;
end;

procedure TZForkDeck.Stop;
begin
  if FPlayer <> nil then FPlayer.Stop;
end;

procedure TZForkDeck.CueSet;
begin
  RefreshStatus;
  FCueMs := FPosMs;
  Invalidate;
  Changed;
end;

procedure TZForkDeck.CueJump;
begin
  if FPlayer = nil then Exit;
  FPlayer.SeekMs(ZFORK_SEEK_BEGIN, FCueMs);
  if not FPlaying then FPlayer.Pause;
end;

procedure TZForkDeck.SetHotcue(Index: Integer);
begin
  if (Index < 0) or (Index >= ZFORK_DECK_HOTCUES) then Exit;
  RefreshStatus;
  FHotcues[Index] := FPosMs;
  Invalidate;
  Changed;
end;

procedure TZForkDeck.JumpHotcue(Index: Integer);
begin
  if (Index < 0) or (Index >= ZFORK_DECK_HOTCUES) then Exit;
  if FHotcues[Index] = High(LongWord) then
  begin
    SetHotcue(Index);
    Exit;
  end;
  if FPlayer <> nil then
    FPlayer.SeekMs(ZFORK_SEEK_BEGIN, FHotcues[Index]);
end;

procedure TZForkDeck.ClearHotcues;
var
  I: Integer;
begin
  for I := 0 to High(FHotcues) do FHotcues[I] := High(LongWord);
  Invalidate;
end;

procedure TZForkDeck.DetectBpm;
begin
  if FPlayer = nil then Exit;
  FBpm := FPlayer.DetectBPM;
  Invalidate;
  Changed;
end;

function TZForkDeck.TransportRect: TRect;
begin
  Result := MkRect(8, 28, Width - 8, 52);
end;

function TZForkDeck.StripRect: TRect;
begin
  Result := MkRect(8, 58, Width - 8, 78);
end;

function TZForkDeck.PitchRect: TRect;
begin
  Result := MkRect(8, 110, Width div 2 - 4, Height - 8);
end;

function TZForkDeck.TempoRect: TRect;
begin
  Result := MkRect(Width div 2 + 4, 110, Width - 8, Height - 8);
end;

function TZForkDeck.HotcueRect(Index: Integer): TRect;
var
  W, X: Integer;
begin
  W := (Width - 16) div ZFORK_DECK_HOTCUES;
  X := 8 + Index * W;
  Result := MkRect(X, 84, X + W - 2, 104);
end;

function TZForkDeck.HitHotcue(X, Y: Integer): Integer;
var
  I: Integer;
  R: TRect;
begin
  Result := -1;
  for I := 0 to ZFORK_DECK_HOTCUES - 1 do
  begin
    R := HotcueRect(I);
    if (X >= R.Left) and (X < R.Right) and (Y >= R.Top) and (Y < R.Bottom) then
      Exit(I);
  end;
end;

function TZForkDeck.HitTransport(X, Y: Integer): Integer;
var
  R: TRect;
  W, Slot: Integer;
begin
  Result := -1;
  R := TransportRect;
  if (Y < R.Top) or (Y >= R.Bottom) or (X < R.Left) or (X >= R.Right) then Exit;
  W := (R.Right - R.Left) div 4;
  Slot := (X - R.Left) div Max(1, W);
  if (Slot >= 0) and (Slot <= 3) then Result := Slot;
end;

procedure TZForkDeck.Paint;
var
  R, SR: TRect;
  I, X, W: Integer;
  Labels: array[0..3] of string = ('Play', 'Pause', 'Stop', 'Cue');
  Frac: Single;
  PitchN, TempoN: Single;
begin
  Canvas.Brush.Color := FBackColor;
  Canvas.FillRect(ClientRect);
  Canvas.Font.Color := clWhite;
  Canvas.Font.Style := [fsBold];
  Canvas.Font.Height := -12;
  Canvas.Brush.Style := bsClear;
  Canvas.TextOut(8, 6, FCaption);
  if FBpm > 40 then
    Canvas.TextOut(Width - 70, 6, Format('%.0f BPM', [FBpm]));
  Canvas.Font.Style := [];

  R := TransportRect;
  W := (R.Right - R.Left) div 4;
  for I := 0 to 3 do
  begin
    SR := MkRect(R.Left + I * W, R.Top, R.Left + (I + 1) * W - 2, R.Bottom);
    Canvas.Brush.Color := RGBToColor(40, 40, 48);
    Canvas.FillRect(SR);
    Canvas.Font.Color := FAccentColor;
    Canvas.Brush.Style := bsClear;
    Canvas.TextOut(SR.Left + 6, SR.Top + 4, Labels[I]);
    Canvas.Brush.Style := bsSolid;
  end;

  { Wave / Position strip }
  R := StripRect;
  Canvas.Brush.Color := RGBToColor(16, 16, 20);
  Canvas.FillRect(R);
  if FLenMs > 0 then
  begin
    Frac := FPosMs / Single(FLenMs);
    X := R.Left + Round(Frac * (R.Right - R.Left));
    Canvas.Brush.Color := FAccentColor;
    Canvas.FillRect(MkRect(R.Left, R.Top, Max(R.Left + 2, X), R.Bottom));
    if FCueMs > 0 then
    begin
      X := R.Left + Round((FCueMs / Single(FLenMs)) * (R.Right - R.Left));
      Canvas.Pen.Color := clYellow;
      Canvas.MoveTo(X, R.Top);
      Canvas.LineTo(X, R.Bottom);
    end;
  end;
  Canvas.Font.Color := clSilver;
  Canvas.Font.Height := -10;
  Canvas.Brush.Style := bsClear;
  Canvas.TextOut(R.Left + 4, R.Top + 2,
    Format('%d:%02d / %d:%02d', [
      Integer(FPosMs div 60000), Integer((FPosMs div 1000) mod 60),
      Integer(FLenMs div 60000), Integer((FLenMs div 1000) mod 60)]));
  Canvas.Brush.Style := bsSolid;

  for I := 0 to ZFORK_DECK_HOTCUES - 1 do
  begin
    SR := HotcueRect(I);
    if FHotcues[I] <> High(LongWord) then
      Canvas.Brush.Color := FAccentColor
    else
      Canvas.Brush.Color := RGBToColor(36, 36, 42);
    Canvas.FillRect(SR);
    Canvas.Font.Color := clWhite;
    Canvas.Brush.Style := bsClear;
    Canvas.TextOut(SR.Left + 4, SR.Top + 2, IntToStr(I + 1));
    Canvas.Brush.Style := bsSolid;
  end;

  { Pitch / Tempo faders }
  PitchN := (FPitchSemitones + 12) / 24.0;
  TempoN := (FTempo - 0.5) / 1.5;
  R := PitchRect;
  Canvas.Brush.Color := RGBToColor(36, 36, 42);
  Canvas.FillRect(R);
  Canvas.Brush.Color := FAccentColor;
  Canvas.FillRect(MkRect(R.Left, R.Bottom - Round(PitchN * (R.Bottom - R.Top)), R.Right, R.Bottom));
  Canvas.Font.Color := clWhite;
  Canvas.Brush.Style := bsClear;
  Canvas.TextOut(R.Left + 4, R.Top + 2, Format('Pitch %.1f', [FPitchSemitones]));
  Canvas.Brush.Style := bsSolid;

  R := TempoRect;
  Canvas.Brush.Color := RGBToColor(36, 36, 42);
  Canvas.FillRect(R);
  Canvas.Brush.Color := clLime;
  Canvas.FillRect(MkRect(R.Left, R.Bottom - Round(TempoN * (R.Bottom - R.Top)), R.Right, R.Bottom));
  Canvas.Font.Color := clWhite;
  Canvas.Brush.Style := bsClear;
  Canvas.TextOut(R.Left + 4, R.Top + 2, Format('Tempo %.0f%%', [FTempo * 100]));
  Canvas.Brush.Style := bsSolid;
end;

procedure TZForkDeck.MouseDown(Button: TMouseButton; Shift: TShiftState; X, Y: Integer);
var
  T, H: Integer;
  R: TRect;
  Norm: Single;
begin
  inherited MouseDown(Button, Shift, X, Y);
  if Button <> mbLeft then Exit;
  T := HitTransport(X, Y);
  case T of
    0: Play;
    1: Pause;
    2: Stop;
    3:
      if ssShift in Shift then CueSet else CueJump;
  end;
  H := HitHotcue(X, Y);
  if H >= 0 then
  begin
    if ssShift in Shift then SetHotcue(H) else JumpHotcue(H);
    Exit;
  end;
  R := PitchRect;
  if (X >= R.Left) and (X < R.Right) and (Y >= R.Top) and (Y < R.Bottom) then
  begin
    FDragPitch := True;
    Norm := 1.0 - (Y - R.Top) / Max(1, R.Bottom - R.Top);
    SetPitchSemitones(Norm * 24 - 12);
    Exit;
  end;
  R := TempoRect;
  if (X >= R.Left) and (X < R.Right) and (Y >= R.Top) and (Y < R.Bottom) then
  begin
    FDragTempo := True;
    Norm := 1.0 - (Y - R.Top) / Max(1, R.Bottom - R.Top);
    SetTempo(0.5 + Norm * 1.5);
  end;
end;

procedure TZForkDeck.MouseMove(Shift: TShiftState; X, Y: Integer);
var
  R: TRect;
  Norm: Single;
begin
  inherited MouseMove(Shift, X, Y);
  if not (ssLeft in Shift) then Exit;
  if FDragPitch then
  begin
    R := PitchRect;
    Norm := EnsureRange(1.0 - (Y - R.Top) / Max(1.0, R.Bottom - R.Top), 0, 1);
    SetPitchSemitones(Norm * 24 - 12);
  end
  else if FDragTempo then
  begin
    R := TempoRect;
    Norm := EnsureRange(1.0 - (Y - R.Top) / Max(1.0, R.Bottom - R.Top), 0, 1);
    SetTempo(0.5 + Norm * 1.5);
  end;
end;

procedure TZForkDeck.MouseUp(Button: TMouseButton; Shift: TShiftState; X, Y: Integer);
begin
  inherited MouseUp(Button, Shift, X, Y);
  FDragPitch := False;
  FDragTempo := False;
end;

end.
