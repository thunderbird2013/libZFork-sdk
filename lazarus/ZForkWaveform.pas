{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  TZForkWaveform — DJ-Waveform (Kick/Mid/High) fuer Deck A/B

  Was die Komponente kann:
  - Stacked-, Overlay- oder Single-Ansicht; Farben pro Deck/Band
  - Async-Datei-Analyse (Peaks/Waveform); eigener Tick-Thread (60/90 FPS)
  - Playhead/Needle, Beat-Strip, Zoom, Qualitaet, Latency/SyncOffset
  - ColorBoost, Strg+Mausrad-Zoom; Bindung an einen oder zwei Player
  - FastDraw / Smooth-Position fuer ruckelfreie Nadel
******************************************************************************}
unit ZForkWaveform;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Math, Types, Controls, Graphics, ExtCtrls, Forms, LCLType, SyncObjs,
  ZForkPlayer, ZForkIntf, LibZFork;

const
  ZFORK_WAVE_MAX_BINS = 131072;     { Time-Bins — genug fuer Zoom-Detail }
  ZFORK_WAVE_MAX_RAW  = ZFORK_WAVE_MAX_BINS * 3; { Kick/Mid/High interleaved }

type
  TZForkWaveStyle = (
    wsStacked,
    wsOverlay,
    wsSingle
  );

  TZForkWaveDetail = (
    wdSmooth,
    wdPeaks
  );

  { Zoom-Presets (Object Inspector Combo) }
  TZForkWaveQuality = (
    wqNormal,   { ZoomMs ~ 8000 }
    wqHigh,     { ZoomMs ~ 5000 }
    wqUltra     { ZoomMs ~ 2000 — max. Detail (VDJ-nah) }
  );

  { FPS-Presets }
  TZForkWaveFPS = (
    wfps60,
    wfps90
  );

  TZForkWaveform = class;

  TZForkWaveDeckColors = class(TPersistent)
  private
    FOwner: TPersistent;
    FWaveform: TZForkWaveform;
    FKick: TColor;
    FMid: TColor;
    FHigh: TColor;
    FAlpha: Integer;
    procedure SetKick(AValue: TColor);
    procedure SetMid(AValue: TColor);
    procedure SetHigh(AValue: TColor);
    procedure SetAlpha(AValue: Integer);
  protected
    function GetOwner: TPersistent; override;
    procedure Changed;
  public
    constructor Create(AOwner: TPersistent; AWaveform: TZForkWaveform);
    procedure Assign(Source: TPersistent); override;
  published
    property Kick: TColor read FKick write SetKick;   { Bass / Kicks }
    property Mid: TColor read FMid write SetMid;       { Mitten }
    property High: TColor read FHigh write SetHigh;    { Hoehen }
    property Alpha: Integer read FAlpha write SetAlpha default 220;
      { 0 = voll transparent, 255 = deckend — Kick/Mid/High }
  end;

  { Animations-Takt: eigener Thread statt TTimer/WM_TIMER — WM_TIMER ist eine
    Low-Priority-Message mit ~15ms Mindestgranularität und wird von Windows unter
    Last verzögert/zusammengefasst, was bei 90 FPS sichtbares Ruckeln erzeugt. }
  TZForkWaveTickThread = class(TThread)
  private
    FOwner: TZForkWaveform;
    procedure DoTick;
  protected
    procedure Execute; override;
  public
    constructor Create(AOwner: TZForkWaveform);
  end;

  { Hintergrund-Analyse: eigener DLL-Handle → blockiert UI/Playback nicht }
  TZForkWaveAnalyzeThread = class(TThread)
  private
    FOwner: TZForkWaveform;
    FDeck: Integer;
    FGen: Integer;
    FPath: string;
    FRaw: array of Byte;
    FCount: LongWord;
    FLengthMs: LongWord;
    FOk: Boolean;
  protected
    procedure Execute; override;
    procedure ApplyResult;
  public
    constructor Create(AOwner: TZForkWaveform; ADeck: Integer; AGen: Integer; const APath: string);
  end;

  TZForkWaveform = class(TCustomControl, IZForkPlayerClient)
  private
    FPlayerA: TZForkPlayer;
    FPlayerB: TZForkPlayer;
    FActive: Boolean;
    FStyle: TZForkWaveStyle;
    FDetail: TZForkWaveDetail;
    FQuality: TZForkWaveQuality;
    FFpsPreset: TZForkWaveFPS;
    FColorsA: TZForkWaveDeckColors;
    FColorsB: TZForkWaveDeckColors;
    FBackColor: TColor;
    FNeedleColor: TColor;
    FNeedleAlpha: Integer;
    FNeedleWidth: Integer;
    FZoomMs: Integer;
    FShowMirror: Boolean;
    FShowGrid: Boolean;
    FShowBeatStrip: Boolean;
    FGridColor: TColor;
    FGridAlpha: Integer;
    FBeatMarkerColor: TColor;
    FBeatMarkerAlpha: Integer;
    FAutoAnalyze: Boolean;
    FTargetFPS: Integer;
    FLatencyMs: Integer;
    FSyncOffsetMs: Integer;
    FFastDraw: Boolean;
    FSoftEdges: Boolean;
    FColorBoost: Integer;
    FWheelZoom: Boolean;
    FAbout: string;
    FBuffer: TBitmap;
    FBandKA, FBandMA, FBandHA: array of Byte;
    FBandKB, FBandMB, FBandHB: array of Byte;
    FBeatsA, FBeatsB: TSingleDynArray;
    FLengthA: LongWord;
    FLengthB: LongWord;
    FPosRawA, FPosRawB: LongWord;
    FPosTickA, FPosTickB: QWord;
    FDispA, FDispB: Double;
    FDispTick: QWord;
    FLastRawA, FLastRawB: LongWord;
    FPlayingA, FPlayingB: Boolean;
    FPaintBusy: Boolean;
    FLastPaintDur: Integer;
    FFileA: string;
    FFileB: string;
    FAnalyzingA, FAnalyzingB: Boolean;
    FGenA, FGenB: Integer;
    FTick: Integer;
    FCS: TCriticalSection;
    FTickThread: TZForkWaveTickThread;
    FShuttingDown: Boolean;
    procedure SetPlayerA(AValue: TZForkPlayer);
    procedure SetPlayerB(AValue: TZForkPlayer);
    procedure SetActive(AValue: Boolean);
    procedure SetStyle(AValue: TZForkWaveStyle);
    procedure SetDetail(AValue: TZForkWaveDetail);
    procedure SetQuality(AValue: TZForkWaveQuality);
    procedure SetFpsPreset(AValue: TZForkWaveFPS);
    procedure SetColorsA(AValue: TZForkWaveDeckColors);
    procedure SetColorsB(AValue: TZForkWaveDeckColors);
    procedure SetBackColor(AValue: TColor);
    procedure SetNeedleColor(AValue: TColor);
    procedure SetNeedleAlpha(AValue: Integer);
    procedure SetNeedleWidth(AValue: Integer);
    procedure SetZoomMs(AValue: Integer);
    procedure SetShowMirror(AValue: Boolean);
    procedure SetShowGrid(AValue: Boolean);
    procedure SetShowBeatStrip(AValue: Boolean);
    procedure SetGridColor(AValue: TColor);
    procedure SetGridAlpha(AValue: Integer);
    procedure SetBeatMarkerColor(AValue: TColor);
    procedure SetBeatMarkerAlpha(AValue: Integer);
    procedure SetAutoAnalyze(AValue: Boolean);
    procedure SetTargetFPS(AValue: Integer);
    procedure SetLatencyMs(AValue: Integer);
    procedure SetSyncOffsetMs(AValue: Integer);
    procedure SetFastDraw(AValue: Boolean);
    procedure SetSoftEdges(AValue: Boolean);
    procedure SetColorBoost(AValue: Integer);
    procedure SetWheelZoom(AValue: Boolean);
    function GetAbout: string;
    procedure SetAbout(const AValue: string);
    procedure EnsureClientsRegistered;
    procedure UpdateTimer;
    procedure TimerTick(Sender: TObject);
    function IsAnyPlaying: Boolean;
    procedure EnsureBuffer;
    function GetFrameGapMs: Integer;
    procedure ApplyQualityZoom;
    procedure ApplyFpsPreset;
    procedure SamplePositions;
    procedure AdvanceDisplay;
    function DisplayPosA: LongWord;
    function DisplayPosB: LongWord;
    procedure StartAnalyzeAsync(Deck: Integer);
    procedure ApplyAnalyzeResult(Deck: Integer; const Raw: array of Byte; Count, LenMs: LongWord; const Path: string);
    procedure ClearDeck(Deck: Integer);
    function ResolveLengthMs(Player: TZForkPlayer; AnalyzedMs: LongWord): LongWord;
    function MaxRangeBand(const Bins: array of Byte; T0, T1: Single): Single;
    function MixRGB(C1, C2: TColor; T: Single): TColor;
    function BlendPixel(Bg, Fg: TColor; Alpha: Integer): TColor;
    procedure BoostRGB(var R, G, B: Integer);
    procedure BuildBeatsFromKick(const Kick: array of Byte; LengthMs: LongWord; var Beats: TSingleDynArray);
    procedure PaintLane(ACanvas: TCanvas; const ARect: TRect;
      const Kick, Mid, High: array of Byte; const Beats: array of Single;
      LengthMs, PosMs: LongWord; Colors: TZForkWaveDeckColors);
    procedure PaintNeedle(ACanvas: TCanvas; const ARect: TRect);
    procedure PaintDesignTime(ACanvas: TCanvas; const ARect: TRect);
  protected
    procedure Notification(AComponent: TComponent; Operation: TOperation); override;
    procedure Paint; override;
    procedure EraseBackground(DC: HDC); override;
    procedure Loaded; override;
    function DoMouseWheel(Shift: TShiftState; WheelDelta: Integer;
      MousePos: TPoint): Boolean; override;
  public
    constructor Create(AOwner: TComponent); override;
    destructor Destroy; override;
    procedure NotifyPlayerOpen(const AFileName: string);
    procedure NotifyPlayerSong(SongIndex: Integer);
    procedure NotifyPlayerClose;
    procedure NotifyPlayerStop;
    procedure ShowAbout;
    procedure RefreshWaveforms;
  published
    property About: string read GetAbout write SetAbout stored False;
    property Align;
    property Anchors;
    property BorderSpacing;
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
    property Style: TZForkWaveStyle read FStyle write SetStyle default wsStacked;
    property Detail: TZForkWaveDetail read FDetail write SetDetail default wdPeaks;
    property Quality: TZForkWaveQuality read FQuality write SetQuality default wqUltra;
    property FpsPreset: TZForkWaveFPS read FFpsPreset write SetFpsPreset default wfps90;
    property ColorsA: TZForkWaveDeckColors read FColorsA write SetColorsA;
    property ColorsB: TZForkWaveDeckColors read FColorsB write SetColorsB;
    property BackColor: TColor read FBackColor write SetBackColor default clBlack;
    property NeedleColor: TColor read FNeedleColor write SetNeedleColor default clWhite;
    property NeedleAlpha: Integer read FNeedleAlpha write SetNeedleAlpha default 240;
    property NeedleWidth: Integer read FNeedleWidth write SetNeedleWidth default 2;
    property ZoomMs: Integer read FZoomMs write SetZoomMs default 2000;
    property ShowMirror: Boolean read FShowMirror write SetShowMirror default True;
    property ShowGrid: Boolean read FShowGrid write SetShowGrid default True;
    property ShowBeatStrip: Boolean read FShowBeatStrip write SetShowBeatStrip default True;
    property GridColor: TColor read FGridColor write SetGridColor default clSilver;
    property GridAlpha: Integer read FGridAlpha write SetGridAlpha default 120;
    property BeatMarkerColor: TColor read FBeatMarkerColor write SetBeatMarkerColor default clRed;
    property BeatMarkerAlpha: Integer read FBeatMarkerAlpha write SetBeatMarkerAlpha default 220;
    property AutoAnalyze: Boolean read FAutoAnalyze write SetAutoAnalyze default True;
    { Decoder oft etwas voraus → vom Display abziehen (ms). }
    property LatencyMs: Integer read FLatencyMs write SetLatencyMs default 450;
    { Feintuning (+ = Wave voraus, − = Wave spaeter). }
    property SyncOffsetMs: Integer read FSyncOffsetMs write SetSyncOffsetMs default 0;
    property TargetFPS: Integer read FTargetFPS write SetTargetFPS default 90;
    { 100 = normal, 150–200 = kraeftig/VDJ-nah, max 300 }
    property ColorBoost: Integer read FColorBoost write SetColorBoost default 180;
      { 0 = transparent/aus, 100 = normal, 300 = max. Farbsättigung }
    property FastDraw: Boolean read FFastDraw write SetFastDraw default True;
    { Weiche Peak-Kanten (leichtes Pseudo-AA). Echtes GDI+ bringt bei 1px-Spalten kaum was. }
    property SoftEdges: Boolean read FSoftEdges write SetSoftEdges default True;
    { Mausrad: Musik-Zoom nur mit Strg+Rad (ZoomMs). }
    property WheelZoom: Boolean read FWheelZoom write SetWheelZoom default True;
  end;

implementation

uses
  ZForkAbout;

{ TZForkWaveDeckColors }

constructor TZForkWaveDeckColors.Create(AOwner: TPersistent; AWaveform: TZForkWaveform);
begin
  inherited Create;
  FOwner := AOwner;
  FWaveform := AWaveform;
  FKick := $4040FF;   { rot/orange Kick }
  FMid := $40E080;    { gruen Mid }
  FHigh := $FFE080;   { cyan/gelb High }
  FAlpha := 220;
end;

function TZForkWaveDeckColors.GetOwner: TPersistent;
begin
  Result := FOwner;
end;

procedure TZForkWaveDeckColors.Changed;
begin
  if FWaveform <> nil then
    FWaveform.Invalidate;
end;

procedure TZForkWaveDeckColors.Assign(Source: TPersistent);
begin
  if Source is TZForkWaveDeckColors then
  begin
    FKick := TZForkWaveDeckColors(Source).Kick;
    FMid := TZForkWaveDeckColors(Source).Mid;
    FHigh := TZForkWaveDeckColors(Source).High;
    FAlpha := TZForkWaveDeckColors(Source).Alpha;
    Changed;
  end
  else
    inherited Assign(Source);
end;

procedure TZForkWaveDeckColors.SetKick(AValue: TColor);
begin
  if FKick = AValue then Exit;
  FKick := AValue;
  Changed;
end;

procedure TZForkWaveDeckColors.SetMid(AValue: TColor);
begin
  if FMid = AValue then Exit;
  FMid := AValue;
  Changed;
end;

procedure TZForkWaveDeckColors.SetHigh(AValue: TColor);
begin
  if FHigh = AValue then Exit;
  FHigh := AValue;
  Changed;
end;

procedure TZForkWaveDeckColors.SetAlpha(AValue: Integer);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 255 then AValue := 255;
  if FAlpha = AValue then Exit;
  FAlpha := AValue;
  Changed;
end;

{ TZForkWaveTickThread }

constructor TZForkWaveTickThread.Create(AOwner: TZForkWaveform);
begin
  FOwner := AOwner;
  FreeOnTerminate := False;
  inherited Create(False);
end;

procedure TZForkWaveTickThread.DoTick;
begin
  if (FOwner = nil) or FOwner.FShuttingDown then Exit;
  FOwner.TimerTick(nil);
end;

procedure TZForkWaveTickThread.Execute;
const
  { Kein Deck spielt → 5 Hz reicht, um Play/Pause zeitnah zu erkennen und den
    Nadel-/Positionsstand aktuell zu halten, spart aber ~90% CPU im Leerlauf
    gegenueber Dauerbetrieb mit voller Ziel-FPS. }
  IdleGapMs = 200;
var
  NextT, NowT: QWord;
  Gap: QWord;
begin
  NextT := GetTickCount64;
  while not Terminated do
  begin
    if (FOwner = nil) or FOwner.FShuttingDown then Break;
    if FOwner.IsAnyPlaying then
      Gap := QWord(FOwner.GetFrameGapMs)
    else
      Gap := IdleGapMs;
    NextT := NextT + Gap;
    NowT := GetTickCount64;
    if NextT > NowT then
      Sleep(Cardinal(NextT - NowT))
    else
      NextT := NowT; { deutlich hinter dem Ziel: nicht versuchen aufzuholen }
    if Terminated then Break;
    Synchronize(@DoTick);
  end;
end;

{ TZForkWaveAnalyzeThread }

constructor TZForkWaveAnalyzeThread.Create(AOwner: TZForkWaveform; ADeck: Integer; AGen: Integer; const APath: string);
begin
  inherited Create(True);
  FreeOnTerminate := True;
  FOwner := AOwner;
  FDeck := ADeck;
  FGen := AGen;
  FPath := APath;
  FOk := False;
  FCount := 0;
  FLengthMs := 0;
end;

procedure TZForkWaveAnalyzeThread.Execute;
var
  H: TZFORK_HANDLE;
  Analysis: TZFORK_ANALYSIS_HANDLE;
  Bpm: Single;
  Flags: LongWord;
begin
  H := zfork_Create;
  if H = nil then Exit;
  try
    SetLength(FRaw, ZFORK_WAVE_MAX_RAW);
    FillChar(FRaw[0], Length(FRaw), 0);
    { Nur Waveform+Length — kein BPM/Peaks (spart 1–4 s und haengt die UI nicht) }
    if zfork_AnalyzeFile(H, PWideChar(UnicodeString(FPath)),
         ZFORK_ANALYZE_WAVEFORM or ZFORK_ANALYZE_LENGTH,
         @Analysis) = 0 then
      Exit;
    try
      zfork_GetAnalysisSummary(H, Analysis, @Bpm, @FLengthMs, @Flags);
      FCount := zfork_CopyAnalysisWaveform(H, Analysis, @FRaw[0], LongWord(Length(FRaw)));
      FOk := FCount >= 2;
    finally
      zfork_FreeAnalysis(H, Analysis);
    end;
  finally
    zfork_Destroy(H);
  end;
  if not Terminated and (FOwner <> nil) then
    Synchronize(@ApplyResult);
end;

procedure TZForkWaveAnalyzeThread.ApplyResult;
begin
  if FOwner = nil then Exit;
  { Veraltete Analyse verwerfen (neuer Song / Refresh) }
  if FDeck = 0 then
  begin
    if FGen <> FOwner.FGenA then Exit;
    if FOk then
      FOwner.ApplyAnalyzeResult(FDeck, FRaw, FCount, FLengthMs, FPath)
    else
      FOwner.FAnalyzingA := False;
  end
  else
  begin
    if FGen <> FOwner.FGenB then Exit;
    if FOk then
      FOwner.ApplyAnalyzeResult(FDeck, FRaw, FCount, FLengthMs, FPath)
    else
      FOwner.FAnalyzingB := False;
  end;
end;

{ TZForkWaveform }

constructor TZForkWaveform.Create(AOwner: TComponent);
begin
  inherited Create(AOwner);
  ControlStyle := ControlStyle + [csOpaque, csCaptureMouse, csDoubleClicks];
  DoubleBuffered := True;
  Width := 480;
  Height := 140;
  FAbout := '(About…)';
  FActive := True;
  FStyle := wsStacked;
  FDetail := wdPeaks;
  FQuality := wqUltra;
  FFpsPreset := wfps90;
  FBackColor := clBlack;
  FNeedleColor := clWhite;
  FNeedleAlpha := 240;
  FNeedleWidth := 2;
  FZoomMs := 2000;
  FShowMirror := True;
  FShowGrid := True;
  FShowBeatStrip := True;
  FGridColor := clSilver;
  FGridAlpha := 120;
  FBeatMarkerColor := clRed;
  FBeatMarkerAlpha := 220;
  FAutoAnalyze := True;
  FTargetFPS := 90;
  { Decoder-Pos liegt typ. ~Buffer voraus (Default ~500 ms) }
  FLatencyMs := 450;
  FSyncOffsetMs := 0;
  FGenA := 0;
  FGenB := 0;
  FFastDraw := True;
  FSoftEdges := True;
  FColorBoost := 180;
  FWheelZoom := True;
  FDispA := 0;
  FDispB := 0;
  FDispTick := 0;
  FLastRawA := High(LongWord);
  FLastRawB := High(LongWord);
  FPaintBusy := False;
  FLastPaintDur := 0;
  FCS := TCriticalSection.Create;
  FColorsA := TZForkWaveDeckColors.Create(Self, Self);
  { TColor = BGR ($00BBGGRR)! Rot = $0000FF = clRed, NICHT $FF0000 }
  FColorsA.Kick := clRed;       { echtes Rot }
  FColorsA.Mid  := $C000E0;     { Magenta/Pink-Mitte }
  FColorsA.High := $FFFF40;     { Cyan/Hell }
  FColorsA.Alpha := 220;
  FColorsB := TZForkWaveDeckColors.Create(Self, Self);
  FColorsB.Kick := clBlue;      { echtes Blau $FF0000 }
  FColorsB.Mid  := $E000A0;     { Violett }
  FColorsB.High := clLime;      { Gruen }
  FColorsB.Alpha := 220;
  FShuttingDown := False;
  FTickThread := nil;
  Color := FBackColor;
end;

destructor TZForkWaveform.Destroy;
begin
  FShuttingDown := True;
  if FTickThread <> nil then
  begin
    FTickThread.Terminate;
    FTickThread.WaitFor;
    FreeAndNil(FTickThread);
  end;
  if FPlayerA <> nil then
  begin
    FPlayerA.UnregisterClient(Self);
    FPlayerA := nil;
  end;
  if FPlayerB <> nil then
  begin
    FPlayerB.UnregisterClient(Self);
    FPlayerB := nil;
  end;
  FreeAndNil(FBuffer);
  FreeAndNil(FColorsA);
  FreeAndNil(FColorsB);
  FreeAndNil(FCS);
  inherited Destroy;
end;

function TZForkWaveform.GetAbout: string;
begin
  Result := '(About…)';
end;

procedure TZForkWaveform.SetAbout(const AValue: string);
begin
end;

procedure TZForkWaveform.ShowAbout;
begin
  ShowZForkAbout;
end;

procedure TZForkWaveform.EnsureClientsRegistered;
begin
  if csDesigning in ComponentState then Exit;
  if FPlayerA <> nil then FPlayerA.RegisterClient(Self);
  if FPlayerB <> nil then FPlayerB.RegisterClient(Self);
end;

procedure TZForkWaveform.Loaded;
begin
  inherited Loaded;
  EnsureClientsRegistered;
  ApplyQualityZoom;
  ApplyFpsPreset;
  UpdateTimer;
  if not (csDesigning in ComponentState) then
    RefreshWaveforms;
end;

procedure TZForkWaveform.Notification(AComponent: TComponent; Operation: TOperation);
begin
  inherited Notification(AComponent, Operation);
  if Operation = opRemove then
  begin
    if AComponent = FPlayerA then begin FPlayerA := nil; ClearDeck(0); end;
    if AComponent = FPlayerB then begin FPlayerB := nil; ClearDeck(1); end;
  end;
end;

procedure TZForkWaveform.SetPlayerA(AValue: TZForkPlayer);
begin
  if FPlayerA = AValue then begin EnsureClientsRegistered; Exit; end;
  if FPlayerA <> nil then FPlayerA.UnregisterClient(Self);
  FPlayerA := AValue;
  if FPlayerA <> nil then
  begin
    FPlayerA.FreeNotification(Self);
    if not (csLoading in ComponentState) then FPlayerA.RegisterClient(Self);
  end;
  if not (csLoading in ComponentState) then StartAnalyzeAsync(0);
  Invalidate;
end;

procedure TZForkWaveform.SetPlayerB(AValue: TZForkPlayer);
begin
  if FPlayerB = AValue then begin EnsureClientsRegistered; Exit; end;
  if FPlayerB <> nil then FPlayerB.UnregisterClient(Self);
  FPlayerB := AValue;
  if FPlayerB <> nil then
  begin
    FPlayerB.FreeNotification(Self);
    if not (csLoading in ComponentState) then FPlayerB.RegisterClient(Self);
  end;
  if not (csLoading in ComponentState) then StartAnalyzeAsync(1);
  Invalidate;
end;

procedure TZForkWaveform.SetActive(AValue: Boolean);
begin
  if FActive = AValue then Exit;
  FActive := AValue;
  UpdateTimer;
  Invalidate;
end;

procedure TZForkWaveform.SetStyle(AValue: TZForkWaveStyle);
begin
  if FStyle = AValue then Exit;
  FStyle := AValue;
  Invalidate;
end;

procedure TZForkWaveform.SetDetail(AValue: TZForkWaveDetail);
begin
  if FDetail = AValue then Exit;
  FDetail := AValue;
  Invalidate;
end;

procedure TZForkWaveform.ApplyQualityZoom;
begin
  case FQuality of
    wqNormal: FZoomMs := 8000;
    wqHigh:   FZoomMs := 4000;
    wqUltra:  FZoomMs := 2000;  { max. Detail: ~2 s Fenster }
  end;
end;

procedure TZForkWaveform.SetQuality(AValue: TZForkWaveQuality);
begin
  if FQuality = AValue then Exit;
  FQuality := AValue;
  ApplyQualityZoom;
  Invalidate;
end;

procedure TZForkWaveform.ApplyFpsPreset;
begin
  case FFpsPreset of
    wfps60: FTargetFPS := 60;
    wfps90: FTargetFPS := 90;
  end;
  { FTargetFPS wird vom Tick-Thread bei jeder Iteration frisch gelesen — kein
    Interval-Feld mehr noetig. }
end;

procedure TZForkWaveform.SetFpsPreset(AValue: TZForkWaveFPS);
begin
  if FFpsPreset = AValue then Exit;
  FFpsPreset := AValue;
  ApplyFpsPreset;
  UpdateTimer;
end;

procedure TZForkWaveform.SetColorsA(AValue: TZForkWaveDeckColors);
begin
  if AValue <> nil then FColorsA.Assign(AValue);
end;

procedure TZForkWaveform.SetColorsB(AValue: TZForkWaveDeckColors);
begin
  if AValue <> nil then FColorsB.Assign(AValue);
end;

procedure TZForkWaveform.SetBackColor(AValue: TColor);
begin
  if FBackColor = AValue then Exit;
  FBackColor := AValue;
  Color := AValue;
  Invalidate;
end;

procedure TZForkWaveform.SetNeedleColor(AValue: TColor);
begin
  if FNeedleColor = AValue then Exit;
  FNeedleColor := AValue;
  Invalidate;
end;

procedure TZForkWaveform.SetNeedleAlpha(AValue: Integer);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 255 then AValue := 255;
  if FNeedleAlpha = AValue then Exit;
  FNeedleAlpha := AValue;
  Invalidate;
end;

procedure TZForkWaveform.SetNeedleWidth(AValue: Integer);
begin
  if AValue < 1 then AValue := 1;
  if AValue > 8 then AValue := 8;
  if FNeedleWidth = AValue then Exit;
  FNeedleWidth := AValue;
  Invalidate;
end;

procedure TZForkWaveform.SetZoomMs(AValue: Integer);
begin
  if AValue < 250 then AValue := 250;       { eng: Beat-Grid }
  if AValue > 120000 then AValue := 120000; { weit: Uebersicht }
  if FZoomMs = AValue then Exit;
  FZoomMs := AValue;
  Invalidate;
end;

procedure TZForkWaveform.SetShowMirror(AValue: Boolean);
begin
  if FShowMirror = AValue then Exit;
  FShowMirror := AValue;
  Invalidate;
end;

procedure TZForkWaveform.SetShowGrid(AValue: Boolean);
begin
  if FShowGrid = AValue then Exit;
  FShowGrid := AValue;
  Invalidate;
end;

procedure TZForkWaveform.SetShowBeatStrip(AValue: Boolean);
begin
  if FShowBeatStrip = AValue then Exit;
  FShowBeatStrip := AValue;
  Invalidate;
end;

procedure TZForkWaveform.SetGridColor(AValue: TColor);
begin
  if FGridColor = AValue then Exit;
  FGridColor := AValue;
  Invalidate;
end;

procedure TZForkWaveform.SetGridAlpha(AValue: Integer);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 255 then AValue := 255;
  if FGridAlpha = AValue then Exit;
  FGridAlpha := AValue;
  Invalidate;
end;

procedure TZForkWaveform.SetBeatMarkerColor(AValue: TColor);
begin
  if FBeatMarkerColor = AValue then Exit;
  FBeatMarkerColor := AValue;
  Invalidate;
end;

procedure TZForkWaveform.SetBeatMarkerAlpha(AValue: Integer);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 255 then AValue := 255;
  if FBeatMarkerAlpha = AValue then Exit;
  FBeatMarkerAlpha := AValue;
  Invalidate;
end;

procedure TZForkWaveform.SetAutoAnalyze(AValue: Boolean);
begin
  FAutoAnalyze := AValue;
end;

procedure TZForkWaveform.SetTargetFPS(AValue: Integer);
begin
  if AValue < 15 then AValue := 15;
  if AValue > 240 then AValue := 240;
  if FTargetFPS = AValue then Exit;
  FTargetFPS := AValue;
  UpdateTimer;
end;

procedure TZForkWaveform.SetLatencyMs(AValue: Integer);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 2000 then AValue := 2000;
  if FLatencyMs = AValue then Exit;
  FLatencyMs := AValue;
end;

procedure TZForkWaveform.SetSyncOffsetMs(AValue: Integer);
begin
  if AValue < -2000 then AValue := -2000;
  if AValue > 2000 then AValue := 2000;
  if FSyncOffsetMs = AValue then Exit;
  FSyncOffsetMs := AValue;
end;

procedure TZForkWaveform.SetFastDraw(AValue: Boolean);
begin
  if FFastDraw = AValue then Exit;
  FFastDraw := AValue;
  Invalidate;
end;

procedure TZForkWaveform.SetSoftEdges(AValue: Boolean);
begin
  if FSoftEdges = AValue then Exit;
  FSoftEdges := AValue;
  Invalidate;
end;

procedure TZForkWaveform.SetColorBoost(AValue: Integer);
begin
  { 0 = Farben aus / transparent, 100 = normal, bis 300 = stark }
  if AValue < 0 then AValue := 0;
  if AValue > 300 then AValue := 300;
  if FColorBoost = AValue then Exit;
  FColorBoost := AValue;
  Invalidate;
end;

procedure TZForkWaveform.SetWheelZoom(AValue: Boolean);
begin
  FWheelZoom := AValue;
end;

function TZForkWaveform.DoMouseWheel(Shift: TShiftState; WheelDelta: Integer;
  MousePos: TPoint): Boolean;
var
  Steps: Double;
  NewZoom: Integer;
  Factor: Double;
begin
  { Musik-Zoom nur mit Strg+Mausrad — normales Rad bleibt für Storyboard/Parent }
  if (not FWheelZoom) or (csDesigning in ComponentState) or not (ssCtrl in Shift) then
  begin
    Result := inherited DoMouseWheel(Shift, WheelDelta, MousePos);
    Exit;
  end;
  Steps := Abs(WheelDelta) / 120.0;
  if Steps < 0.25 then Steps := 0.25;
  Factor := Power(1.22, Steps);
  if WheelDelta > 0 then
    NewZoom := Round(FZoomMs / Factor)   { rein = kleineres Fenster }
  else
    NewZoom := Round(FZoomMs * Factor);
  SetZoomMs(NewZoom);
  Result := True;
end;

procedure TZForkWaveform.UpdateTimer;
var
  ShouldRun: Boolean;
begin
  ShouldRun := FActive and not FShuttingDown
    and not (csDesigning in ComponentState)
    and not (csLoading in ComponentState);
  { Tick-Thread IMMER wenn Active — nie durch Analyse blockieren }
  if ShouldRun then
  begin
    if FTickThread = nil then
      FTickThread := TZForkWaveTickThread.Create(Self);
  end
  else if FTickThread <> nil then
  begin
    FTickThread.Terminate;
    FTickThread.WaitFor;
    FreeAndNil(FTickThread);
  end;
end;

function TZForkWaveform.GetFrameGapMs: Integer;
begin
  Result := Max(1, Round(1000.0 / Max(1, FTargetFPS)));
end;

procedure TZForkWaveform.EnsureBuffer;
var
  W, H: Integer;
begin
  W := Max(1, Width);
  H := Max(1, Height);
  if FBuffer = nil then
    FBuffer := TBitmap.Create;
  if (FBuffer.Width <> W) or (FBuffer.Height <> H) then
    FBuffer.SetSize(W, H);
end;

procedure TZForkWaveform.TimerTick(Sender: TObject);
begin
  if not FActive or FShuttingDown or (csDestroying in ComponentState) then Exit;
  SamplePositions;
  AdvanceDisplay;
  { Kein binäres Frame-Dropping mehr — das erzeugte sichtbare Aussetzer.
    WM_PAINT-Nachrichten koalesziert Windows ohnehin selbst bei Überlast;
    Paint liest nur noch den aktuellen Zustand statt selbst erneut
    weiterzuschalten (siehe Paint), daher bleibt die Position auch bei
    verzögertem Repaint akkurat. }
  if HandleAllocated and Visible then
    Invalidate;
end;

function TZForkWaveform.IsAnyPlaying: Boolean;
begin
  Result := FPlayingA or FPlayingB;
end;

procedure TZForkWaveform.SamplePositions;
var
  P: LongWord;
  Info: TStreamInfo;
  St: TStreamStatus;
  NowT: QWord;
begin
  NowT := GetTickCount64;
  if (FPlayerA <> nil) and (FPlayerA.Handle <> nil) then
  begin
    if FPlayerA.GetPositionMs(P) then
    begin
      FPosRawA := P;
      FPosTickA := NowT;
    end;
    FPlayingA := FPlayerA.GetStatus(St) and St.playing and not St.paused;
    if (FLengthA = 0) and FPlayerA.GetStreamInfo(0, Info) and (Info.length.ms > 0) then
      FLengthA := Info.length.ms;
  end
  else
    FPlayingA := False;

  if (FPlayerB <> nil) and (FPlayerB.Handle <> nil) then
  begin
    if FPlayerB.GetPositionMs(P) then
    begin
      FPosRawB := P;
      FPosTickB := NowT;
    end;
    FPlayingB := FPlayerB.GetStatus(St) and St.playing and not St.paused;
    if (FLengthB = 0) and FPlayerB.GetStreamInfo(0, Info) and (Info.length.ms > 0) then
      FLengthB := Info.length.ms;
  end
  else
    FPlayingB := False;
end;

procedure TZForkWaveform.AdvanceDisplay;
var
  NowT: QWord;
  Dt, Target, Err: Double;

  procedure Step(var Disp: Double; Raw, LastRaw: LongWord; Playing: Boolean;
    var OutLast: LongWord);
  begin
    Target := Double(Raw) - FLatencyMs + FSyncOffsetMs;
    if Target < 0 then Target := 0;
    if (LastRaw = High(LongWord)) or (not Playing) then
    begin
      Disp := Target;
      OutLast := Raw;
      Exit;
    end;
    if Raw <> LastRaw then
    begin
      Err := Target - Disp;
      if Abs(Err) > 180 then
        Disp := Target
      else
        Disp := Disp + Err * 0.35;
      OutLast := Raw;
    end
    else if Playing then
      Disp := Disp + Dt
    else
      Disp := Target;
    if Disp < 0 then Disp := 0;
  end;

begin
  NowT := GetTickCount64;
  if FDispTick = 0 then
    Dt := 0
  else
    Dt := Double(NowT - FDispTick);
  { Cap: UI-Freeze darf Display nicht um Sekunden vorauslaufen }
  if Dt > 50 then Dt := 50;
  FDispTick := NowT;
  Step(FDispA, FPosRawA, FLastRawA, FPlayingA, FLastRawA);
  Step(FDispB, FPosRawB, FLastRawB, FPlayingB, FLastRawB);
end;

function TZForkWaveform.DisplayPosA: LongWord;
begin
  if FDispA < 0 then Result := 0 else Result := LongWord(Trunc(FDispA));
end;

function TZForkWaveform.DisplayPosB: LongWord;
begin
  if FDispB < 0 then Result := 0 else Result := LongWord(Trunc(FDispB));
end;

procedure TZForkWaveform.ClearDeck(Deck: Integer);
begin
  if Deck = 0 then
  begin
    Inc(FGenA);
    SetLength(FBandKA, 0); SetLength(FBandMA, 0); SetLength(FBandHA, 0);
    SetLength(FBeatsA, 0);
    FLengthA := 0; FPosRawA := 0; FDispA := 0; FLastRawA := High(LongWord);
    FFileA := ''; FAnalyzingA := False;
  end
  else
  begin
    Inc(FGenB);
    SetLength(FBandKB, 0); SetLength(FBandMB, 0); SetLength(FBandHB, 0);
    SetLength(FBeatsB, 0);
    FLengthB := 0; FPosRawB := 0; FDispB := 0; FLastRawB := High(LongWord);
    FFileB := ''; FAnalyzingB := False;
  end;
  Invalidate;
end;

function TZForkWaveform.ResolveLengthMs(Player: TZForkPlayer; AnalyzedMs: LongWord): LongWord;
var
  Info: TStreamInfo;
begin
  Result := AnalyzedMs;
  if (Player <> nil) and (Player.Handle <> nil) and Player.GetStreamInfo(0, Info) then
  begin
    if Info.length.ms > Result then Result := Info.length.ms;
    if (Result = 0) and (Info.length.sec > 0) then Result := Info.length.sec * 1000;
  end;
end;

procedure TZForkWaveform.StartAnalyzeAsync(Deck: Integer);
var
  Player: TZForkPlayer;
  Path: string;
  Thr: TZForkWaveAnalyzeThread;
  Gen: Integer;
begin
  if not FAutoAnalyze then Exit;
  if csDesigning in ComponentState then Exit;
  if Deck = 0 then Player := FPlayerA else Player := FPlayerB;
  if (Player = nil) then begin ClearDeck(Deck); Exit; end;
  Path := Trim(Player.FileName);
  if Path = '' then begin ClearDeck(Deck); Exit; end;

  if Deck = 0 then
  begin
    if (Path = FFileA) and (Length(FBandKA) > 0) and (not FAnalyzingA) then Exit;
    Inc(FGenA);
    Gen := FGenA;
    FAnalyzingA := True;
  end
  else
  begin
    if (Path = FFileB) and (Length(FBandKB) > 0) and (not FAnalyzingB) then Exit;
    Inc(FGenB);
    Gen := FGenB;
    FAnalyzingB := True;
  end;

  Thr := TZForkWaveAnalyzeThread.Create(Self, Deck, Gen, Path);
  Thr.Start;
end;

procedure TZForkWaveform.BuildBeatsFromKick(const Kick: array of Byte; LengthMs: LongWord; var Beats: TSingleDynArray);
var
  I, N, C, Cap: Integer;
  V, Prev, Next: Single;
begin
  SetLength(Beats, 0);
  N := Length(Kick);
  if (N < 3) or (LengthMs = 0) then Exit;
  Cap := N div 8 + 16;
  SetLength(Beats, Cap);
  C := 0;
  for I := 1 to N - 2 do
  begin
    V := Kick[I] / 255.0;
    if V < 0.38 then Continue;
    Prev := Kick[I - 1] / 255.0;
    Next := Kick[I + 1] / 255.0;
    if (V >= Prev) and (V > Next) then
    begin
      if C >= Cap then
      begin
        Cap := Cap * 2;
        SetLength(Beats, Cap);
      end;
      Beats[C] := (I / Max(1, N - 1)) * LengthMs;
      Inc(C);
    end;
  end;
  SetLength(Beats, C);
end;

procedure TZForkWaveform.ApplyAnalyzeResult(Deck: Integer; const Raw: array of Byte;
  Count, LenMs: LongWord; const Path: string);
var
  N, I, TCount: Integer;
  Player: TZForkPlayer;
  IsRGB: Boolean;
begin
  if Deck = 0 then Player := FPlayerA else Player := FPlayerB;
  LenMs := ResolveLengthMs(Player, LenMs);
  N := Integer(Count);
  IsRGB := (N >= 9) and ((N mod 3) = 0);
  if IsRGB then
    TCount := N div 3
  else
    TCount := N;

  if Deck = 0 then
  begin
    SetLength(FBandKA, TCount);
    SetLength(FBandMA, TCount);
    SetLength(FBandHA, TCount);
    if IsRGB then
      for I := 0 to TCount - 1 do
      begin
        FBandKA[I] := Raw[I * 3];
        FBandMA[I] := Raw[I * 3 + 1];
        FBandHA[I] := Raw[I * 3 + 2];
      end
    else
      for I := 0 to TCount - 1 do
      begin
        FBandKA[I] := Raw[I];
        FBandMA[I] := Raw[I];
        FBandHA[I] := Raw[I];
      end;
    FLengthA := LenMs;
    FFileA := Path;
    BuildBeatsFromKick(FBandKA, FLengthA, FBeatsA);
    FAnalyzingA := False;
  end
  else
  begin
    SetLength(FBandKB, TCount);
    SetLength(FBandMB, TCount);
    SetLength(FBandHB, TCount);
    if IsRGB then
      for I := 0 to TCount - 1 do
      begin
        FBandKB[I] := Raw[I * 3];
        FBandMB[I] := Raw[I * 3 + 1];
        FBandHB[I] := Raw[I * 3 + 2];
      end
    else
      for I := 0 to TCount - 1 do
      begin
        FBandKB[I] := Raw[I];
        FBandMB[I] := Raw[I];
        FBandHB[I] := Raw[I];
      end;
    FLengthB := LenMs;
    FFileB := Path;
    BuildBeatsFromKick(FBandKB, FLengthB, FBeatsB);
    FAnalyzingB := False;
  end;
  Invalidate;
end;

procedure TZForkWaveform.RefreshWaveforms;
begin
  EnsureClientsRegistered;
  FFileA := '';
  FFileB := '';
  StartAnalyzeAsync(0);
  StartAnalyzeAsync(1);
end;

procedure TZForkWaveform.NotifyPlayerOpen(const AFileName: string);
begin
  RefreshWaveforms;
end;

procedure TZForkWaveform.NotifyPlayerSong(SongIndex: Integer);
begin
  RefreshWaveforms;
end;

procedure TZForkWaveform.NotifyPlayerClose;
begin
  RefreshWaveforms;
end;

procedure TZForkWaveform.NotifyPlayerStop;
begin
  SamplePositions;
  Invalidate;
end;

function TZForkWaveform.MaxRangeBand(const Bins: array of Byte; T0, T1: Single): Single;
var
  N, I0, I1, I: Integer;
  M: Byte;
  A, B: Single;
begin
  N := Length(Bins);
  if N < 1 then Exit(0);
  A := T0; B := T1;
  if A > B then begin A := T1; B := T0; end;
  if A < 0 then A := 0;
  if B > 1 then B := 1;
  if B < A then Exit(0);
  I0 := Trunc(A * (N - 1));
  I1 := Trunc(B * (N - 1) + 0.999);
  if I0 < 0 then I0 := 0;
  if I1 >= N then I1 := N - 1;
  if I1 < I0 then I1 := I0;
  M := Bins[I0];
  for I := I0 + 1 to I1 do
    if Bins[I] > M then M := Bins[I];
  Result := M / 255.0;
end;

function TZForkWaveform.MixRGB(C1, C2: TColor; T: Single): TColor;
var
  R1, G1, B1, R2, G2, B2: Integer;
begin
  T := EnsureRange(T, 0, 1);
  C1 := ColorToRGB(C1); C2 := ColorToRGB(C2);
  R1 := Red(C1); G1 := Green(C1); B1 := Blue(C1);
  R2 := Red(C2); G2 := Green(C2); B2 := Blue(C2);
  Result := RGBToColor(
    Round(R1 + (R2 - R1) * T),
    Round(G1 + (G2 - G1) * T),
    Round(B1 + (B2 - B1) * T));
end;

function TZForkWaveform.BlendPixel(Bg, Fg: TColor; Alpha: Integer): TColor;
begin
  Result := MixRGB(Bg, Fg, EnsureRange(Alpha / 255.0, 0, 1));
end;

procedure TZForkWaveform.BoostRGB(var R, G, B: Integer);
var
  F, M, Br: Single;
begin
  { 0 = keine Farbe, 100 = neutral, >100 = Sättigung + Helligkeit }
  if FColorBoost <= 0 then
  begin
    R := 0; G := 0; B := 0;
    Exit;
  end;
  F := FColorBoost / 100.0;
  M := (R + G + B) / 3.0;
  R := Round(EnsureRange(M + (R - M) * F, 0, 255));
  G := Round(EnsureRange(M + (G - M) * F, 0, 255));
  B := Round(EnsureRange(M + (B - M) * F, 0, 255));
  if F > 1.0 then
  begin
    Br := 1.0 + (F - 1.0) * 0.45;
    R := Round(EnsureRange(R * Br, 0, 255));
    G := Round(EnsureRange(G * Br, 0, 255));
    B := Round(EnsureRange(B * Br, 0, 255));
  end;
end;

procedure TZForkWaveform.PaintLane(ACanvas: TCanvas; const ARect: TRect;
  const Kick, Mid, High: array of Byte; const Beats: array of Single;
  LengthMs, PosMs: LongWord; Colors: TZForkWaveDeckColors);
var
  X, MidY, HalfH, H, W, StripH, I, Hgt, EffAlpha, TipAlpha, StepX, Xw: Integer;
  WaveR, StripR: TRect;
  WinStart, WinEnd, T0, T1, AmpK, AmpM, AmpH, Amp, Sum, EffLen, PxMs, BT, BeatMs: Single;
  Wk, Wm, Wh, Strength: Single;
  Col: TColor;
  Rk, Gk, Bk, Rm, Gm, Bm, Rh, Gh, Bh, Cr, Cg, Cb: Integer;
  UseSoft: Boolean;
begin
  if Length(Kick) < 2 then Exit;
  Strength := EnsureRange(FColorBoost / 100.0, 0, 3.0);
  EffAlpha := Round(EnsureRange(Colors.Alpha, 0, 255) * Min(Strength, 1.0));
  if EffAlpha <= 0 then Exit;
  EffLen := LengthMs;
  if EffLen < 2 then EffLen := Max(FZoomMs, Integer(PosMs) + FZoomMs);

  Hgt := ARect.Bottom - ARect.Top;
  StripH := 0;
  if FShowBeatStrip then StripH := Max(10, Hgt div 7);
  WaveR := Rect(ARect.Left, ARect.Top, ARect.Right, ARect.Bottom - StripH);
  StripR := Rect(ARect.Left, WaveR.Bottom, ARect.Right, ARect.Bottom);

  MidY := (WaveR.Top + WaveR.Bottom) div 2;
  if FShowMirror then
    HalfH := Max(2, (WaveR.Bottom - WaveR.Top) div 2 - 1)
  else
  begin
    MidY := WaveR.Bottom - 1;
    HalfH := Max(2, WaveR.Bottom - WaveR.Top - 2);
  end;

  WinStart := Double(PosMs) - FZoomMs * 0.5;
  WinEnd := WinStart + FZoomMs;
  W := Max(1, WaveR.Right - WaveR.Left);
  PxMs := (WinEnd - WinStart) / W;
  { FastDraw: jede 2. Spalte + breiter Strich — halbiert GDI-Calls }
  if FFastDraw then StepX := 2 else StepX := 1;
  UseSoft := FSoftEdges and (not FFastDraw);

  if FShowGrid and (not FFastDraw) and (Length(Beats) >= 4) then
  begin
    BeatMs := 0;
    for I := 1 to Min(32, Length(Beats) - 1) do
      BeatMs := BeatMs + (Beats[I] - Beats[I - 1]);
    BeatMs := BeatMs / Min(32, Length(Beats) - 1);
    if (BeatMs > 200) and (BeatMs < 1200) then
    begin
      ACanvas.Pen.Width := 1;
      BT := WinStart - Frac(WinStart / BeatMs) * BeatMs;
      while BT < WinEnd do
      begin
        if BT >= WinStart then
        begin
          X := WaveR.Left + Round((BT - WinStart) / Max(Single(1.0), WinEnd - WinStart) * W);
          ACanvas.Pen.Color := BlendPixel(FBackColor, FGridColor, FGridAlpha);
          ACanvas.Line(X, WaveR.Top, X, WaveR.Bottom);
        end;
        BT := BT + BeatMs;
      end;
    end;
  end;

  Rk := Red(ColorToRGB(Colors.Kick));
  Gk := Green(ColorToRGB(Colors.Kick));
  Bk := Blue(ColorToRGB(Colors.Kick));
  Rm := Red(ColorToRGB(Colors.Mid));
  Gm := Green(ColorToRGB(Colors.Mid));
  Bm := Blue(ColorToRGB(Colors.Mid));
  Rh := Red(ColorToRGB(Colors.High));
  Gh := Green(ColorToRGB(Colors.High));
  Bh := Blue(ColorToRGB(Colors.High));

  ACanvas.Pen.Style := psClear;
  X := WaveR.Left;
  while X < WaveR.Right do
  begin
    Xw := Min(StepX, WaveR.Right - X);
    T0 := (WinStart + (X - WaveR.Left) * PxMs) / EffLen;
    T1 := (WinStart + (X - WaveR.Left + Xw) * PxMs) / EffLen;
    if T1 < 0 then begin Inc(X, StepX); Continue; end;
    if T0 > 1 then begin Inc(X, StepX); Continue; end;

    if FFastDraw then
    begin
      { Ein Sample statt Range-Scan — deutlich schneller }
      AmpK := MaxRangeBand(Kick, T0, T0);
      AmpM := MaxRangeBand(Mid, T0, T0);
      AmpH := MaxRangeBand(High, T0, T0);
    end
    else
    begin
      AmpK := MaxRangeBand(Kick, T0, T1);
      AmpM := MaxRangeBand(Mid, T0, T1);
      AmpH := MaxRangeBand(High, T0, T1);
    end;

    if (not FFastDraw) and (FDetail = wdPeaks) then
    begin
      if AmpK > 0 then AmpK := Power(AmpK, 0.60);
      if AmpM > 0 then AmpM := Power(AmpM, 0.62);
      if AmpH > 0 then AmpH := Power(AmpH, 0.55);
    end
    else if not FFastDraw then
    begin
      if AmpK > 0 then AmpK := Power(AmpK, 0.82);
      if AmpM > 0 then AmpM := Power(AmpM, 0.84);
      if AmpH > 0 then AmpH := Power(AmpH, 0.78);
    end;

    Amp := AmpK;
    if AmpM > Amp then Amp := AmpM;
    if AmpH > Amp then Amp := AmpH;
    if Amp < 0.012 then begin Inc(X, StepX); Continue; end;

    if FFastDraw then
    begin
      { billiger Dominant-Mix ohne Power() }
      Wk := AmpK * AmpK;
      Wm := AmpM * AmpM;
      Wh := AmpH * AmpH;
    end
    else
    begin
      Wk := Power(Max(AmpK, 0.0), 2.5);
      Wm := Power(Max(AmpM, 0.0), 2.5);
      Wh := Power(Max(AmpH, 0.0), 2.5);
    end;
    Sum := Wk + Wm + Wh;
    if Sum < 0.0001 then Sum := 0.0001;
    Cr := Round((Wk * Rk + Wm * Rm + Wh * Rh) / Sum);
    Cg := Round((Wk * Gk + Wm * Gm + Wh * Gh) / Sum);
    Cb := Round((Wk * Bk + Wm * Bm + Wh * Bh) / Sum);
    BoostRGB(Cr, Cg, Cb);
    Col := BlendPixel(FBackColor, RGBToColor(Cr, Cg, Cb), EffAlpha);

    H := Round(HalfH * Amp * EnsureRange(0.55 + Strength * 0.45, 0.55, 1.35));
    if H < 1 then begin Inc(X, StepX); Continue; end;
    ACanvas.Brush.Color := Col;
    TipAlpha := Max(1, EffAlpha div 2);
    if FShowMirror then
    begin
      if UseSoft and (H > 2) then
      begin
        ACanvas.FillRect(X, MidY - (H - 1), X + Xw, MidY + (H - 1));
        ACanvas.Brush.Color := BlendPixel(FBackColor, RGBToColor(Cr, Cg, Cb), TipAlpha);
        ACanvas.FillRect(X, MidY - H, X + Xw, MidY - H + 1);
        ACanvas.FillRect(X, MidY + H - 1, X + Xw, MidY + H);
      end
      else
        ACanvas.FillRect(X, MidY - H, X + Xw, MidY + H);
    end
    else
    begin
      if UseSoft and (H > 2) then
      begin
        ACanvas.FillRect(X, MidY - (H - 1), X + Xw, MidY);
        ACanvas.Brush.Color := BlendPixel(FBackColor, RGBToColor(Cr, Cg, Cb), TipAlpha);
        ACanvas.FillRect(X, MidY - H, X + Xw, MidY - H + 1);
      end
      else
        ACanvas.FillRect(X, MidY - H, X + Xw, MidY);
    end;
    Inc(X, StepX);
  end;
  ACanvas.Pen.Style := psSolid;

  if FShowBeatStrip and (StripH > 2) then
  begin
    ACanvas.Brush.Color := BlendPixel(FBackColor, clGray, 28);
    ACanvas.FillRect(StripR);
    ACanvas.Pen.Style := psClear;
    for I := 0 to Length(Beats) - 1 do
    begin
      BT := Beats[I];
      if (BT < WinStart) or (BT > WinEnd) then Continue;
      X := StripR.Left + Round((BT - WinStart) / Max(Single(1.0), WinEnd - WinStart) * (StripR.Right - StripR.Left));
      ACanvas.Brush.Color := BlendPixel(FBackColor, FBeatMarkerColor, FBeatMarkerAlpha);
      ACanvas.FillRect(X - 2, StripR.Top + 1, X + 3, StripR.Bottom - 1);
    end;
    ACanvas.Pen.Style := psSolid;
  end;
end;

procedure TZForkWaveform.PaintNeedle(ACanvas: TCanvas; const ARect: TRect);
var
  CX, W: Integer;
begin
  CX := (ARect.Left + ARect.Right) div 2;
  W := Max(1, FNeedleWidth);
  ACanvas.Pen.Style := psClear;
  ACanvas.Brush.Color := BlendPixel(FBackColor, FNeedleColor, FNeedleAlpha);
  ACanvas.FillRect(CX - W div 2, ARect.Top, CX - W div 2 + W, ARect.Bottom);
  ACanvas.Pen.Style := psSolid;
end;

procedure TZForkWaveform.PaintDesignTime(ACanvas: TCanvas; const ARect: TRect);
var
  TopR, BotR: TRect;
  Mid, I, N: Integer;
  T, Kick, Md, Hi: Single;
begin
  Mid := (ARect.Top + ARect.Bottom) div 2;
  TopR := Rect(ARect.Left, ARect.Top, ARect.Right, Mid);
  BotR := Rect(ARect.Left, Mid, ARect.Right, ARect.Bottom);
  N := 512;
  SetLength(FBandKA, N); SetLength(FBandMA, N); SetLength(FBandHA, N);
  SetLength(FBeatsA, 0);
  for I := 0 to N - 1 do
  begin
    T := I / (N - 1);
    Kick := Abs(Sin(T * Pi * 48 + FTick * 0.08)) * Abs(Sin(T * Pi * 4));
    Md   := Abs(Sin(T * Pi * 90 + 1.2)) * 0.7;
    Hi   := Abs(Sin(T * Pi * 180)) * Abs(Cos(T * Pi * 12)) * 0.55;
    if (I mod 16) = 0 then Kick := 1.0;
    FBandKA[I] := Byte(Round(EnsureRange(Kick, 0, 1) * 255));
    FBandMA[I] := Byte(Round(EnsureRange(Md, 0, 1) * 255));
    FBandHA[I] := Byte(Round(EnsureRange(Hi, 0, 1) * 255));
    if (I mod 16) = 0 then
    begin
      Mid := Length(FBeatsA);
      SetLength(FBeatsA, Mid + 1);
      FBeatsA[Mid] := T * 10000;
    end;
  end;
  PaintLane(ACanvas, TopR, FBandKA, FBandMA, FBandHA, FBeatsA, 10000, 2000 + FTick * 20, FColorsA);
  PaintLane(ACanvas, BotR, FBandKA, FBandMA, FBandHA, FBeatsA, 10000, 2000 + FTick * 20, FColorsB);
  PaintNeedle(ACanvas, ARect);
end;

procedure TZForkWaveform.EraseBackground(DC: HDC);
begin
end;

procedure TZForkWaveform.Paint;
var
  R, TopR, BotR: TRect;
  DC: TCanvas;
  Mid: Integer;
  PosA, PosB: LongWord;
  HasA, HasB: Boolean;
  T0: QWord;
begin
  T0 := GetTickCount64;
  FPaintBusy := True;
  try
  R := ClientRect;
  EnsureBuffer;
  DC := FBuffer.Canvas;
  DC.Brush.Color := FBackColor;
  DC.FillRect(R);

  { AdvanceDisplay laeuft nur noch im Tick-Thread (TimerTick) — ein erneuter
    Aufruf hier fuehrte bei verzoegertem WM_PAINT zu ungleichmaessigen
    Zeitschritten (Doppel-Advance). Paint liest nur den aktuellen Stand. }
  PosA := DisplayPosA;
  PosB := DisplayPosB;
  HasA := Length(FBandKA) > 1;
  HasB := Length(FBandKB) > 1;

  if csDesigning in ComponentState then
  begin
    Inc(FTick);
    PaintDesignTime(DC, R);
  end
  else
  begin
    case FStyle of
      wsStacked:
        begin
          if HasA and HasB then
          begin
            Mid := (R.Top + R.Bottom) div 2;
            TopR := Rect(R.Left, R.Top, R.Right, Mid);
            BotR := Rect(R.Left, Mid, R.Right, R.Bottom);
            PaintLane(DC, TopR, FBandKA, FBandMA, FBandHA, FBeatsA, FLengthA, PosA, FColorsA);
            PaintLane(DC, BotR, FBandKB, FBandMB, FBandHB, FBeatsB, FLengthB, PosB, FColorsB);
          end
          else if HasA then
            PaintLane(DC, R, FBandKA, FBandMA, FBandHA, FBeatsA, FLengthA, PosA, FColorsA)
          else if HasB then
            PaintLane(DC, R, FBandKB, FBandMB, FBandHB, FBeatsB, FLengthB, PosB, FColorsB);
        end;
      wsOverlay:
        begin
          if HasA then
            PaintLane(DC, R, FBandKA, FBandMA, FBandHA, FBeatsA, FLengthA, PosA, FColorsA);
          if HasB then
            PaintLane(DC, R, FBandKB, FBandMB, FBandHB, FBeatsB, FLengthB, PosB, FColorsB);
        end;
      wsSingle:
        if HasA then
          PaintLane(DC, R, FBandKA, FBandMA, FBandHA, FBeatsA, FLengthA, PosA, FColorsA)
        else if HasB then
          PaintLane(DC, R, FBandKB, FBandMB, FBandHB, FBeatsB, FLengthB, PosB, FColorsB);
    end;
    PaintNeedle(DC, R);
  end;

  Canvas.CopyRect(R, FBuffer.Canvas, R);
  finally
    FPaintBusy := False;
    FLastPaintDur := Integer(GetTickCount64 - T0);
  end;
end;

end.
