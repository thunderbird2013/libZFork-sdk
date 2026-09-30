{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  ZForkStoryAuto — Automationskurven fuer Storyboard-Clips

  Was dieses Unit kann:
  - Parameter: Volume, Bass/Mid/High, Echo, Reverb, SideCut
  - Kurvenpunkte (TimeMs, Wert 0..1), lineare Auswertung EvalAt
  - Farbgruppe TZForkStoryAutoColors fuer die Timeline-Darstellung
  - Wird von TZForkStoryboard genutzt (kein eigenes Palette-Control)
******************************************************************************}
unit ZForkStoryAuto;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Graphics, Math;

type
  TZForkAutoParam = (
    apVolume,
    apBass,
    apMid,
    apHigh,
    apEcho,
    apReverb,
    apSideCut
  );

  TZForkAutoPoint = record
    TimeMs: LongWord; { relativ zum Clip-Start (0..LengthMs) }
    Value: Single;    { 0..1 }
  end;

  TZForkAutoCurve = class(TPersistent)
  private
    FPoints: array of TZForkAutoPoint;
    FOnChange: TNotifyEvent;
    procedure Changed;
    function GetCount: Integer;
    function GetPoint(Index: Integer): TZForkAutoPoint;
  public
    procedure Assign(Source: TPersistent); override;
    procedure Clear;
    procedure Sort;
    function AddPoint(TimeMs: LongWord; Value: Single): Integer;
    procedure DeletePoint(Index: Integer);
    procedure SetPoint(Index: Integer; TimeMs: LongWord; Value: Single);
    function HitTest(TimeMs: LongWord; Value: Single; TolMs: LongWord;
      TolVal: Single): Integer;
    { Linear-Interpolation; ohne Punkte → DefaultValue }
    function EvalAt(TimeMs: LongWord; DefaultValue: Single): Single;
    property Count: Integer read GetCount;
    property Points[Index: Integer]: TZForkAutoPoint read GetPoint;
    property OnChange: TNotifyEvent read FOnChange write FOnChange;
  end;

  TZForkClipAutomation = class(TPersistent)
  private
    FCurves: array[TZForkAutoParam] of TZForkAutoCurve;
    FOnChange: TNotifyEvent;
    procedure CurveChanged(Sender: TObject);
  public
    constructor Create;
    destructor Destroy; override;
    procedure Assign(Source: TPersistent); override;
    function Curve(Param: TZForkAutoParam): TZForkAutoCurve;
    property OnChange: TNotifyEvent read FOnChange write FOnChange;
  end;

  TZForkStoryAutoColors = class(TPersistent)
  private
    FOwner: TPersistent;
    FNotify: TNotifyEvent;
    FVolume: TColor;
    FBass: TColor;
    FMid: TColor;
    FHigh: TColor;
    FEcho: TColor;
    FReverb: TColor;
    FSideCut: TColor;
    procedure SetColor(var Field: TColor; AValue: TColor);
    procedure SetVolume(AValue: TColor);
    procedure SetBass(AValue: TColor);
    procedure SetMid(AValue: TColor);
    procedure SetHigh(AValue: TColor);
    procedure SetEcho(AValue: TColor);
    procedure SetReverb(AValue: TColor);
    procedure SetSideCut(AValue: TColor);
  protected
    procedure Changed;
    function GetOwner: TPersistent; override;
  public
    constructor Create(AOwner: TPersistent; ANotify: TNotifyEvent);
    procedure Assign(Source: TPersistent); override;
    function ColorOf(Param: TZForkAutoParam): TColor;
  published
    property Volume: TColor read FVolume write SetVolume default clLime;
    property Bass: TColor read FBass write SetBass default clRed;
    property Mid: TColor read FMid write SetMid default clYellow;
    property High: TColor read FHigh write SetHigh default clAqua;
    property Echo: TColor read FEcho write SetEcho default clFuchsia;
    property Reverb: TColor read FReverb write SetReverb default clSkyBlue;
    property SideCut: TColor read FSideCut write SetSideCut default clOlive;
  end;

function AutoParamCaption(P: TZForkAutoParam): string;
function AutoParamDefault(P: TZForkAutoParam): Single;
function Clamp01(V: Single): Single;

implementation

function Clamp01(V: Single): Single;
begin
  if V < 0 then Result := 0
  else if V > 1 then Result := 1
  else Result := V;
end;

function AutoParamCaption(P: TZForkAutoParam): string;
begin
  case P of
    apVolume:  Result := 'Volume';
    apBass:    Result := 'Bass';
    apMid:     Result := 'Middle';
    apHigh:    Result := 'High';
    apEcho:    Result := 'Echo';
    apReverb:  Result := 'Reverb';
    apSideCut: Result := 'Side Cut';
  else
    Result := '?';
  end;
end;

function AutoParamDefault(P: TZForkAutoParam): Single;
begin
  case P of
    apVolume: Result := 1.0;
    apBass, apMid, apHigh: Result := 0.5; { Mitte = 0 dB }
  else
    Result := 0.0;
  end;
end;

{ TZForkAutoCurve }

procedure TZForkAutoCurve.Changed;
begin
  if Assigned(FOnChange) then FOnChange(Self);
end;

function TZForkAutoCurve.GetCount: Integer;
begin
  Result := Length(FPoints);
end;

function TZForkAutoCurve.GetPoint(Index: Integer): TZForkAutoPoint;
begin
  Result := FPoints[Index];
end;

procedure TZForkAutoCurve.Assign(Source: TPersistent);
var
  C: TZForkAutoCurve;
  I: Integer;
begin
  if Source is TZForkAutoCurve then
  begin
    C := TZForkAutoCurve(Source);
    SetLength(FPoints, Length(C.FPoints));
    for I := 0 to High(FPoints) do
      FPoints[I] := C.FPoints[I];
    Changed;
  end
  else
    inherited Assign(Source);
end;

procedure TZForkAutoCurve.Clear;
begin
  SetLength(FPoints, 0);
  Changed;
end;

procedure TZForkAutoCurve.Sort;
var
  I, J: Integer;
  Tmp: TZForkAutoPoint;
begin
  for I := 0 to High(FPoints) - 1 do
    for J := I + 1 to High(FPoints) do
      if FPoints[J].TimeMs < FPoints[I].TimeMs then
      begin
        Tmp := FPoints[I];
        FPoints[I] := FPoints[J];
        FPoints[J] := Tmp;
      end;
end;

function TZForkAutoCurve.AddPoint(TimeMs: LongWord; Value: Single): Integer;
begin
  Result := Length(FPoints);
  SetLength(FPoints, Result + 1);
  FPoints[Result].TimeMs := TimeMs;
  FPoints[Result].Value := Clamp01(Value);
  Sort;
  { Index nach Sort neu finden }
  Result := HitTest(TimeMs, Value, 2, 0.02);
  if Result < 0 then Result := 0;
  Changed;
end;

procedure TZForkAutoCurve.DeletePoint(Index: Integer);
var
  I: Integer;
begin
  if (Index < 0) or (Index >= Length(FPoints)) then Exit;
  for I := Index to High(FPoints) - 1 do
    FPoints[I] := FPoints[I + 1];
  SetLength(FPoints, Length(FPoints) - 1);
  Changed;
end;

procedure TZForkAutoCurve.SetPoint(Index: Integer; TimeMs: LongWord; Value: Single);
begin
  if (Index < 0) or (Index >= Length(FPoints)) then Exit;
  FPoints[Index].TimeMs := TimeMs;
  FPoints[Index].Value := Clamp01(Value);
  Sort;
  Changed;
end;

function TZForkAutoCurve.HitTest(TimeMs: LongWord; Value: Single; TolMs: LongWord;
  TolVal: Single): Integer;
var
  I: Integer;
  Best: Integer;
  BestScore, Score: Single;
  Dt: Int64;
begin
  Result := -1;
  Best := -1;
  BestScore := 1e30;
  for I := 0 to High(FPoints) do
  begin
    Dt := Abs(Int64(FPoints[I].TimeMs) - Int64(TimeMs));
    if Dt > TolMs then Continue;
    Score := Dt / Max(1.0, TolMs) + Abs(FPoints[I].Value - Value) / Max(0.001, TolVal);
    if Score < BestScore then
    begin
      BestScore := Score;
      Best := I;
    end;
  end;
  Result := Best;
end;

function TZForkAutoCurve.EvalAt(TimeMs: LongWord; DefaultValue: Single): Single;
var
  I: Integer;
  T0, T1: LongWord;
  V0, V1, U: Single;
begin
  if Length(FPoints) = 0 then Exit(DefaultValue);
  if Length(FPoints) = 1 then Exit(FPoints[0].Value);
  if TimeMs <= FPoints[0].TimeMs then Exit(FPoints[0].Value);
  if TimeMs >= FPoints[High(FPoints)].TimeMs then Exit(FPoints[High(FPoints)].Value);
  for I := 0 to High(FPoints) - 1 do
  begin
    if (TimeMs >= FPoints[I].TimeMs) and (TimeMs <= FPoints[I + 1].TimeMs) then
    begin
      T0 := FPoints[I].TimeMs;
      T1 := FPoints[I + 1].TimeMs;
      V0 := FPoints[I].Value;
      V1 := FPoints[I + 1].Value;
      if T1 <= T0 then Exit(V0);
      U := (TimeMs - T0) / Single(T1 - T0);
      Exit(V0 + (V1 - V0) * U);
    end;
  end;
  Result := DefaultValue;
end;

{ TZForkClipAutomation }

constructor TZForkClipAutomation.Create;
var
  P: TZForkAutoParam;
begin
  inherited Create;
  for P := Low(TZForkAutoParam) to High(TZForkAutoParam) do
  begin
    FCurves[P] := TZForkAutoCurve.Create;
    FCurves[P].OnChange := @CurveChanged;
  end;
end;

destructor TZForkClipAutomation.Destroy;
var
  P: TZForkAutoParam;
begin
  for P := Low(TZForkAutoParam) to High(TZForkAutoParam) do
    FreeAndNil(FCurves[P]);
  inherited Destroy;
end;

procedure TZForkClipAutomation.CurveChanged(Sender: TObject);
begin
  if Assigned(FOnChange) then FOnChange(Self);
end;

procedure TZForkClipAutomation.Assign(Source: TPersistent);
var
  A: TZForkClipAutomation;
  P: TZForkAutoParam;
begin
  if Source is TZForkClipAutomation then
  begin
    A := TZForkClipAutomation(Source);
    for P := Low(TZForkAutoParam) to High(TZForkAutoParam) do
      FCurves[P].Assign(A.FCurves[P]);
  end
  else
    inherited Assign(Source);
end;

function TZForkClipAutomation.Curve(Param: TZForkAutoParam): TZForkAutoCurve;
begin
  Result := FCurves[Param];
end;

{ TZForkStoryAutoColors }

constructor TZForkStoryAutoColors.Create(AOwner: TPersistent; ANotify: TNotifyEvent);
begin
  inherited Create;
  FOwner := AOwner;
  FNotify := ANotify;
  FVolume := clLime;
  FBass := clRed;
  FMid := clYellow;
  FHigh := clAqua;
  FEcho := clFuchsia;
  FReverb := clSkyBlue;
  FSideCut := clOlive;
end;

function TZForkStoryAutoColors.GetOwner: TPersistent;
begin
  Result := FOwner;
end;

procedure TZForkStoryAutoColors.Changed;
begin
  if Assigned(FNotify) then FNotify(Self);
end;

procedure TZForkStoryAutoColors.Assign(Source: TPersistent);
var
  C: TZForkStoryAutoColors;
begin
  if Source is TZForkStoryAutoColors then
  begin
    C := TZForkStoryAutoColors(Source);
    FVolume := C.FVolume;
    FBass := C.FBass;
    FMid := C.FMid;
    FHigh := C.FHigh;
    FEcho := C.FEcho;
    FReverb := C.FReverb;
    FSideCut := C.FSideCut;
    Changed;
  end
  else
    inherited Assign(Source);
end;

function TZForkStoryAutoColors.ColorOf(Param: TZForkAutoParam): TColor;
begin
  case Param of
    apVolume:  Result := FVolume;
    apBass:    Result := FBass;
    apMid:     Result := FMid;
    apHigh:    Result := FHigh;
    apEcho:    Result := FEcho;
    apReverb:  Result := FReverb;
    apSideCut: Result := FSideCut;
  else
    Result := clWhite;
  end;
end;

procedure TZForkStoryAutoColors.SetColor(var Field: TColor; AValue: TColor);
begin
  if Field = AValue then Exit;
  Field := AValue;
  Changed;
end;

procedure TZForkStoryAutoColors.SetVolume(AValue: TColor);
begin
  SetColor(FVolume, AValue);
end;

procedure TZForkStoryAutoColors.SetBass(AValue: TColor);
begin
  SetColor(FBass, AValue);
end;

procedure TZForkStoryAutoColors.SetMid(AValue: TColor);
begin
  SetColor(FMid, AValue);
end;

procedure TZForkStoryAutoColors.SetHigh(AValue: TColor);
begin
  SetColor(FHigh, AValue);
end;

procedure TZForkStoryAutoColors.SetEcho(AValue: TColor);
begin
  SetColor(FEcho, AValue);
end;

procedure TZForkStoryAutoColors.SetReverb(AValue: TColor);
begin
  SetColor(FReverb, AValue);
end;

procedure TZForkStoryAutoColors.SetSideCut(AValue: TColor);
begin
  SetColor(FSideCut, AValue);
end;

end.
