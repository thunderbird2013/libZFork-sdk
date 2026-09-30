{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  TZForkStoryboard — Radio-Programm-Timeline (Spuren A/B/C)

  Was die Komponente kann:
  - Clips (Musik/Spot) auf Spuren A/B/C: anlegen, trimmen, verschieben
  - Drag&Drop von Dateien, Zoom, Playhead, Minuten-Lineal
  - Scheduler/Wiedergabe des Programms ohne zusaetzliche DLL-Features
  - DuckOnC, AutoBeatmatch, Beat-Grid, Wave-Analyse pro Clip
  - Clip-Automation (Volumen/EQ/Echo/Reverb/SideCut) zeichnen/abspielen
  - Play/Pause/SeekProgram; Anbindung an bis zu drei Player
******************************************************************************}
unit ZForkStoryboard;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Math, Types, Controls, Graphics, ExtCtrls, StdCtrls, Menus,
  Forms, LCLType, LCLIntf, LMessages, Windows, ShellAPI,
  ZForkPlayer, ZForkAbout, ZForkStoryAuto, LibZFork;

type
  TZForkStoryTrack = (
    stTrackA,
    stTrackB,
    stTrackC
  );

  TZForkStoryboard = class;
  TZForkStoryClipAnalyzeThread = class;

  TZForkStoryClip = class(TCollectionItem)
  private
    FTrack: TZForkStoryTrack;
    FFileName: string;
    FStartMs: LongWord;
    FLengthMs: LongWord;
    FInMs: LongWord;           { Trim vorne (Offset in Quelldatei) }
    FSourceLengthMs: LongWord; { volle Dateilänge }
    FColor: TColor;
    FTitle: string;
    FBpm: Single;
    FBeatOffsetMs: LongWord;   { erstes Beat relativ zur Datei }
    FWave: array of Byte;   { Envelope 0..255, für Minimap }
    FAnalyzing: Boolean;
    FGen: Integer;
    FAutomation: TZForkClipAutomation;
    procedure SetTrack(AValue: TZForkStoryTrack);
    procedure SetFileName(const AValue: string);
    procedure SetStartMs(AValue: LongWord);
    procedure SetLengthMs(AValue: LongWord);
    procedure SetInMs(AValue: LongWord);
    procedure SetColor(AValue: TColor);
    procedure SetTitle(const AValue: string);
    procedure SetBeatOffsetMs(AValue: LongWord);
    function GetWaveLength: Integer;
  protected
    function GetDisplayName: string; override;
  public
    constructor Create(ACollection: TCollection); override;
    destructor Destroy; override;
    function EndMs: LongWord;
    function OutMs: LongWord; { InMs + LengthMs in Quelle }
    property Bpm: Single read FBpm write FBpm;
    property Analyzing: Boolean read FAnalyzing;
    property WaveLength: Integer read GetWaveLength;
    property SourceLengthMs: LongWord read FSourceLengthMs write FSourceLengthMs;
    property Automation: TZForkClipAutomation read FAutomation;
  published
    property Track: TZForkStoryTrack read FTrack write SetTrack default stTrackA;
    property FileName: string read FFileName write SetFileName;
    property StartMs: LongWord read FStartMs write SetStartMs default 0;
    property LengthMs: LongWord read FLengthMs write SetLengthMs default 30000;
    property InMs: LongWord read FInMs write SetInMs default 0;
    property BeatOffsetMs: LongWord read FBeatOffsetMs write SetBeatOffsetMs default 0;
    property Color: TColor read FColor write SetColor default clNavy;
    property Title: string read FTitle write SetTitle;
  end;

  TZForkStoryClips = class(TOwnedCollection)
  private
    function GetItem(Index: Integer): TZForkStoryClip;
    procedure SetItem(Index: Integer; AValue: TZForkStoryClip);
  protected
    procedure Update(Item: TCollectionItem); override;
  public
    property Items[Index: Integer]: TZForkStoryClip read GetItem write SetItem; default;
  end;

  TZForkStoryDragMode = (
    sdmNone,
    sdmMove,
    sdmTrimLeft,
    sdmTrimRight,
    sdmNudgeGrid,
    sdmAutoPoint
  );

  TZForkStoryboard = class(TCustomControl)
  private
    FPlayerA: TZForkPlayer;
    FPlayerB: TZForkPlayer;
    FPlayerC: TZForkPlayer;
    FClips: TZForkStoryClips;
    FActive: Boolean;
    FPlaying: Boolean;
    FProgramMs: LongWord;
    FDurationMs: LongWord;
    FPixelsPerMinute: Integer;
    FLabelWidth: Integer;
    FRulerHeight: Integer;
    FTrackHeight: Integer;
    FBackColor: TColor;
    FRulerColor: TColor;
    FGridColor: TColor;
    FPlayheadColor: TColor;
    FDuckOnC: Boolean;
    FDuckPercent: Integer;
    FAutoBeatmatch: Boolean;
    FMaxTempoPct: Integer;
    FShowBeatGrid: Boolean;
    FWheelZoom: Boolean;
    FShowAutomation: Boolean;
    FEditAutoParam: TZForkAutoParam;
    FAutoColors: TZForkStoryAutoColors;
    FPopup: TPopupMenu;
    FCtxClip: Integer;
    FCtxX, FCtxY: Integer;
    FDragAutoPt: Integer;
    FSavedVolA: Integer;
    FSavedVolB: Integer;
    FDucking: Boolean;
    FAbout: string;
    FTimer: TTimer;
    FHscroll: TScrollBar;
    FUpdatingScroll: Boolean;
    FScrollX: Integer;
    FDragClip: Integer;
    FDragOffsetX: Integer;
    FDragMode: TZForkStoryDragMode;
    FNudgeStartX: Integer;
    FNudgeStartOffset: LongWord;
    FActiveClipA: Integer;
    FActiveClipB: Integer;
    FActiveClipC: Integer;
    FPlayStartTick: QWord;
    FPlayStartMs: LongWord;
    FOnChange: TNotifyEvent;
    FOnProgramTime: TNotifyEvent;
    procedure SetPlayerA(AValue: TZForkPlayer);
    procedure SetPlayerB(AValue: TZForkPlayer);
    procedure SetPlayerC(AValue: TZForkPlayer);
    procedure SetClips(AValue: TZForkStoryClips);
    procedure SetActive(AValue: Boolean);
    procedure SetDurationMs(AValue: LongWord);
    procedure SetPixelsPerMinute(AValue: Integer);
    procedure SetBackColor(AValue: TColor);
    procedure SetDuckOnC(AValue: Boolean);
    procedure SetDuckPercent(AValue: Integer);
    procedure SetAutoBeatmatch(AValue: Boolean);
    procedure SetMaxTempoPct(AValue: Integer);
    procedure SetShowBeatGrid(AValue: Boolean);
    procedure SetWheelZoom(AValue: Boolean);
    procedure SetShowAutomation(AValue: Boolean);
    procedure SetEditAutoParam(AValue: TZForkAutoParam);
    procedure SetAutoColors(AValue: TZForkStoryAutoColors);
    procedure AutoColorsChanged(Sender: TObject);
    function GetAbout: string;
    procedure SetAbout(const AValue: string);
    procedure TimerTick(Sender: TObject);
    procedure UpdateTimer;
    procedure Changed;
    function ContentWidth: Integer;
    function TracksBottom: Integer;
    function TrackRect(Track: TZForkStoryTrack): TRect;
    function RulerRect: TRect;
    function MsToX(Ms: LongWord): Integer;
    function XToMs(X: Integer): LongWord;
    function HitTrack(Y: Integer): Integer; { -1 none, 0..2 }
    function HitClip(X, Y: Integer): Integer;
    function HitClipEdge(ClipIndex, X: Integer): TZForkStoryDragMode;
    function DefaultColor(Track: TZForkStoryTrack): TColor;
    function ProbeLengthMs(Player: TZForkPlayer; const Path: string): LongWord;
    function PlayerFor(Track: TZForkStoryTrack): TZForkPlayer;
    procedure EnsureDurationForClip(Clip: TZForkStoryClip);
    procedure SyncActiveClips;
    procedure StartClipOnPlayer(ClipIndex: Integer);
    procedure StopTrackPlayer(Track: TZForkStoryTrack);
    procedure ApplyDuck(WantDuck: Boolean);
    procedure UpdateHScroll;
    procedure ClampScrollX;
    procedure HScrollChange(Sender: TObject);
    procedure DrawRuler(ACanvas: TCanvas);
    procedure DrawTrack(ACanvas: TCanvas; Track: TZForkStoryTrack);
    procedure DrawClipWave(ACanvas: TCanvas; const CR: TRect; Clip: TZForkStoryClip);
    procedure DrawClipBeatGrid(ACanvas: TCanvas; const CR: TRect; Clip: TZForkStoryClip);
    procedure DrawClipTrimHandles(ACanvas: TCanvas; const CR: TRect);
    procedure DrawClipAutomation(ACanvas: TCanvas; const CR: TRect; Clip: TZForkStoryClip);
    procedure DrawTransitions(ACanvas: TCanvas);
    procedure DrawPlayhead(ACanvas: TCanvas);
    procedure ProcessDroppedFiles(const FileNames: array of string; X, Y: Integer);
    procedure HandleHDROP(Drop: HDROP);
    procedure StartAnalyzeClip(Clip: TZForkStoryClip);
    procedure ApplyClipAnalysis(Clip: TZForkStoryClip; const Wave: array of Byte;
      WaveCount: Integer; Bpm: Single; LengthMs: LongWord);
    function ReferenceBpm(ForClip: TZForkStoryClip): Single;
    procedure ApplyBeatmatchTempo(Clip: TZForkStoryClip; Player: TZForkPlayer);
    procedure ApplyClipAutomation(Clip: TZForkStoryClip; Player: TZForkPlayer;
      LocalMs: LongWord);
    procedure ZoomAt(X: Integer; ZoomIn: Boolean);
    procedure ZoomToClip(ClipIndex: Integer);
    procedure ApplyTrimLeft(Clip: TZForkStoryClip; NewStartMs: LongWord);
    procedure ApplyTrimRight(Clip: TZForkStoryClip; NewEndMs: LongWord);
    function ClipLocalMs(Clip: TZForkStoryClip; X: Integer): LongWord;
    function YToAutoValue(const CR: TRect; Y: Integer): Single;
    function AutoValueToY(const CR: TRect; Value: Single): Integer;
    function HitAutoPoint(ClipIndex, X, Y: Integer; out PtIndex: Integer): Boolean;
    procedure BuildAutoPopup;
    procedure PopupAddPoint(Sender: TObject);
    procedure PopupDeletePoint(Sender: TObject);
    procedure PopupClearCurve(Sender: TObject);
    procedure PopupSelectParam(Sender: TObject);
    procedure ClipAutoChanged(Sender: TObject);
  protected
    procedure Notification(AComponent: TComponent; Operation: TOperation); override;
    procedure Paint; override;
    procedure CreateWnd; override;
    procedure DestroyWnd; override;
    procedure WndProc(var Message: TLMessage); override;
    procedure MouseDown(Button: TMouseButton; Shift: TShiftState; X, Y: Integer); override;
    procedure MouseMove(Shift: TShiftState; X, Y: Integer); override;
    procedure MouseUp(Button: TMouseButton; Shift: TShiftState; X, Y: Integer); override;
    procedure MouseEnter; override;
    procedure DblClick; override;
    function DoMouseWheel(Shift: TShiftState; WheelDelta: Integer;
      MousePos: TPoint): Boolean; override;
    procedure Resize; override;
    procedure Loaded; override;
  public
    constructor Create(AOwner: TComponent); override;
    destructor Destroy; override;
    procedure ShowAbout;
    function AddFile(Track: TZForkStoryTrack; const Path: string;
      StartMs: LongWord = High(LongWord)): TZForkStoryClip;
    { Dateien von außen (Form.OnDropFiles oder Explorer) }
    procedure DropFilesAt(const FileNames: array of string; X, Y: Integer);
    function ClientHitTest(X, Y: Integer): Boolean;
    procedure ClearClips;
    procedure Play;
    procedure Pause;
    procedure Stop;
    procedure SeekProgram(Ms: LongWord);
    procedure ZoomIn;
    procedure ZoomOut;
    procedure ZoomFit;
    property Playing: Boolean read FPlaying;
    property ProgramMs: LongWord read FProgramMs;
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
    property PlayerC: TZForkPlayer read FPlayerC write SetPlayerC;
    property Clips: TZForkStoryClips read FClips write SetClips;
    property Active: Boolean read FActive write SetActive default True;
    property DurationMs: LongWord read FDurationMs write SetDurationMs default 600000;
    property PixelsPerMinute: Integer read FPixelsPerMinute write SetPixelsPerMinute default 80;
    property BackColor: TColor read FBackColor write SetBackColor default clBlack;
    property DuckOnC: Boolean read FDuckOnC write SetDuckOnC default True;
    property DuckPercent: Integer read FDuckPercent write SetDuckPercent default 25;
    property AutoBeatmatch: Boolean read FAutoBeatmatch write SetAutoBeatmatch default True;
    property MaxTempoPct: Integer read FMaxTempoPct write SetMaxTempoPct default 8;
    property ShowBeatGrid: Boolean read FShowBeatGrid write SetShowBeatGrid default True;
    property WheelZoom: Boolean read FWheelZoom write SetWheelZoom default True;
    property ShowAutomation: Boolean read FShowAutomation write SetShowAutomation default True;
    property EditAutoParam: TZForkAutoParam read FEditAutoParam write SetEditAutoParam default apVolume;
    property AutoColors: TZForkStoryAutoColors read FAutoColors write SetAutoColors;
    property OnChange: TNotifyEvent read FOnChange write FOnChange;
    property OnProgramTime: TNotifyEvent read FOnProgramTime write FOnProgramTime;
  end;

  { Async Wave+BPM für einen Clip }
  TZForkStoryClipAnalyzeThread = class(TThread)
  private
    FBoard: TZForkStoryboard;
    FClip: TZForkStoryClip;
    FGen: Integer;
    FPath: string;
    FOk: Boolean;
    FBpm: Single;
    FLengthMs: LongWord;
    FWave: array of Byte;
    procedure ApplyResult;
  protected
    procedure Execute; override;
  public
    constructor Create(ABoard: TZForkStoryboard; AClip: TZForkStoryClip;
      AGen: Integer; const APath: string);
  end;

implementation

const
  STORY_WAVE_BINS = 1024;      { Envelope-Punkte pro Clip }
  STORY_RAW_BINS  = 4096;      { Zeit-Bins aus Analyze (×3 Kick/Mid/High) }
  STORY_EDGE_PX   = 8;         { Trim-Griffbreite }

function IsAudioFile(const Path: string): Boolean;
var
  Ext: string;
begin
  Ext := LowerCase(ExtractFileExt(Path));
  if Ext = '' then Exit(False);
  { '.mp3' muss als ganze Endung matchen — nicht nur Teilstring }
  Result := (Ext = '.mp3') or (Ext = '.wav') or (Ext = '.ogg') or
    (Ext = '.flac') or (Ext = '.m4a') or (Ext = '.aac') or (Ext = '.wma') or
    (Ext = '.aiff') or (Ext = '.aif') or (Ext = '.opus') or (Ext = '.mp2') or
    (Ext = '.mp4') or (Ext = '.cda');
end;

function MkRect(L, T, R, B: Integer): TRect;
begin
  Result.Left := L;
  Result.Top := T;
  Result.Right := R;
  Result.Bottom := B;
end;

function MkPt(X, Y: Integer): TPoint;
begin
  Result.X := X;
  Result.Y := Y;
end;

{ TZForkStoryClipAnalyzeThread }

constructor TZForkStoryClipAnalyzeThread.Create(ABoard: TZForkStoryboard;
  AClip: TZForkStoryClip; AGen: Integer; const APath: string);
begin
  inherited Create(True);
  FreeOnTerminate := True;
  FBoard := ABoard;
  FClip := AClip;
  FGen := AGen;
  FPath := APath;
  FOk := False;
  FBpm := 0;
  FLengthMs := 0;
end;

procedure TZForkStoryClipAnalyzeThread.Execute;
var
  H: TZFORK_HANDLE;
  Analysis: TZFORK_ANALYSIS_HANDLE;
  Raw: array of Byte;
  Count, TimeBins, I, J, Src, Peak: Integer;
  Flags: LongWord;
  K, M, Hi: Byte;
begin
  H := zfork_Create;
  if H = nil then Exit;
  try
    SetLength(Raw, STORY_RAW_BINS * 3);
    FillChar(Raw[0], Length(Raw), 0);
    if zfork_AnalyzeFile(H, PWideChar(UnicodeString(FPath)),
         ZFORK_ANALYZE_WAVEFORM or ZFORK_ANALYZE_BPM or ZFORK_ANALYZE_LENGTH,
         @Analysis) = 0 then
      Exit;
    try
      zfork_GetAnalysisSummary(H, Analysis, @FBpm, @FLengthMs, @Flags);
      Count := Integer(zfork_CopyAnalysisWaveform(H, Analysis, @Raw[0],
        LongWord(Length(Raw))));
      if Count >= 6 then
      begin
        TimeBins := Count div 3;
        SetLength(FWave, STORY_WAVE_BINS);
        for I := 0 to STORY_WAVE_BINS - 1 do
        begin
          Src := (I * TimeBins) div STORY_WAVE_BINS;
          if Src >= TimeBins then Src := TimeBins - 1;
          J := Src * 3;
          K := Raw[J];
          M := Raw[J + 1];
          Hi := Raw[J + 2];
          Peak := K;
          if M > Peak then Peak := M;
          if Hi > Peak then Peak := Hi;
          FWave[I] := Byte(Peak);
        end;
        FOk := True;
      end;
    finally
      zfork_FreeAnalysis(H, Analysis);
    end;
  finally
    zfork_Destroy(H);
  end;
  if not Terminated and (FBoard <> nil) then
    Synchronize(@ApplyResult);
end;

procedure TZForkStoryClipAnalyzeThread.ApplyResult;
begin
  if (FBoard = nil) or (FClip = nil) then Exit;
  if FGen <> FClip.FGen then Exit;
  if FOk then
    FBoard.ApplyClipAnalysis(FClip, FWave, Length(FWave), FBpm, FLengthMs)
  else
  begin
    FClip.FAnalyzing := False;
    FBoard.Invalidate;
  end;
end;

{ TZForkStoryClip }

constructor TZForkStoryClip.Create(ACollection: TCollection);
begin
  inherited Create(ACollection);
  FTrack := stTrackA;
  FStartMs := 0;
  FLengthMs := 30000;
  FInMs := 0;
  FSourceLengthMs := 0;
  FColor := clNavy;
  FTitle := '';
  FBpm := 0;
  FBeatOffsetMs := 0;
  FAnalyzing := False;
  FGen := 0;
  SetLength(FWave, 0);
  FAutomation := TZForkClipAutomation.Create;
end;

destructor TZForkStoryClip.Destroy;
begin
  FreeAndNil(FAutomation);
  inherited Destroy;
end;

function TZForkStoryClip.GetWaveLength: Integer;
begin
  Result := Length(FWave);
end;

function TZForkStoryClip.GetDisplayName: string;
begin
  if FTitle <> '' then
    Result := FTitle
  else if FFileName <> '' then
    Result := ExtractFileName(FFileName)
  else
    Result := 'Clip ' + IntToStr(Index);
end;

function TZForkStoryClip.EndMs: LongWord;
begin
  Result := FStartMs + FLengthMs;
end;

function TZForkStoryClip.OutMs: LongWord;
begin
  Result := FInMs + FLengthMs;
end;

procedure TZForkStoryClip.SetTrack(AValue: TZForkStoryTrack);
begin
  if FTrack = AValue then Exit;
  FTrack := AValue;
  Changed(True);
end;

procedure TZForkStoryClip.SetFileName(const AValue: string);
begin
  if FFileName = AValue then Exit;
  FFileName := AValue;
  if FTitle = '' then
    FTitle := ExtractFileName(AValue);
  Changed(True);
end;

procedure TZForkStoryClip.SetStartMs(AValue: LongWord);
begin
  if FStartMs = AValue then Exit;
  FStartMs := AValue;
  Changed(True);
end;

procedure TZForkStoryClip.SetLengthMs(AValue: LongWord);
begin
  if AValue < 500 then AValue := 500;
  if FLengthMs = AValue then Exit;
  FLengthMs := AValue;
  Changed(True);
end;

procedure TZForkStoryClip.SetInMs(AValue: LongWord);
begin
  if FInMs = AValue then Exit;
  FInMs := AValue;
  Changed(True);
end;

procedure TZForkStoryClip.SetBeatOffsetMs(AValue: LongWord);
begin
  if FBeatOffsetMs = AValue then Exit;
  FBeatOffsetMs := AValue;
  Changed(True);
end;

procedure TZForkStoryClip.SetColor(AValue: TColor);
begin
  if FColor = AValue then Exit;
  FColor := AValue;
  Changed(True);
end;

procedure TZForkStoryClip.SetTitle(const AValue: string);
begin
  if FTitle = AValue then Exit;
  FTitle := AValue;
  Changed(True);
end;

{ TZForkStoryClips }

function TZForkStoryClips.GetItem(Index: Integer): TZForkStoryClip;
begin
  Result := TZForkStoryClip(inherited Items[Index]);
end;

procedure TZForkStoryClips.SetItem(Index: Integer; AValue: TZForkStoryClip);
begin
  inherited Items[Index] := AValue;
end;

procedure TZForkStoryClips.Update(Item: TCollectionItem);
begin
  inherited Update(Item);
  if Owner is TZForkStoryboard then
    TZForkStoryboard(Owner).Invalidate;
end;

{ TZForkStoryboard }

constructor TZForkStoryboard.Create(AOwner: TComponent);
begin
  inherited Create(AOwner);
  ControlStyle := ControlStyle + [csOpaque, csAcceptsControls, csDisplayDragImage];
  Width := 640;
  Height := 190;
  FActive := True;
  FPlaying := False;
  FProgramMs := 0;
  FDurationMs := 10 * 60 * 1000; { 10 Min }
  FPixelsPerMinute := 80;
  FLabelWidth := 36;
  FRulerHeight := 22;
  FTrackHeight := 44;
  FBackColor := RGBToColor(24, 24, 28);
  FRulerColor := RGBToColor(40, 40, 48);
  FGridColor := RGBToColor(60, 60, 70);
  FPlayheadColor := clWhite;
  FDuckOnC := True;
  FDuckPercent := 25;
  FAutoBeatmatch := True;
  FMaxTempoPct := 8;
  FShowBeatGrid := True;
  FWheelZoom := True;
  FShowAutomation := True;
  FEditAutoParam := apVolume;
  FCtxClip := -1;
  FDragAutoPt := -1;
  FSavedVolA := 100;
  FSavedVolB := 100;
  FDucking := False;
  FAbout := '(About…)';
  FScrollX := 0;
  FUpdatingScroll := False;
  FDragClip := -1;
  FDragMode := sdmNone;
  FActiveClipA := -1;
  FActiveClipB := -1;
  FActiveClipC := -1;
  FClips := TZForkStoryClips.Create(Self, TZForkStoryClip);
  FTimer := TTimer.Create(Self);
  FTimer.Enabled := False;
  FTimer.Interval := 50;
  FTimer.OnTimer := @TimerTick;
  FHscroll := TScrollBar.Create(Self);
  FHscroll.Parent := Self;
  FHscroll.Kind := sbHorizontal;
  FHscroll.Align := alBottom;
  FHscroll.Height := 16;
  FHscroll.TabStop := False;
  FHscroll.OnChange := @HScrollChange;
  FAutoColors := TZForkStoryAutoColors.Create(Self, @AutoColorsChanged);
  BuildAutoPopup;
  Color := FBackColor;
  DoubleBuffered := True;
  TabStop := True;
end;

destructor TZForkStoryboard.Destroy;
begin
  FTimer.Enabled := False;
  FreeAndNil(FPopup);
  FreeAndNil(FAutoColors);
  FreeAndNil(FClips);
  inherited Destroy;
end;

function TZForkStoryboard.GetAbout: string;
begin
  Result := FAbout;
end;

procedure TZForkStoryboard.SetAbout(const AValue: string);
begin
  ShowAbout;
end;

procedure TZForkStoryboard.ShowAbout;
begin
  ShowZForkAbout;
end;

procedure TZForkStoryboard.Changed;
begin
  if Assigned(FOnChange) then FOnChange(Self);
end;

procedure TZForkStoryboard.Loaded;
begin
  inherited Loaded;
  UpdateTimer;
  if HandleAllocated and not (csDesigning in ComponentState) then
    DragAcceptFiles(Handle, True);
end;

procedure TZForkStoryboard.CreateWnd;
begin
  inherited CreateWnd;
  if not (csDesigning in ComponentState) then
    DragAcceptFiles(Handle, True);
end;

procedure TZForkStoryboard.DestroyWnd;
begin
  if HandleAllocated and not (csDesigning in ComponentState) then
    DragAcceptFiles(Handle, False);
  inherited DestroyWnd;
end;

procedure TZForkStoryboard.HandleHDROP(Drop: HDROP);
var
  N, I, Len: Integer;
  Buf: array[0..4095] of WideChar;
  Names: array of string;
  Pt: TPoint;
begin
  if Drop = 0 then Exit;
  N := Integer(DragQueryFileW(Drop, $FFFFFFFF, nil, 0));
  if N <= 0 then
  begin
    DragFinish(Drop);
    Exit;
  end;
  SetLength(Names, N);
  for I := 0 to N - 1 do
  begin
    FillChar(Buf, SizeOf(Buf), 0);
    Len := Integer(DragQueryFileW(Drop, I, @Buf[0], Length(Buf)));
    if Len > 0 then
      Names[I] := string(UnicodeString(PWideChar(@Buf[0])))
    else
      Names[I] := '';
  end;
  Pt.X := 0;
  Pt.Y := 0;
  DragQueryPoint(Drop, @Pt);
  DragFinish(Drop);
  ProcessDroppedFiles(Names, Pt.X, Pt.Y);
end;

procedure TZForkStoryboard.WndProc(var Message: TLMessage);
begin
  if (Message.Msg = WM_DROPFILES) and not (csDesigning in ComponentState) then
  begin
    HandleHDROP(HDROP(Message.WParam));
    Message.Result := 0;
    Exit;
  end;
  inherited WndProc(Message);
end;

procedure TZForkStoryboard.DropFilesAt(const FileNames: array of string; X, Y: Integer);
begin
  ProcessDroppedFiles(FileNames, X, Y);
end;

function TZForkStoryboard.ClientHitTest(X, Y: Integer): Boolean;
begin
  Result := (X >= 0) and (Y >= 0) and (X < Width) and (Y < Height);
end;

procedure TZForkStoryboard.ProcessDroppedFiles(const FileNames: array of string; X, Y: Integer);
var
  I, T, Added: Integer;
begin
  T := HitTrack(Y);
  if T < 0 then
  begin
    if Y < FRulerHeight then
      T := 0
    else
      T := 0;
  end;
  Added := 0;
  for I := 0 to High(FileNames) do
  begin
    if (FileNames[I] = '') or (not FileExists(FileNames[I])) then Continue;
    if not IsAudioFile(FileNames[I]) then Continue;
    if AddFile(TZForkStoryTrack(T), FileNames[I], XToMs(X)) <> nil then
      Inc(Added);
  end;
  if Added > 0 then
    Invalidate;
end;

procedure TZForkStoryboard.Notification(AComponent: TComponent; Operation: TOperation);
begin
  inherited Notification(AComponent, Operation);
  if Operation = opRemove then
  begin
    if AComponent = FPlayerA then FPlayerA := nil;
    if AComponent = FPlayerB then FPlayerB := nil;
    if AComponent = FPlayerC then FPlayerC := nil;
  end;
end;

procedure TZForkStoryboard.SetPlayerA(AValue: TZForkPlayer);
begin
  if FPlayerA = AValue then Exit;
  if FPlayerA <> nil then FPlayerA.RemoveFreeNotification(Self);
  FPlayerA := AValue;
  if FPlayerA <> nil then FPlayerA.FreeNotification(Self);
end;

procedure TZForkStoryboard.SetPlayerB(AValue: TZForkPlayer);
begin
  if FPlayerB = AValue then Exit;
  if FPlayerB <> nil then FPlayerB.RemoveFreeNotification(Self);
  FPlayerB := AValue;
  if FPlayerB <> nil then FPlayerB.FreeNotification(Self);
end;

procedure TZForkStoryboard.SetPlayerC(AValue: TZForkPlayer);
begin
  if FPlayerC = AValue then Exit;
  if FPlayerC <> nil then FPlayerC.RemoveFreeNotification(Self);
  FPlayerC := AValue;
  if FPlayerC <> nil then FPlayerC.FreeNotification(Self);
end;

procedure TZForkStoryboard.SetClips(AValue: TZForkStoryClips);
begin
  FClips.Assign(AValue);
end;

procedure TZForkStoryboard.SetActive(AValue: Boolean);
begin
  if FActive = AValue then Exit;
  FActive := AValue;
  UpdateTimer;
  Invalidate;
end;

procedure TZForkStoryboard.SetDurationMs(AValue: LongWord);
begin
  if AValue < 60000 then AValue := 60000;
  if FDurationMs = AValue then Exit;
  FDurationMs := AValue;
  UpdateHScroll;
  Invalidate;
end;

procedure TZForkStoryboard.SetPixelsPerMinute(AValue: Integer);
begin
  if AValue < 20 then AValue := 20;
  if AValue > 12000 then AValue := 12000;
  if FPixelsPerMinute = AValue then Exit;
  FPixelsPerMinute := AValue;
  UpdateHScroll;
  Invalidate;
end;

procedure TZForkStoryboard.SetBackColor(AValue: TColor);
begin
  if FBackColor = AValue then Exit;
  FBackColor := AValue;
  Color := AValue;
  Invalidate;
end;

procedure TZForkStoryboard.SetDuckOnC(AValue: Boolean);
begin
  FDuckOnC := AValue;
end;

procedure TZForkStoryboard.SetDuckPercent(AValue: Integer);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 100 then AValue := 100;
  FDuckPercent := AValue;
end;

procedure TZForkStoryboard.SetAutoBeatmatch(AValue: Boolean);
begin
  FAutoBeatmatch := AValue;
end;

procedure TZForkStoryboard.SetMaxTempoPct(AValue: Integer);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 20 then AValue := 20;
  FMaxTempoPct := AValue;
end;

procedure TZForkStoryboard.SetShowBeatGrid(AValue: Boolean);
begin
  if FShowBeatGrid = AValue then Exit;
  FShowBeatGrid := AValue;
  Invalidate;
end;

procedure TZForkStoryboard.SetWheelZoom(AValue: Boolean);
begin
  FWheelZoom := AValue;
end;

procedure TZForkStoryboard.SetShowAutomation(AValue: Boolean);
begin
  if FShowAutomation = AValue then Exit;
  FShowAutomation := AValue;
  Invalidate;
end;

procedure TZForkStoryboard.SetEditAutoParam(AValue: TZForkAutoParam);
begin
  if FEditAutoParam = AValue then Exit;
  FEditAutoParam := AValue;
  Invalidate;
end;

procedure TZForkStoryboard.SetAutoColors(AValue: TZForkStoryAutoColors);
begin
  if AValue <> nil then
    FAutoColors.Assign(AValue);
end;

procedure TZForkStoryboard.AutoColorsChanged(Sender: TObject);
begin
  Invalidate;
end;

procedure TZForkStoryboard.ClipAutoChanged(Sender: TObject);
begin
  Invalidate;
  Changed;
end;

procedure TZForkStoryboard.UpdateTimer;
begin
  if csDesigning in ComponentState then
  begin
    FTimer.Enabled := False;
    Exit;
  end;
  FTimer.Enabled := FActive and FPlaying;
end;

function TZForkStoryboard.ContentWidth: Integer;
begin
  Result := FLabelWidth + Round((FDurationMs / 60000.0) * FPixelsPerMinute) + 40;
end;

function TZForkStoryboard.TracksBottom: Integer;
begin
  Result := Height;
  if (FHscroll <> nil) and FHscroll.Visible then
    Result := Height - FHscroll.Height;
end;

procedure TZForkStoryboard.ClampScrollX;
var
  MaxS: Integer;
begin
  MaxS := Max(0, ContentWidth - Width);
  if FScrollX < 0 then FScrollX := 0;
  if FScrollX > MaxS then FScrollX := MaxS;
end;

procedure TZForkStoryboard.UpdateHScroll;
begin
  ClampScrollX;
  if FHscroll = nil then Exit;
  FUpdatingScroll := True;
  try
    FHscroll.Min := 0;
    FHscroll.Max := Max(0, ContentWidth - Width);
    FHscroll.Enabled := FHscroll.Max > 0;
    if FHscroll.Position <> FScrollX then
      FHscroll.Position := FScrollX;
  finally
    FUpdatingScroll := False;
  end;
end;

procedure TZForkStoryboard.HScrollChange(Sender: TObject);
begin
  if FUpdatingScroll or (FHscroll = nil) then Exit;
  FScrollX := FHscroll.Position;
  Invalidate;
end;

procedure TZForkStoryboard.Resize;
begin
  inherited Resize;
  UpdateHScroll;
end;

function TZForkStoryboard.RulerRect: TRect;
begin
  Result := MkRect(0, 0, Self.Width, FRulerHeight);
end;

function TZForkStoryboard.TrackRect(Track: TZForkStoryTrack): TRect;
var
  Y: Integer;
begin
  Y := FRulerHeight + Ord(Track) * FTrackHeight;
  Result := MkRect(0, Y, Self.Width, Min(Y + FTrackHeight, TracksBottom));
end;

function TZForkStoryboard.MsToX(Ms: LongWord): Integer;
begin
  Result := FLabelWidth + Round((Ms / 60000.0) * FPixelsPerMinute) - FScrollX;
end;

function TZForkStoryboard.XToMs(X: Integer): LongWord;
var
  V: Double;
begin
  V := ((X + FScrollX - FLabelWidth) / Max(1, FPixelsPerMinute)) * 60000.0;
  if V < 0 then V := 0;
  if V > FDurationMs then V := FDurationMs;
  Result := Round(V);
end;

function TZForkStoryboard.HitTrack(Y: Integer): Integer;
var
  T: Integer;
begin
  Result := -1;
  if Y < FRulerHeight then Exit;
  if Y >= TracksBottom then Exit;
  T := (Y - FRulerHeight) div Max(1, FTrackHeight);
  if (T >= 0) and (T <= 2) then Result := T;
end;

function TZForkStoryboard.HitClip(X, Y: Integer): Integer;
var
  I, Tr: Integer;
  C: TZForkStoryClip;
  R: TRect;
  X0, X1: Integer;
begin
  Result := -1;
  Tr := HitTrack(Y);
  if Tr < 0 then Exit;
  for I := 0 to FClips.Count - 1 do
  begin
    C := FClips[I];
    if Ord(C.Track) <> Tr then Continue;
    X0 := MsToX(C.StartMs);
    X1 := MsToX(C.EndMs);
    R := MkRect(X0, TrackRect(C.Track).Top + 4, X1, TrackRect(C.Track).Bottom - 4);
    if PtInRect(R, MkPt(X, Y)) then Exit(I);
  end;
end;

function TZForkStoryboard.HitClipEdge(ClipIndex, X: Integer): TZForkStoryDragMode;
var
  C: TZForkStoryClip;
  X0, X1: Integer;
begin
  Result := sdmMove;
  if (ClipIndex < 0) or (ClipIndex >= FClips.Count) then Exit(sdmNone);
  C := FClips[ClipIndex];
  X0 := MsToX(C.StartMs);
  X1 := MsToX(C.EndMs);
  if Abs(X - X0) <= STORY_EDGE_PX then Exit(sdmTrimLeft);
  if Abs(X - X1) <= STORY_EDGE_PX then Exit(sdmTrimRight);
end;

function TZForkStoryboard.DefaultColor(Track: TZForkStoryTrack): TColor;
begin
  case Track of
    stTrackA: Result := RGBToColor(180, 40, 80);
    stTrackB: Result := RGBToColor(40, 100, 200);
  else
    Result := RGBToColor(220, 140, 30); { Werbung }
  end;
end;

function TZForkStoryboard.PlayerFor(Track: TZForkStoryTrack): TZForkPlayer;
begin
  case Track of
    stTrackA: Result := FPlayerA;
    stTrackB: Result := FPlayerB;
  else
    Result := FPlayerC;
  end;
end;

function TZForkStoryboard.ProbeLengthMs(Player: TZForkPlayer; const Path: string): LongWord;
var
  Info: TStreamInfo;
begin
  Result := 30000;
  if (Player = nil) or (Path = '') then Exit;
  if not Player.OpenFile(Path) then Exit;
  FillChar(Info, SizeOf(Info), 0);
  if Player.GetStreamInfo(0, Info) and (Info.length.ms > 0) then
    Result := Info.length.ms
  else if Info.length.sec > 0 then
    Result := Info.length.sec * 1000;
  Player.Stop;
end;

procedure TZForkStoryboard.EnsureDurationForClip(Clip: TZForkStoryClip);
begin
  if Clip = nil then Exit;
  if Clip.EndMs + 30000 > FDurationMs then
  begin
    FDurationMs := Clip.EndMs + 60000;
    UpdateHScroll;
  end;
end;

function TZForkStoryboard.AddFile(Track: TZForkStoryTrack; const Path: string;
  StartMs: LongWord): TZForkStoryClip;
var
  P: TZForkPlayer;
  LastEnd: LongWord;
  I: Integer;
  C: TZForkStoryClip;
begin
  Result := nil;
  if not FileExists(Path) then Exit;
  Result := TZForkStoryClip(FClips.Add);
  Result.Track := Track;
  Result.FileName := Path;
  Result.Title := ExtractFileName(Path);
  Result.Color := DefaultColor(Track);
  P := PlayerFor(Track);
  Result.LengthMs := ProbeLengthMs(P, Path);
  if StartMs = High(LongWord) then
  begin
    LastEnd := 0;
    for I := 0 to FClips.Count - 1 do
    begin
      C := FClips[I];
      if (C <> Result) and (C.Track = Track) and (C.EndMs > LastEnd) then
        LastEnd := C.EndMs;
    end;
    Result.StartMs := LastEnd;
  end
  else
    Result.StartMs := StartMs;
  EnsureDurationForClip(Result);
  Result.Automation.OnChange := @ClipAutoChanged;
  StartAnalyzeClip(Result);
  Invalidate;
  Changed;
end;

procedure TZForkStoryboard.StartAnalyzeClip(Clip: TZForkStoryClip);
var
  Th: TZForkStoryClipAnalyzeThread;
begin
  if (Clip = nil) or (Clip.FileName = '') then Exit;
  if csDesigning in ComponentState then Exit;
  Inc(Clip.FGen);
  Clip.FAnalyzing := True;
  SetLength(Clip.FWave, 0);
  Th := TZForkStoryClipAnalyzeThread.Create(Self, Clip, Clip.FGen, Clip.FileName);
  Th.Start;
end;

procedure TZForkStoryboard.ApplyClipAnalysis(Clip: TZForkStoryClip;
  const Wave: array of Byte; WaveCount: Integer; Bpm: Single; LengthMs: LongWord);
var
  I: Integer;
begin
  if Clip = nil then Exit;
  SetLength(Clip.FWave, WaveCount);
  for I := 0 to WaveCount - 1 do
    Clip.FWave[I] := Wave[I];
  if Bpm > 40 then Clip.FBpm := Bpm;
  if LengthMs > 500 then
  begin
    if Clip.FSourceLengthMs = 0 then
    begin
      Clip.FSourceLengthMs := LengthMs;
      Clip.FInMs := 0;
      Clip.FLengthMs := LengthMs;
    end
    else
    begin
      Clip.FSourceLengthMs := LengthMs;
      if Clip.FInMs >= Clip.FSourceLengthMs then
        Clip.FInMs := 0;
      if Clip.FInMs + Clip.FLengthMs > Clip.FSourceLengthMs then
      begin
        if Clip.FSourceLengthMs > Clip.FInMs + 500 then
          Clip.FLengthMs := Clip.FSourceLengthMs - Clip.FInMs
        else
          Clip.FLengthMs := 500;
      end;
    end;
  end;
  Clip.FAnalyzing := False;
  EnsureDurationForClip(Clip);
  Invalidate;
  Changed;
end;

procedure TZForkStoryboard.ClearClips;
begin
  FClips.Clear;
  FActiveClipA := -1;
  FActiveClipB := -1;
  FActiveClipC := -1;
  Invalidate;
  Changed;
end;

procedure TZForkStoryboard.Play;
begin
  if csDesigning in ComponentState then Exit;
  FPlaying := True;
  FPlayStartTick := GetTickCount64;
  FPlayStartMs := FProgramMs;
  UpdateTimer;
  SyncActiveClips;
  Invalidate;
end;

procedure TZForkStoryboard.Pause;
begin
  FPlaying := False;
  UpdateTimer;
  if FPlayerA <> nil then FPlayerA.Pause;
  if FPlayerB <> nil then FPlayerB.Pause;
  if FPlayerC <> nil then FPlayerC.Pause;
end;

procedure TZForkStoryboard.Stop;
begin
  FPlaying := False;
  UpdateTimer;
  StopTrackPlayer(stTrackA);
  StopTrackPlayer(stTrackB);
  StopTrackPlayer(stTrackC);
  if FPlayerA <> nil then FPlayerA.SetTempo(1.0);
  if FPlayerB <> nil then FPlayerB.SetTempo(1.0);
  if FPlayerC <> nil then FPlayerC.SetTempo(1.0);
  ApplyDuck(False);
  FActiveClipA := -1;
  FActiveClipB := -1;
  FActiveClipC := -1;
  FProgramMs := 0;
  Invalidate;
end;

procedure TZForkStoryboard.SeekProgram(Ms: LongWord);
begin
  if Ms > FDurationMs then Ms := FDurationMs;
  FProgramMs := Ms;
  FPlayStartTick := GetTickCount64;
  FPlayStartMs := Ms;
  if FPlaying then
  begin
    FActiveClipA := -1;
    FActiveClipB := -1;
    FActiveClipC := -1;
    SyncActiveClips;
  end;
  Invalidate;
  if Assigned(FOnProgramTime) then FOnProgramTime(Self);
end;

procedure TZForkStoryboard.StopTrackPlayer(Track: TZForkStoryTrack);
var
  P: TZForkPlayer;
begin
  P := PlayerFor(Track);
  if P <> nil then P.Stop;
end;

procedure TZForkStoryboard.ApplyDuck(WantDuck: Boolean);
begin
  if not FDuckOnC then WantDuck := False;
  if WantDuck = FDucking then Exit;
  if WantDuck then
  begin
    if FPlayerA <> nil then
    begin
      FSavedVolA := FPlayerA.MasterVolumeLeft;
      FPlayerA.MasterVolumeLeft := Max(0, (FSavedVolA * FDuckPercent) div 100);
      FPlayerA.MasterVolumeRight := FPlayerA.MasterVolumeLeft;
    end;
    if FPlayerB <> nil then
    begin
      FSavedVolB := FPlayerB.MasterVolumeLeft;
      FPlayerB.MasterVolumeLeft := Max(0, (FSavedVolB * FDuckPercent) div 100);
      FPlayerB.MasterVolumeRight := FPlayerB.MasterVolumeLeft;
    end;
  end
  else
  begin
    if FPlayerA <> nil then
    begin
      FPlayerA.MasterVolumeLeft := FSavedVolA;
      FPlayerA.MasterVolumeRight := FSavedVolA;
    end;
    if FPlayerB <> nil then
    begin
      FPlayerB.MasterVolumeLeft := FSavedVolB;
      FPlayerB.MasterVolumeRight := FSavedVolB;
    end;
  end;
  FDucking := WantDuck;
end;

procedure TZForkStoryboard.StartClipOnPlayer(ClipIndex: Integer);
var
  C: TZForkStoryClip;
  P: TZForkPlayer;
  Offset: LongWord;
begin
  if (ClipIndex < 0) or (ClipIndex >= FClips.Count) then Exit;
  C := FClips[ClipIndex];
  P := PlayerFor(C.Track);
  if (P = nil) or (C.FileName = '') then Exit;
  if not FileExists(C.FileName) then Exit;
  if not P.OpenFile(C.FileName) then Exit;
  ApplyBeatmatchTempo(C, P);
  Offset := C.InMs;
  if FProgramMs > C.StartMs then
    Offset := Offset + (FProgramMs - C.StartMs);
  if Offset > 0 then
    P.SeekMs(ZFORK_SEEK_BEGIN, Offset);
  if FProgramMs >= C.StartMs then
    ApplyClipAutomation(C, P, FProgramMs - C.StartMs)
  else
    ApplyClipAutomation(C, P, 0);
  P.Play;
end;

function TZForkStoryboard.ReferenceBpm(ForClip: TZForkStoryClip): Single;
var
  I: Integer;
  C: TZForkStoryClip;
  Best: Single;
begin
  Result := 0;
  if ForClip = nil then Exit;
  { 1) Andere Spur, die gerade spielt }
  for I := 0 to FClips.Count - 1 do
  begin
    C := FClips[I];
    if C = ForClip then Continue;
    if C.Track = ForClip.Track then Continue;
    if C.Track = stTrackC then Continue; { Werbung kein Tempo-Master }
    if ForClip.Track = stTrackC then Continue;
    if (FProgramMs >= C.StartMs) and (FProgramMs < C.EndMs) and (C.Bpm > 40) then
      Exit(C.Bpm);
  end;
  { 2) Clip, der kurz vorher auf anderer Musik-Spur endet }
  Best := 0;
  for I := 0 to FClips.Count - 1 do
  begin
    C := FClips[I];
    if C = ForClip then Continue;
    if C.Track = stTrackC then Continue;
    if ForClip.Track = stTrackC then Continue;
    if C.Track = ForClip.Track then Continue;
    if (C.EndMs <= ForClip.StartMs + 2000) and (C.EndMs + 120000 >= ForClip.StartMs)
       and (C.Bpm > 40) then
      Best := C.Bpm;
  end;
  Result := Best;
end;

procedure TZForkStoryboard.ApplyBeatmatchTempo(Clip: TZForkStoryClip; Player: TZForkPlayer);
var
  Ref, Tempo, Lo, Hi: Single;
begin
  if (Player = nil) or (Clip = nil) then Exit;
  if (not FAutoBeatmatch) or (Clip.Track = stTrackC) then
  begin
    Player.SetTempo(1.0);
    Exit;
  end;
  Ref := ReferenceBpm(Clip);
  if (Ref < 40) or (Clip.Bpm < 40) then
  begin
    Player.SetTempo(1.0);
    Exit;
  end;
  Tempo := Ref / Clip.Bpm;
  { Halbe/Doppelte BPMs angleichen }
  if Abs(Tempo - 2.0) < Abs(Tempo - 1.0) then Tempo := Tempo / 2.0;
  if Abs(Tempo - 0.5) < Abs(Tempo - 1.0) then Tempo := Tempo * 2.0;
  Lo := 1.0 - FMaxTempoPct / 100.0;
  Hi := 1.0 + FMaxTempoPct / 100.0;
  if Tempo < Lo then Tempo := Lo;
  if Tempo > Hi then Tempo := Hi;
  Player.SetTempo(Tempo);
end;

procedure TZForkStoryboard.SyncActiveClips;
var
  I: Integer;
  C: TZForkStoryClip;
  WantA, WantB, WantC: Integer;
  NeedDuck: Boolean;
begin
  WantA := -1;
  WantB := -1;
  WantC := -1;
  for I := 0 to FClips.Count - 1 do
  begin
    C := FClips[I];
    if (FProgramMs >= C.StartMs) and (FProgramMs < C.EndMs) then
      case C.Track of
        stTrackA: WantA := I;
        stTrackB: WantB := I;
        stTrackC: WantC := I;
      end;
  end;

  if WantA <> FActiveClipA then
  begin
    if WantA < 0 then StopTrackPlayer(stTrackA) else StartClipOnPlayer(WantA);
    FActiveClipA := WantA;
  end;
  if WantB <> FActiveClipB then
  begin
    if WantB < 0 then StopTrackPlayer(stTrackB) else StartClipOnPlayer(WantB);
    FActiveClipB := WantB;
  end;
  if WantC <> FActiveClipC then
  begin
    if WantC < 0 then StopTrackPlayer(stTrackC) else StartClipOnPlayer(WantC);
    FActiveClipC := WantC;
  end;

  NeedDuck := WantC >= 0;
  ApplyDuck(NeedDuck);

  { Automation kontinuierlich nachziehen }
  if WantA >= 0 then
    ApplyClipAutomation(FClips[WantA], FPlayerA, FProgramMs - FClips[WantA].StartMs);
  if WantB >= 0 then
    ApplyClipAutomation(FClips[WantB], FPlayerB, FProgramMs - FClips[WantB].StartMs);
  if WantC >= 0 then
    ApplyClipAutomation(FClips[WantC], FPlayerC, FProgramMs - FClips[WantC].StartMs);
end;

procedure TZForkStoryboard.TimerTick(Sender: TObject);
var
  Elapsed: QWord;
begin
  if not FPlaying then Exit;
  Elapsed := GetTickCount64 - FPlayStartTick;
  FProgramMs := FPlayStartMs + LongWord(Elapsed);
  if FProgramMs >= FDurationMs then
  begin
    FProgramMs := FDurationMs;
    Pause;
  end;
  SyncActiveClips;
  Invalidate;
  if Assigned(FOnProgramTime) then FOnProgramTime(Self);
end;

function MixRGBDark(C: TColor): TColor;
var
  R, G, B: Byte;
begin
  RedGreenBlue(ColorToRGB(C), R, G, B);
  Result := RGBToColor(R div 2, G div 2, B div 2);
end;

procedure TZForkStoryboard.DrawRuler(ACanvas: TCanvas);
var
  R: TRect;
  MinIdx, MaxMin, X, M: Integer;
  LabelStr: string;
begin
  R := RulerRect;
  ACanvas.Brush.Color := FRulerColor;
  ACanvas.FillRect(R);
  ACanvas.Font.Color := clSilver;
  ACanvas.Font.Height := -10;
  ACanvas.Brush.Style := bsClear;
  MaxMin := Ceil(FDurationMs / 60000.0);
  MinIdx := Max(0, (FScrollX) div Max(1, FPixelsPerMinute) - 1);
  for M := MinIdx to MaxMin do
  begin
    X := MsToX(LongWord(M) * 60000);
    if (X < FLabelWidth - 2) or (X > Width) then Continue;
    ACanvas.Pen.Color := FGridColor;
    ACanvas.MoveTo(X, R.Bottom - 6);
    ACanvas.LineTo(X, R.Bottom);
    LabelStr := Format('%d:00', [M]);
    ACanvas.TextOut(X + 2, 4, LabelStr);
  end;
  ACanvas.Brush.Style := bsSolid;
end;

procedure TZForkStoryboard.DrawTrack(ACanvas: TCanvas; Track: TZForkStoryTrack);
var
  R, CR: TRect;
  I, X0, X1: Integer;
  C: TZForkStoryClip;
  Lbl: string;
  TW: Integer;
begin
  R := TrackRect(Track);
  if Odd(Ord(Track)) then
    ACanvas.Brush.Color := RGBToColor(28, 28, 32)
  else
    ACanvas.Brush.Color := FBackColor;
  ACanvas.FillRect(R);

  ACanvas.Brush.Color := RGBToColor(36, 36, 42);
  ACanvas.FillRect(R.Left, R.Top, FLabelWidth, R.Bottom);
  case Track of
    stTrackA: Lbl := 'A';
    stTrackB: Lbl := 'B';
  else
    Lbl := 'C';
  end;
  ACanvas.Font.Color := clWhite;
  ACanvas.Font.Style := [fsBold];
  ACanvas.Font.Height := -14;
  ACanvas.Brush.Style := bsClear;
  TW := ACanvas.TextWidth(Lbl);
  ACanvas.TextOut((FLabelWidth - TW) div 2, R.Top + (FTrackHeight - 14) div 2, Lbl);
  ACanvas.Font.Style := [];
  ACanvas.Brush.Style := bsSolid;

  { Minuten-Gitter }
  ACanvas.Pen.Color := FGridColor;
  for I := 0 to Ceil(FDurationMs / 60000.0) do
  begin
    X0 := MsToX(LongWord(I) * 60000);
    if (X0 >= FLabelWidth) and (X0 < Width) then
    begin
      ACanvas.MoveTo(X0, R.Top);
      ACanvas.LineTo(X0, R.Bottom);
    end;
  end;

  for I := 0 to FClips.Count - 1 do
  begin
    C := FClips[I];
    if C.Track <> Track then Continue;
    X0 := MsToX(C.StartMs);
    X1 := MsToX(C.EndMs);
    if X1 <= FLabelWidth then Continue;
    if X0 >= Width then Continue;
    if X0 < FLabelWidth then X0 := FLabelWidth;
    CR := MkRect(X0, R.Top + 4, Max(X0 + 4, X1), R.Bottom - 4);
    ACanvas.Brush.Color := MixRGBDark(C.Color);
    ACanvas.Pen.Color := clWhite;
    ACanvas.Rectangle(CR);
    DrawClipBeatGrid(ACanvas, CR, C);
    DrawClipWave(ACanvas, CR, C);
    DrawClipAutomation(ACanvas, CR, C);
    DrawClipTrimHandles(ACanvas, CR);
    ACanvas.Brush.Style := bsClear;
    ACanvas.Font.Color := clWhite;
    ACanvas.Font.Height := -10;
    if C.Bpm > 40 then
      ACanvas.TextRect(CR, CR.Left + 4, CR.Top + 2,
        Format('%s  %.0f BPM', [C.DisplayName, C.Bpm]))
    else if C.Analyzing then
      ACanvas.TextRect(CR, CR.Left + 4, CR.Top + 2, C.DisplayName + ' …')
    else
      ACanvas.TextRect(CR, CR.Left + 4, CR.Top + 2, C.DisplayName);
    ACanvas.Brush.Style := bsSolid;
  end;
end;

procedure TZForkStoryboard.DrawClipWave(ACanvas: TCanvas; const CR: TRect;
  Clip: TZForkStoryClip);
var
  W, H, MidY, X, Idx, Amp, Y0, Y1, N, I0, I1: Integer;
  WaveCol: TColor;
  R, G, B: Byte;
  SrcLen: LongWord;
begin
  if (Clip = nil) or (Length(Clip.FWave) < 4) then Exit;
  W := CR.Right - CR.Left;
  H := CR.Bottom - CR.Top;
  if (W < 4) or (H < 6) then Exit;
  MidY := (CR.Top + CR.Bottom) div 2;
  N := Length(Clip.FWave);
  SrcLen := Clip.FSourceLengthMs;
  if SrcLen = 0 then SrcLen := Clip.FLengthMs + Clip.FInMs;
  if SrcLen = 0 then SrcLen := 1;
  I0 := Integer((QWord(Clip.FInMs) * LongWord(N)) div SrcLen);
  I1 := Integer((QWord(Clip.OutMs) * LongWord(N)) div SrcLen);
  if I1 <= I0 then I1 := I0 + 1;
  if I1 > N then I1 := N;
  if I0 < 0 then I0 := 0;
  RedGreenBlue(ColorToRGB(Clip.Color), R, G, B);
  WaveCol := RGBToColor(Min(255, R + 80), Min(255, G + 80), Min(255, B + 80));
  ACanvas.Pen.Color := WaveCol;
  ACanvas.Pen.Width := 1;
  for X := 0 to W - 1 do
  begin
    Idx := I0 + ((X * (I1 - I0)) div W);
    if Idx >= N then Idx := N - 1;
    if Idx < 0 then Idx := 0;
    Amp := (Integer(Clip.FWave[Idx]) * (H div 2 - 1)) div 255;
    if Amp < 1 then Continue;
    Y0 := MidY - Amp;
    Y1 := MidY + Amp;
    ACanvas.MoveTo(CR.Left + X, Y0);
    ACanvas.LineTo(CR.Left + X, Y1);
  end;
end;

procedure TZForkStoryboard.DrawClipBeatGrid(ACanvas: TCanvas; const CR: TRect;
  Clip: TZForkStoryClip);
var
  MidY, YHi, YLo, BeatMs, T, X, Bar, Step, PxBeat: Integer;
  Rel: LongWord;
begin
  if (Clip = nil) or not FShowBeatGrid then Exit;
  MidY := (CR.Top + CR.Bottom) div 2;
  YHi := CR.Top + (CR.Bottom - CR.Top) div 4;
  YLo := CR.Bottom - (CR.Bottom - CR.Top) div 4;
  { horizontale Hilfslinien }
  ACanvas.Pen.Color := RGBToColor(90, 90, 100);
  ACanvas.Pen.Width := 1;
  ACanvas.MoveTo(CR.Left + 1, MidY);
  ACanvas.LineTo(CR.Right - 1, MidY);
  ACanvas.Pen.Color := RGBToColor(55, 55, 65);
  ACanvas.MoveTo(CR.Left + 1, YHi);
  ACanvas.LineTo(CR.Right - 1, YHi);
  ACanvas.MoveTo(CR.Left + 1, YLo);
  ACanvas.LineTo(CR.Right - 1, YLo);

  if Clip.Bpm < 40 then Exit;
  BeatMs := Max(1, Round(60000.0 / Clip.Bpm));
  { Bei Zoom-out nur Takte, sonst Beats — sonst „Streifen-Salat“ }
  PxBeat := Max(1, Round(BeatMs * FPixelsPerMinute / 60000.0));
  if PxBeat < 4 then
    Step := BeatMs * 4
  else if PxBeat < 8 then
    Step := BeatMs * 2
  else
    Step := BeatMs;

  T := Integer(Clip.FBeatOffsetMs);
  while T < Integer(Clip.FInMs) do
    Inc(T, BeatMs);
  while T < Integer(Clip.OutMs) do
  begin
    Rel := LongWord(T) - Clip.FInMs;
    X := CR.Left + Integer((QWord(Rel) * LongWord(CR.Right - CR.Left)) div Max(LongWord(1), Clip.FLengthMs));
    if (X > CR.Left) and (X < CR.Right) then
    begin
      Bar := ((T - Integer(Clip.FBeatOffsetMs)) div BeatMs) mod 4;
      if Bar = 0 then
        ACanvas.Pen.Color := RGBToColor(220, 220, 240)
      else if Step = BeatMs then
        ACanvas.Pen.Color := RGBToColor(120, 120, 140)
      else
        ACanvas.Pen.Color := RGBToColor(160, 160, 180);
      ACanvas.MoveTo(X, CR.Top + 1);
      ACanvas.LineTo(X, CR.Bottom - 1);
    end;
    Inc(T, Step);
    if T > Integer(Clip.OutMs) + Step * 4 then Break;
  end;
end;

procedure TZForkStoryboard.DrawClipTrimHandles(ACanvas: TCanvas; const CR: TRect);
begin
  if CR.Right - CR.Left < 12 then Exit;
  ACanvas.Brush.Color := RGBToColor(240, 240, 250);
  ACanvas.FillRect(MkRect(CR.Left, CR.Top, CR.Left + 3, CR.Bottom));
  ACanvas.FillRect(MkRect(CR.Right - 3, CR.Top, CR.Right, CR.Bottom));
end;

function TZForkStoryboard.ClipLocalMs(Clip: TZForkStoryClip; X: Integer): LongWord;
var
  Rel: LongWord;
begin
  if Clip = nil then Exit(0);
  Rel := 0;
  if X > MsToX(Clip.StartMs) then
    Rel := XToMs(X) - Clip.StartMs;
  if Rel > Clip.LengthMs then Rel := Clip.LengthMs;
  Result := Rel;
end;

function TZForkStoryboard.YToAutoValue(const CR: TRect; Y: Integer): Single;
var
  H: Integer;
begin
  H := CR.Bottom - CR.Top;
  if H <= 1 then Exit(0.5);
  Result := Clamp01(1.0 - (Y - CR.Top) / Single(H));
end;

function TZForkStoryboard.AutoValueToY(const CR: TRect; Value: Single): Integer;
begin
  Result := CR.Top + Round((1.0 - Clamp01(Value)) * (CR.Bottom - CR.Top));
end;

procedure TZForkStoryboard.DrawClipAutomation(ACanvas: TCanvas; const CR: TRect;
  Clip: TZForkStoryClip);
var
  P: TZForkAutoParam;
  Curve: TZForkAutoCurve;
  I, X0, Y0, X1, Y1, W: Integer;
  Pt: TZForkAutoPoint;
  Col: TColor;
  Def: Single;
begin
  if (not FShowAutomation) or (Clip = nil) or (Clip.Automation = nil) then Exit;
  W := CR.Right - CR.Left;
  if W < 8 then Exit;

  for P := Low(TZForkAutoParam) to High(TZForkAutoParam) do
  begin
    Curve := Clip.Automation.Curve(P);
    if Curve.Count = 0 then Continue;
    Col := FAutoColors.ColorOf(P);
    ACanvas.Pen.Color := Col;
    if P = FEditAutoParam then
      ACanvas.Pen.Width := 2
    else
      ACanvas.Pen.Width := 1;

    Def := AutoParamDefault(P);
    X0 := CR.Left;
    Y0 := AutoValueToY(CR, Curve.EvalAt(0, Def));
    ACanvas.MoveTo(X0, Y0);
    for I := 1 to W - 1 do
    begin
      X1 := CR.Left + I;
      Y1 := AutoValueToY(CR, Curve.EvalAt(
        LongWord((QWord(I) * Clip.LengthMs) div LongWord(W)), Def));
      ACanvas.LineTo(X1, Y1);
    end;

    { Punkte nur für aktive Edit-Kurve }
    if P = FEditAutoParam then
    begin
      ACanvas.Brush.Color := Col;
      for I := 0 to Curve.Count - 1 do
      begin
        Pt := Curve.Points[I];
        X0 := CR.Left + Integer((QWord(Pt.TimeMs) * LongWord(W)) div Max(LongWord(1), Clip.LengthMs));
        Y0 := AutoValueToY(CR, Pt.Value);
        ACanvas.FillRect(MkRect(X0 - 3, Y0 - 3, X0 + 4, Y0 + 4));
      end;
    end;
  end;
  ACanvas.Pen.Width := 1;
end;

function TZForkStoryboard.HitAutoPoint(ClipIndex, X, Y: Integer;
  out PtIndex: Integer): Boolean;
var
  C: TZForkStoryClip;
  CR: TRect;
  Curve: TZForkAutoCurve;
  LocalMs: LongWord;
  Val: Single;
  TolMs: LongWord;
begin
  Result := False;
  PtIndex := -1;
  if (ClipIndex < 0) or (ClipIndex >= FClips.Count) then Exit;
  if not FShowAutomation then Exit;
  C := FClips[ClipIndex];
  CR := MkRect(MsToX(C.StartMs), TrackRect(C.Track).Top + 4,
    MsToX(C.EndMs), TrackRect(C.Track).Bottom - 4);
  if CR.Left < FLabelWidth then CR.Left := FLabelWidth;
  Curve := C.Automation.Curve(FEditAutoParam);
  LocalMs := ClipLocalMs(C, X);
  Val := YToAutoValue(CR, Y);
  TolMs := Max(LongWord(80), LongWord(Round(60000.0 / Max(1, FPixelsPerMinute) * 6)));
  PtIndex := Curve.HitTest(LocalMs, Val, TolMs, 0.12);
  Result := PtIndex >= 0;
end;

procedure TZForkStoryboard.BuildAutoPopup;
var
  M, Sub: TMenuItem;
  P: TZForkAutoParam;
begin
  FPopup := TPopupMenu.Create(Self);
  M := TMenuItem.Create(FPopup);
  M.Caption := 'Punkt setzen';
  M.OnClick := @PopupAddPoint;
  FPopup.Items.Add(M);

  M := TMenuItem.Create(FPopup);
  M.Caption := 'Punkt löschen';
  M.OnClick := @PopupDeletePoint;
  FPopup.Items.Add(M);

  M := TMenuItem.Create(FPopup);
  M.Caption := 'Kurve leeren';
  M.OnClick := @PopupClearCurve;
  FPopup.Items.Add(M);

  M := TMenuItem.Create(FPopup);
  M.Caption := '-';
  FPopup.Items.Add(M);

  Sub := TMenuItem.Create(FPopup);
  Sub.Caption := 'Automation';
  FPopup.Items.Add(Sub);
  for P := Low(TZForkAutoParam) to High(TZForkAutoParam) do
  begin
    M := TMenuItem.Create(Sub);
    M.Caption := AutoParamCaption(P);
    M.Tag := Ord(P);
    M.RadioItem := True;
    M.Checked := P = FEditAutoParam;
    M.OnClick := @PopupSelectParam;
    Sub.Add(M);
  end;
end;

procedure TZForkStoryboard.PopupAddPoint(Sender: TObject);
var
  C: TZForkStoryClip;
  CR: TRect;
  LocalMs: LongWord;
  Val: Single;
begin
  if (FCtxClip < 0) or (FCtxClip >= FClips.Count) then Exit;
  C := FClips[FCtxClip];
  CR := MkRect(MsToX(C.StartMs), TrackRect(C.Track).Top + 4,
    MsToX(C.EndMs), TrackRect(C.Track).Bottom - 4);
  LocalMs := ClipLocalMs(C, FCtxX);
  Val := YToAutoValue(CR, FCtxY);
  C.Automation.Curve(FEditAutoParam).AddPoint(LocalMs, Val);
end;

procedure TZForkStoryboard.PopupDeletePoint(Sender: TObject);
var
  C: TZForkStoryClip;
  Idx: Integer;
begin
  if (FCtxClip < 0) or (FCtxClip >= FClips.Count) then Exit;
  C := FClips[FCtxClip];
  if HitAutoPoint(FCtxClip, FCtxX, FCtxY, Idx) then
    C.Automation.Curve(FEditAutoParam).DeletePoint(Idx);
end;

procedure TZForkStoryboard.PopupClearCurve(Sender: TObject);
begin
  if (FCtxClip < 0) or (FCtxClip >= FClips.Count) then Exit;
  FClips[FCtxClip].Automation.Curve(FEditAutoParam).Clear;
end;

procedure TZForkStoryboard.PopupSelectParam(Sender: TObject);
var
  I, J: Integer;
  Sub: TMenuItem;
begin
  if not (Sender is TMenuItem) then Exit;
  SetEditAutoParam(TZForkAutoParam(TMenuItem(Sender).Tag));
  for I := 0 to FPopup.Items.Count - 1 do
    if FPopup.Items[I].Caption = 'Automation' then
    begin
      Sub := FPopup.Items[I];
      for J := 0 to Sub.Count - 1 do
        Sub.Items[J].Checked := Sub.Items[J].Tag = Ord(FEditAutoParam);
      Break;
    end;
end;

procedure TZForkStoryboard.ApplyClipAutomation(Clip: TZForkStoryClip;
  Player: TZForkPlayer; LocalMs: LongWord);
var
  Vol, Bass, Mid, High, EchoV, RevV, SideV: Single;
  Gain: Integer;
  Echo: TEchoParams;
  Db: Single;
const
  EQ_LOW = 2;
  EQ_MID = 12;
  EQ_HIGH = 24;
begin
  if (Clip = nil) or (Player = nil) then Exit;
  Vol := Clip.Automation.Curve(apVolume).EvalAt(LocalMs, AutoParamDefault(apVolume));
  Bass := Clip.Automation.Curve(apBass).EvalAt(LocalMs, AutoParamDefault(apBass));
  Mid := Clip.Automation.Curve(apMid).EvalAt(LocalMs, AutoParamDefault(apMid));
  High := Clip.Automation.Curve(apHigh).EvalAt(LocalMs, AutoParamDefault(apHigh));
  EchoV := Clip.Automation.Curve(apEcho).EvalAt(LocalMs, AutoParamDefault(apEcho));
  RevV := Clip.Automation.Curve(apReverb).EvalAt(LocalMs, AutoParamDefault(apReverb));
  SideV := Clip.Automation.Curve(apSideCut).EvalAt(LocalMs, AutoParamDefault(apSideCut));

  Gain := Round(Vol * 100);
  if Gain < 0 then Gain := 0;
  if Gain > 100 then Gain := 100;
  if FDucking and (Clip.Track in [stTrackA, stTrackB]) then
    Gain := (Gain * FDuckPercent) div 100;
  Player.MasterVolumeLeft := Gain;
  Player.MasterVolumeRight := Gain;

  Player.EqualizerEnabled := True;
  Db := (Bass - 0.5) * 24.0;
  Player.SetEQBandGain(EQ_LOW, Db);
  Db := (Mid - 0.5) * 24.0;
  Player.SetEQBandGain(EQ_MID, Db);
  Db := (High - 0.5) * 24.0;
  Player.SetEQBandGain(EQ_HIGH, Db);

  { Echo + Reverb (kein separates Reverb in Player-API → langer Delay) }
  FillChar(Echo, SizeOf(Echo), 0);
  if RevV > EchoV then
  begin
    Echo.leftDelay := 700;
    Echo.rightDelay := 820;
    Echo.leftEchoVol := Round(RevV * 80);
    Echo.rightEchoVol := Round(RevV * 80);
  end
  else
  begin
    Echo.leftDelay := 280;
    Echo.rightDelay := 320;
    Echo.leftEchoVol := Round(EchoV * 90);
    Echo.rightEchoVol := Round(EchoV * 90);
  end;
  Echo.leftSourceVol := 100;
  Echo.rightSourceVol := 100;
  Player.EchoEnabled := (EchoV > 0.02) or (RevV > 0.02);
  if Player.EchoEnabled then
    Player.SetEchoParams(Echo);

  Player.EnableSideCut(SideV >= 0.5);
end;

procedure TZForkStoryboard.ApplyTrimLeft(Clip: TZForkStoryClip; NewStartMs: LongWord);
var
  OldEnd, Delta, MaxIn: LongWord;
begin
  if Clip = nil then Exit;
  OldEnd := Clip.EndMs;
  if NewStartMs + 500 > OldEnd then
    NewStartMs := OldEnd - 500;
  MaxIn := Clip.FSourceLengthMs;
  if MaxIn = 0 then MaxIn := Clip.FInMs + Clip.FLengthMs;
  if NewStartMs >= Clip.FStartMs then
  begin
    Delta := NewStartMs - Clip.FStartMs;
    if Clip.FInMs + Delta + 500 > MaxIn then
      Delta := MaxIn - Clip.FInMs - 500;
    Clip.FInMs := Clip.FInMs + Delta;
    Clip.FStartMs := Clip.FStartMs + Delta;
    Clip.FLengthMs := OldEnd - Clip.FStartMs;
  end
  else
  begin
    Delta := Clip.FStartMs - NewStartMs;
    if Delta > Clip.FInMs then Delta := Clip.FInMs;
    Clip.FInMs := Clip.FInMs - Delta;
    Clip.FStartMs := Clip.FStartMs - Delta;
    Clip.FLengthMs := Clip.FLengthMs + Delta;
  end;
  if Clip.FLengthMs < 500 then Clip.FLengthMs := 500;
end;

procedure TZForkStoryboard.ApplyTrimRight(Clip: TZForkStoryClip; NewEndMs: LongWord);
var
  MaxLen: LongWord;
begin
  if Clip = nil then Exit;
  if NewEndMs < Clip.FStartMs + 500 then
    NewEndMs := Clip.FStartMs + 500;
  MaxLen := Clip.FSourceLengthMs;
  if MaxLen = 0 then MaxLen := Clip.FInMs + Clip.FLengthMs;
  if MaxLen > Clip.FInMs then
    MaxLen := MaxLen - Clip.FInMs
  else
    MaxLen := 500;
  if NewEndMs - Clip.FStartMs > MaxLen then
    NewEndMs := Clip.FStartMs + MaxLen;
  Clip.FLengthMs := NewEndMs - Clip.FStartMs;
end;

procedure TZForkStoryboard.ZoomAt(X: Integer; ZoomIn: Boolean);
var
  AnchorMs: LongWord;
  NewPpm: Integer;
begin
  AnchorMs := XToMs(X);
  if ZoomIn then
    NewPpm := Round(FPixelsPerMinute * 1.4)
  else
    NewPpm := Round(FPixelsPerMinute / 1.4);
  SetPixelsPerMinute(NewPpm);
  FScrollX := Max(0, Round((AnchorMs / 60000.0) * FPixelsPerMinute) - (X - FLabelWidth));
  UpdateHScroll;
  Invalidate;
end;

procedure TZForkStoryboard.ZoomToClip(ClipIndex: Integer);
var
  C: TZForkStoryClip;
  Avail: Integer;
  Ppm: Integer;
  Denom: Double;
begin
  if (ClipIndex < 0) or (ClipIndex >= FClips.Count) then Exit;
  C := FClips[ClipIndex];
  Avail := Max(60, Width - FLabelWidth - 24);
  if C.LengthMs > 20000 then
    Denom := C.LengthMs / 60000.0
  else
    Denom := 20000.0 / 60000.0;
  if Denom < 0.05 then Denom := 0.05;
  Ppm := Round(Avail / Denom);
  SetPixelsPerMinute(Ppm);
  FScrollX := Max(0, Round((C.StartMs / 60000.0) * FPixelsPerMinute) - 12);
  UpdateHScroll;
  Invalidate;
end;

procedure TZForkStoryboard.ZoomIn;
begin
  ZoomAt(Width div 2, True);
end;

procedure TZForkStoryboard.ZoomOut;
begin
  ZoomAt(Width div 2, False);
end;

procedure TZForkStoryboard.ZoomFit;
var
  I: Integer;
  C: TZForkStoryClip;
  MinMs, MaxMs: LongWord;
  Avail: Integer;
  Denom: Double;
  Found: Boolean;
begin
  Found := False;
  MinMs := High(LongWord);
  MaxMs := 0;
  for I := 0 to FClips.Count - 1 do
  begin
    C := FClips[I];
    if C.StartMs < MinMs then MinMs := C.StartMs;
    if C.EndMs > MaxMs then MaxMs := C.EndMs;
    Found := True;
  end;
  if not Found then
  begin
    SetPixelsPerMinute(80);
    FScrollX := 0;
    UpdateHScroll;
    Invalidate;
    Exit;
  end;
  if MaxMs <= MinMs then MaxMs := MinMs + 60000;
  Avail := Max(60, Width - FLabelWidth - 40);
  Denom := (MaxMs - MinMs + 8000) / 60000.0;
  if Denom < 0.2 then Denom := 0.2;
  SetPixelsPerMinute(Round(Avail / Denom));
  FScrollX := Max(0, Round((MinMs / 60000.0) * FPixelsPerMinute) - 20);
  UpdateHScroll;
  Invalidate;
end;

procedure TZForkStoryboard.DrawTransitions(ACanvas: TCanvas);
var
  I, J, X: Integer;
  A, B: TZForkStoryClip;
  Gap: Int64;
begin
  { Übergänge A↔B markieren (gelb), damit Zoom/Beatmatch sichtbar wird }
  for I := 0 to FClips.Count - 1 do
  begin
    A := FClips[I];
    if A.Track <> stTrackA then Continue;
    for J := 0 to FClips.Count - 1 do
    begin
      B := FClips[J];
      if B.Track <> stTrackB then Continue;
      Gap := Int64(B.StartMs) - Int64(A.EndMs);
      if (Gap > 30000) or (Gap < -30000) then Continue;
      ACanvas.Pen.Color := RGBToColor(255, 200, 60);
      ACanvas.Pen.Width := 2;
      X := MsToX(A.EndMs);
      if (X >= FLabelWidth) and (X < Width) then
      begin
        ACanvas.MoveTo(X, FRulerHeight);
        ACanvas.LineTo(X, TracksBottom);
      end;
      X := MsToX(B.StartMs);
      if (X >= FLabelWidth) and (X < Width) then
      begin
        ACanvas.MoveTo(X, FRulerHeight);
        ACanvas.LineTo(X, TracksBottom);
      end;
      ACanvas.Pen.Width := 1;
    end;
  end;
end;

procedure TZForkStoryboard.DrawPlayhead(ACanvas: TCanvas);
var
  X: Integer;
begin
  X := MsToX(FProgramMs);
  if (X < FLabelWidth) or (X > Width) then Exit;
  ACanvas.Pen.Color := FPlayheadColor;
  ACanvas.Pen.Width := 2;
  ACanvas.MoveTo(X, 0);
  ACanvas.LineTo(X, TracksBottom);
  ACanvas.Pen.Width := 1;
end;

procedure TZForkStoryboard.Paint;
begin
  Canvas.Brush.Color := FBackColor;
  Canvas.FillRect(ClientRect);
  DrawRuler(Canvas);
  DrawTrack(Canvas, stTrackA);
  DrawTrack(Canvas, stTrackB);
  DrawTrack(Canvas, stTrackC);
  DrawTransitions(Canvas);
  DrawPlayhead(Canvas);
  if csDesigning in ComponentState then
  begin
    Canvas.Brush.Style := bsClear;
    Canvas.Font.Color := clGray;
    Canvas.Font.Height := -10;
    Canvas.TextOut(FLabelWidth + 8, Height - 16, 'Storyboard A/B/C — Dateien hierher ziehen');
    Canvas.Brush.Style := bsSolid;
  end;
end;

procedure TZForkStoryboard.MouseDown(Button: TMouseButton; Shift: TShiftState;
  X, Y: Integer);
var
  PtIdx: Integer;
  I: Integer;
  Sub: TMenuItem;
begin
  inherited MouseDown(Button, Shift, X, Y);
  if CanFocus and not Focused then SetFocus;

  if Button = mbRight then
  begin
    FCtxClip := HitClip(X, Y);
    FCtxX := X;
    FCtxY := Y;
    if (FCtxClip >= 0) and (FPopup <> nil) then
    begin
      for I := 0 to FPopup.Items.Count - 1 do
        if FPopup.Items[I].Caption = 'Automation' then
        begin
          Sub := FPopup.Items[I];
          for PtIdx := 0 to Sub.Count - 1 do
            Sub.Items[PtIdx].Checked := Sub.Items[PtIdx].Tag = Ord(FEditAutoParam);
          Break;
        end;
      FPopup.PopUp;
    end;
    Exit;
  end;

  if Button <> mbLeft then Exit;
  if Y < FRulerHeight then
  begin
    SeekProgram(XToMs(X));
    Exit;
  end;
  FDragClip := HitClip(X, Y);
  if FDragClip >= 0 then
  begin
    if FShowAutomation and HitAutoPoint(FDragClip, X, Y, PtIdx) then
    begin
      FDragMode := sdmAutoPoint;
      FDragAutoPt := PtIdx;
    end
    else if ssAlt in Shift then
    begin
      FDragMode := sdmNudgeGrid;
      FNudgeStartX := X;
      FNudgeStartOffset := FClips[FDragClip].BeatOffsetMs;
    end
    else
    begin
      FDragMode := HitClipEdge(FDragClip, X);
      FDragOffsetX := X - MsToX(FClips[FDragClip].StartMs);
    end;
    MouseCapture := True;
  end
  else
  begin
    FDragMode := sdmNone;
    SeekProgram(XToMs(X));
  end;
end;

procedure TZForkStoryboard.MouseMove(Shift: TShiftState; X, Y: Integer);
var
  Edge: TZForkStoryDragMode;
  Idx, PtIdx: Integer;
  DeltaMs: Integer;
  BeatMs: Integer;
  NewOff: Integer;
  C: TZForkStoryClip;
  CR: TRect;
  LocalMs: LongWord;
  Val: Single;
begin
  inherited MouseMove(Shift, X, Y);
  if (FDragClip >= 0) and (ssLeft in Shift) then
  begin
    case FDragMode of
      sdmTrimLeft:
        begin
          ApplyTrimLeft(FClips[FDragClip], XToMs(X));
          EnsureDurationForClip(FClips[FDragClip]);
        end;
      sdmTrimRight:
        begin
          ApplyTrimRight(FClips[FDragClip], XToMs(X));
          EnsureDurationForClip(FClips[FDragClip]);
        end;
      sdmNudgeGrid:
        begin
          DeltaMs := Round((X - FNudgeStartX) * 60000.0 / Max(1, FPixelsPerMinute));
          NewOff := Integer(FNudgeStartOffset) + DeltaMs;
          if FClips[FDragClip].Bpm > 40 then
          begin
            BeatMs := Max(1, Round(60000.0 / FClips[FDragClip].Bpm));
            while NewOff < 0 do Inc(NewOff, BeatMs);
            NewOff := NewOff mod BeatMs;
          end
          else if NewOff < 0 then
            NewOff := 0;
          FClips[FDragClip].BeatOffsetMs := LongWord(NewOff);
        end;
      sdmAutoPoint:
        begin
          C := FClips[FDragClip];
          CR := MkRect(MsToX(C.StartMs), TrackRect(C.Track).Top + 4,
            MsToX(C.EndMs), TrackRect(C.Track).Bottom - 4);
          LocalMs := ClipLocalMs(C, X);
          Val := YToAutoValue(CR, Y);
          C.Automation.Curve(FEditAutoParam).SetPoint(FDragAutoPt, LocalMs, Val);
          { Index kann nach Sort wechseln }
          HitAutoPoint(FDragClip, X, Y, FDragAutoPt);
        end;
    else
      FClips[FDragClip].StartMs := XToMs(X - FDragOffsetX);
      EnsureDurationForClip(FClips[FDragClip]);
    end;
    Invalidate;
  end
  else
  begin
    Idx := HitClip(X, Y);
    if Idx >= 0 then
    begin
      if FShowAutomation and HitAutoPoint(Idx, X, Y, PtIdx) then
        Cursor := crSizeNS
      else if ssAlt in Shift then
        Cursor := crSizeAll
      else
      begin
        Edge := HitClipEdge(Idx, X);
        if Edge in [sdmTrimLeft, sdmTrimRight] then
          Cursor := crSizeWE
        else
          Cursor := crHandPoint;
      end;
    end
    else
      Cursor := crDefault;
  end;
end;

procedure TZForkStoryboard.MouseUp(Button: TMouseButton; Shift: TShiftState;
  X, Y: Integer);
begin
  inherited MouseUp(Button, Shift, X, Y);
  if FDragClip >= 0 then
  begin
    FDragClip := -1;
    FDragMode := sdmNone;
    FDragAutoPt := -1;
    MouseCapture := False;
    Changed;
  end;
end;

procedure TZForkStoryboard.MouseEnter;
begin
  inherited MouseEnter;
  if CanFocus and not (csDesigning in ComponentState) then
    SetFocus;
end;

procedure TZForkStoryboard.DblClick;
var
  P: TPoint;
  Idx: Integer;
begin
  inherited DblClick;
  P := ScreenToClient(Mouse.CursorPos);
  Idx := HitClip(P.X, P.Y);
  if Idx >= 0 then
    ZoomToClip(Idx)
  else
    ZoomFit;
end;

function TZForkStoryboard.DoMouseWheel(Shift: TShiftState; WheelDelta: Integer;
  MousePos: TPoint): Boolean;
var
  P: TPoint;
  DoZoom: Boolean;
begin
  Result := True;
  if CanFocus and not Focused then SetFocus;
  P := ScreenToClient(Mouse.CursorPos);
  { Story-Zoom = normales Rad; Shift = horizontal scrollen; Strg bleibt für Waveform }
  DoZoom := FWheelZoom and not (ssShift in Shift);
  if DoZoom then
    ZoomAt(P.X, WheelDelta > 0)
  else
  begin
    if WheelDelta > 0 then
      FScrollX := Max(0, FScrollX - Max(40, Width div 8))
    else
      FScrollX := Min(Max(0, ContentWidth - Width), FScrollX + Max(40, Width div 8));
    UpdateHScroll;
    Invalidate;
  end;
end;

end.
