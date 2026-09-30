{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  TZForkSpectrum — Echtzeit-FFT- / VU-Visualisierung

  Was die Komponente kann:
  - Styles: Bars, Lines, Area, VU, LED, Neon, Fire, Plasma, Rainbow, …
  - Anbindung an TZForkPlayer; SyncMode Timer/Idle/Hybrid, TargetFPS
  - Attack/Release (Response), LogScale, Grid, Peak-Hold
  - Pro-Style-Farben, AutoIntensity, FastDraw fuer hohe FPS
  - Stereo links/rechts oder gemittelt; Resize-sicheres Offscreen-Painting
******************************************************************************}
unit ZForkSpectrum;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Types, Controls, Graphics, ExtCtrls, Forms, LCLType,
  ZForkPlayer, ZForkIntf, LibZFork;

const
  ZFORK_SPECTRUM_MAX_BARS = 256;

type
  TZForkSpectrumStyle = (
    ssBars,      // klassische FFT-Balken
    ssFilled,    // gefuellte Flaeche
    ssVU,        // Peak-Meter L/R
    ssLine,      // weiche Spektrum-Linie
    ssMirror,    // Spiegelbalken von der Mitte
    ssDots,      // schwebende Dots
    ssLED,       // LED-Segmente
    ssGradient,  // Balken mit Farbverlauf
    ssRainbow,   // Regenbogen nach Frequenz
    ssFire,      // Feuer-Verlauf (rot→gelb)
    ssNeon,      // Neon-Glow Balken
    ssWaveGlow,  // Wellenflaeche + Glow-Linie
    ssPlasma     // bunte Plasma-Balken
  );

  TZForkSpectrumChannels = (
    scMono,    // (L+R)/2
    scStereo   // L oben / R unten (bei Bars/Filled/…)
  );

  { Wie oft/wie synchron aktualisiert wird }
  TZForkSpectrumSync = (
    ssSyncTimer,  // TTimer (einfach)
    ssSyncIdle,   // Application-Idle (~Display-Refresh, fluessiger)
    ssSyncHybrid  // Timer + Idle-Nachzieh (Default: synchron + weich)
  );

  TZForkSpectrum = class;

  { Envelope / Antwort im Object Inspector (+/- aufklappen) }
  TZForkSpectrumResponse = class(TPersistent)
  private
    FSpectrum: TZForkSpectrum;
    FDirectResponse: Boolean;
    FAttack: Single;
    FRelease: Single;
    FSmoothing: Single;
    procedure SetDirectResponse(AValue: Boolean);
    procedure SetAttack(AValue: Single);
    procedure SetRelease(AValue: Single);
    procedure SetSmoothing(AValue: Single);
  protected
    function GetOwner: TPersistent; override;
  public
    constructor Create(ASpectrum: TZForkSpectrum);
    procedure Assign(Source: TPersistent); override;
  published
    { True = FFT 1:1, Balken schiessen sofort hoch/runter }
    property DirectResponse: Boolean read FDirectResponse write SetDirectResponse default True;
    { Anstieg 0=sofort … 0.95=traege (nur wenn DirectResponse=False) }
    property Attack: Single read FAttack write SetAttack;
    { Abfall 0=sofort … 0.95=nachklingend (nur wenn DirectResponse=False) }
    property Release: Single read FRelease write SetRelease;
    { Extra-Glaettung 0=aus (nur wenn DirectResponse=False) }
    property Smoothing: Single read FSmoothing write SetSmoothing;
  end;

  { Basis fuer Object-Inspector-Untergruppen (+/-) }
  TZForkSpectrumColorGroup = class(TPersistent)
  private
    FOwner: TPersistent;
    FSpectrum: TZForkSpectrum;
  protected
    procedure Changed;
    function GetOwner: TPersistent; override;
  public
    constructor Create(AOwner: TPersistent; ASpectrum: TZForkSpectrum); virtual;
  end;

  { Einfache Styles: Hauptfarbe + Peak/Highlight }
  TZForkToneColors = class(TZForkSpectrumColorGroup)
  private
    FColor: TColor;
    FPeak: TColor;
    procedure SetColor(AValue: TColor);
    procedure SetPeak(AValue: TColor);
  public
    constructor Create(AOwner: TPersistent; ASpectrum: TZForkSpectrum); override;
    procedure Assign(Source: TPersistent); override;
  published
    property Color: TColor read FColor write SetColor;
    property Peak: TColor read FPeak write SetPeak;
  end;

  { Fire / Plasma / Rainbow: Low → Mid → High }
  TZForkRampColors = class(TZForkSpectrumColorGroup)
  private
    FLow: TColor;
    FMid: TColor;
    FHigh: TColor;
    procedure SetLow(AValue: TColor);
    procedure SetMid(AValue: TColor);
    procedure SetHigh(AValue: TColor);
  public
    constructor Create(AOwner: TPersistent; ASpectrum: TZForkSpectrum); override;
    procedure Assign(Source: TPersistent); override;
  published
    property Low: TColor read FLow write SetLow;
    property Mid: TColor read FMid write SetMid;
    property High: TColor read FHigh write SetHigh;
  end;

  { Neon: Hauptfarbe + Glow }
  TZForkNeonColors = class(TZForkSpectrumColorGroup)
  private
    FColor: TColor;
    FGlow: TColor;
    procedure SetColor(AValue: TColor);
    procedure SetGlow(AValue: TColor);
  public
    constructor Create(AOwner: TPersistent; ASpectrum: TZForkSpectrum); override;
    procedure Assign(Source: TPersistent); override;
  published
    property Color: TColor read FColor write SetColor;
    property Glow: TColor read FGlow write SetGlow;
  end;

  { WaveGlow: Flaeche + Linien-Peak }
  TZForkWaveColors = class(TZForkSpectrumColorGroup)
  private
    FColor: TColor;
    FPeak: TColor;
    procedure SetColor(AValue: TColor);
    procedure SetPeak(AValue: TColor);
  public
    constructor Create(AOwner: TPersistent; ASpectrum: TZForkSpectrum); override;
    procedure Assign(Source: TPersistent); override;
  published
    property Color: TColor read FColor write SetColor;
    property Peak: TColor read FPeak write SetPeak;
  end;

  { Styles → jeder Anzeige-Style → eigene Farben }
  TZForkSpectrumStyles = class(TPersistent)
  private
    FSpectrum: TZForkSpectrum;
    FBars: TZForkToneColors;
    FFilled: TZForkToneColors;
    FLine: TZForkToneColors;
    FMirror: TZForkToneColors;
    FDots: TZForkToneColors;
    FLED: TZForkToneColors;
    FGradient: TZForkToneColors;
    FVU: TZForkToneColors;
    FRainbow: TZForkRampColors;
    FFire: TZForkRampColors;
    FNeon: TZForkNeonColors;
    FWaveGlow: TZForkWaveColors;
    FPlasma: TZForkRampColors;
    procedure SetBars(AValue: TZForkToneColors);
    procedure SetFilled(AValue: TZForkToneColors);
    procedure SetLine(AValue: TZForkToneColors);
    procedure SetMirror(AValue: TZForkToneColors);
    procedure SetDots(AValue: TZForkToneColors);
    procedure SetLED(AValue: TZForkToneColors);
    procedure SetGradient(AValue: TZForkToneColors);
    procedure SetVU(AValue: TZForkToneColors);
    procedure SetRainbow(AValue: TZForkRampColors);
    procedure SetFire(AValue: TZForkRampColors);
    procedure SetNeon(AValue: TZForkNeonColors);
    procedure SetWaveGlow(AValue: TZForkWaveColors);
    procedure SetPlasma(AValue: TZForkRampColors);
  protected
    function GetOwner: TPersistent; override;
  public
    constructor Create(ASpectrum: TZForkSpectrum);
    destructor Destroy; override;
    procedure Assign(Source: TPersistent); override;
  published
    property Bars: TZForkToneColors read FBars write SetBars;
    property Filled: TZForkToneColors read FFilled write SetFilled;
    property Line: TZForkToneColors read FLine write SetLine;
    property Mirror: TZForkToneColors read FMirror write SetMirror;
    property Dots: TZForkToneColors read FDots write SetDots;
    property LED: TZForkToneColors read FLED write SetLED;
    property Gradient: TZForkToneColors read FGradient write SetGradient;
    property VU: TZForkToneColors read FVU write SetVU;
    property Rainbow: TZForkRampColors read FRainbow write SetRainbow;
    property Fire: TZForkRampColors read FFire write SetFire;
    property Neon: TZForkNeonColors read FNeon write SetNeon;
    property WaveGlow: TZForkWaveColors read FWaveGlow write SetWaveGlow;
    property Plasma: TZForkRampColors read FPlasma write SetPlasma;
  end;

  { TZForkSpectrum }
  TZForkSpectrum = class(TCustomControl, IZForkPlayerClient)
  private
    FPlayer: TZForkPlayer;
    FActive: Boolean;
    FStyle: TZForkSpectrumStyle;
    FChannels: TZForkSpectrumChannels;
    FBarCount: Integer;
    FInterval: Integer;
    FShowPeaks: Boolean;
    FPeakDecay: Single;
    FBackColor: TColor;
    FGridColor: TColor;
    FStyles: TZForkSpectrumStyles;
    FShowGrid: Boolean;
    FGridGlow: Boolean;
    FGridAlpha: Integer;
    FGridStepX: Integer;
    FGridStepY: Integer;
    FAntiAlias: Boolean;
    FIntensity: Single;
    FAutoIntensity: Boolean;
    FAutoGain: Single;
    FPeakRaw: Single;
    FLogScale: Boolean;
    FResponse: TZForkSpectrumResponse;
    FSyncMode: TZForkSpectrumSync;
    FIdleHooked: Boolean;
    FLastSampleMs: QWord;
    FTargetFPS: Integer;
    FFastDraw: Boolean;
    FBuffer: TBitmap;
    FAbout: string;
    FTimer: TTimer;
    FMagL: array[0..ZFORK_SPECTRUM_MAX_BARS - 1] of Single;
    FMagR: array[0..ZFORK_SPECTRUM_MAX_BARS - 1] of Single;
    FPeakL: array[0..ZFORK_SPECTRUM_MAX_BARS - 1] of Single;
    FPeakR: array[0..ZFORK_SPECTRUM_MAX_BARS - 1] of Single;
    FVuL, FVuR: Single;
    FVuPeakL, FVuPeakR: Single;
    FTick: Integer;
    procedure SetPlayer(AValue: TZForkPlayer);
    procedure SetActive(AValue: Boolean);
    procedure SetStyle(AValue: TZForkSpectrumStyle);
    procedure SetChannels(AValue: TZForkSpectrumChannels);
    procedure SetBarCount(AValue: Integer);
    procedure SetInterval(AValue: Integer);
    procedure SetShowPeaks(AValue: Boolean);
    procedure SetPeakDecay(AValue: Single);
    procedure SetBarColor(AValue: TColor);
    procedure SetPeakColor(AValue: TColor);
    function GetBarColor: TColor;
    function GetPeakColor: TColor;
    procedure SetBackColor(AValue: TColor);
    procedure SetGridColor(AValue: TColor);
    procedure SetStyles(AValue: TZForkSpectrumStyles);
    procedure SetShowGrid(AValue: Boolean);
    procedure SetGridGlow(AValue: Boolean);
    procedure SetGridAlpha(AValue: Integer);
    procedure SetGridStepX(AValue: Integer);
    procedure SetGridStepY(AValue: Integer);
    procedure SetAntiAlias(AValue: Boolean);
    procedure SetIntensity(AValue: Single);
    procedure SetAutoIntensity(AValue: Boolean);
    procedure SetLogScale(AValue: Boolean);
    procedure SetResponse(AValue: TZForkSpectrumResponse);
    procedure SetSyncMode(AValue: TZForkSpectrumSync);
    procedure SetTargetFPS(AValue: Integer);
    procedure SetFastDraw(AValue: Boolean);
    procedure EnsureBuffer;
    function GetFrameGapMs: Integer;
    function GetAbout: string;
    procedure SetAbout(const AValue: string);
    function BoostLevel(Raw: Single): Single;
    function SmoothToward(Current, Target: Single): Single;
    procedure UpdateAutoGain(RawPeak: Single);
    procedure TimerTick(Sender: TObject);
    procedure AppIdle(Sender: TObject; var Done: Boolean);
    procedure HookIdle(Enable: Boolean);
    procedure DoSampleAndPaint(ForcePaint: Boolean);
    procedure UpdateTimer;
    procedure SampleData;
    procedure SampleDesignTime;
    procedure DecayPeaks;
    function MixRGB(C1, C2: TColor; T: Single): TColor;
    function BlendOnBack(Fg: TColor; Alpha: Integer): TColor;
    procedure SoftLine(ACanvas: TCanvas; X1, Y1, X2, Y2: Integer; Col: TColor);
    procedure SoftEllipse(ACanvas: TCanvas; X, Y, R: Integer; Col: TColor);
    procedure PaintGrid(ACanvas: TCanvas; const ARect: TRect);
    procedure PaintBars(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
    procedure PaintFilled(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
    procedure PaintVU(ACanvas: TCanvas; const ARect: TRect);
    procedure PaintLine(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
    procedure PaintMirror(ACanvas: TCanvas; const ARect: TRect);
    procedure PaintDots(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
    procedure PaintLED(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
    procedure PaintGradient(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
    procedure PaintRainbow(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
    procedure PaintFire(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
    procedure PaintNeon(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
    procedure PaintWaveGlow(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
    procedure PaintPlasma(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
    function HueColor(Hue: Single; Bright: Single = 1.0): TColor;
    function PlasmaTint(Hue, HeightT: Single): TColor;
  protected
    procedure Notification(AComponent: TComponent; Operation: TOperation); override;
    procedure Paint; override;
    procedure EraseBackground(DC: HDC); override;
    procedure Loaded; override;
  public
    constructor Create(AOwner: TComponent); override;
    destructor Destroy; override;
    { IZForkPlayerClient }
    procedure NotifyPlayerOpen(const AFileName: string);
    procedure NotifyPlayerSong(SongIndex: Integer);
    procedure NotifyPlayerClose;
    procedure NotifyPlayerStop;
    procedure ShowAbout;
  published
    { Object Inspector: Schaltflaeche „…“ oeffnet About/Lizenzen. }
    property About: string read GetAbout write SetAbout stored False;
    property Align;
    property Anchors;
    property BorderSpacing;
    property Constraints;
    property Visible;
    property Hint;
    property ShowHint;
    property ParentShowHint;
    property PopupMenu;
    property OnClick;
    property OnDblClick;
    property OnMouseDown;
    property OnMouseMove;
    property OnMouseUp;
    { Player-Quelle (Handle muss Active/open sein). }
    property Player: TZForkPlayer read FPlayer write SetPlayer;
    property Active: Boolean read FActive write SetActive default True;
    property Style: TZForkSpectrumStyle read FStyle write SetStyle default ssBars;
    property Channels: TZForkSpectrumChannels read FChannels write SetChannels default scMono;
    property BarCount: Integer read FBarCount write SetBarCount default 64;
    property Interval: Integer read FInterval write SetInterval default 11;
    property ShowPeaks: Boolean read FShowPeaks write SetShowPeaks default True;
    { Peak-Abfall pro Tick (kleiner = laengerer Peak-Hold). }
    property PeakDecay: Single read FPeakDecay write SetPeakDecay;
    property BarColor: TColor read GetBarColor write SetBarColor default clAqua;
    property PeakColor: TColor read GetPeakColor write SetPeakColor default clWhite;
    property BackColor: TColor read FBackColor write SetBackColor default clBlack;
    { Styles → jeder Style → eigene Farben }
    property Styles: TZForkSpectrumStyles read FStyles write SetStyles;
    { Anzeige-Verstaerkung: FFT oft flach — Intensity 4..12 wirkt „voller“. }
    property Intensity: Single read FIntensity write SetIntensity;
    { Automatisch nachregeln, damit Balken den Pegel ausfuellen (weniger „hinterher“). }
    property AutoIntensity: Boolean read FAutoIntensity write SetAutoIntensity default True;
    { Logarithmische Skala: leise Baender anheben. }
    property LogScale: Boolean read FLogScale write SetLogScale default False;
    { Response → DirectResponse / Attack / Release / Smoothing }
    property Response: TZForkSpectrumResponse read FResponse write SetResponse;
    { Idle/Hybrid = fluessiger & naeher am Audio (rein Pascal, kein Extra-Thread). }
    property SyncMode: TZForkSpectrumSync read FSyncMode write SetSyncMode default ssSyncIdle;
    { Ziel-Framerate 30..240 (steuert Idle-Abstand; 90 ≈ 11ms). 0 = nur Interval. }
    property TargetFPS: Integer read FTargetFPS write SetTargetFPS default 90;
    { True = weniger Glow/Soft-Passes → deutlich mehr FPS. }
    property FastDraw: Boolean read FFastDraw write SetFastDraw default True;
    { Gitter }
    property ShowGrid: Boolean read FShowGrid write SetShowGrid default True;
    property GridColor: TColor read FGridColor write SetGridColor default $4080FF;
    property GridGlow: Boolean read FGridGlow write SetGridGlow default True;
    property GridAlpha: Integer read FGridAlpha write SetGridAlpha default 90;
    property GridStepX: Integer read FGridStepX write SetGridStepX default 24;
    property GridStepY: Integer read FGridStepY write SetGridStepY default 16;
    { Weichere Kanten / Glow (LCL AntialiasingMode + Soft-Pass). }
    property AntiAlias: Boolean read FAntiAlias write SetAntiAlias default True;
  end;

implementation

uses
  Math, ZForkAbout;

{ TZForkSpectrumResponse }

constructor TZForkSpectrumResponse.Create(ASpectrum: TZForkSpectrum);
begin
  inherited Create;
  FSpectrum := ASpectrum;
  FDirectResponse := True;
  FAttack := 0.0;
  FRelease := 0.04;
  FSmoothing := 0.0;
end;

function TZForkSpectrumResponse.GetOwner: TPersistent;
begin
  Result := FSpectrum;
end;

procedure TZForkSpectrumResponse.Assign(Source: TPersistent);
begin
  if Source is TZForkSpectrumResponse then
  begin
    FDirectResponse := TZForkSpectrumResponse(Source).DirectResponse;
    FAttack := TZForkSpectrumResponse(Source).Attack;
    FRelease := TZForkSpectrumResponse(Source).Release;
    FSmoothing := TZForkSpectrumResponse(Source).Smoothing;
    if FSpectrum <> nil then
      FSpectrum.Invalidate;
  end
  else
    inherited Assign(Source);
end;

procedure TZForkSpectrumResponse.SetDirectResponse(AValue: Boolean);
begin
  if FDirectResponse = AValue then Exit;
  FDirectResponse := AValue;
end;

procedure TZForkSpectrumResponse.SetAttack(AValue: Single);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 0.95 then AValue := 0.95;
  if Abs(FAttack - AValue) < 1e-4 then Exit;
  FAttack := AValue;
end;

procedure TZForkSpectrumResponse.SetRelease(AValue: Single);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 0.95 then AValue := 0.95;
  if Abs(FRelease - AValue) < 1e-4 then Exit;
  FRelease := AValue;
end;

procedure TZForkSpectrumResponse.SetSmoothing(AValue: Single);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 0.95 then AValue := 0.95;
  if Abs(FSmoothing - AValue) < 1e-4 then Exit;
  FSmoothing := AValue;
end;

{ TZForkSpectrumColorGroup }

constructor TZForkSpectrumColorGroup.Create(AOwner: TPersistent; ASpectrum: TZForkSpectrum);
begin
  inherited Create;
  FOwner := AOwner;
  FSpectrum := ASpectrum;
end;

procedure TZForkSpectrumColorGroup.Changed;
begin
  if FSpectrum <> nil then
    FSpectrum.Invalidate;
end;

function TZForkSpectrumColorGroup.GetOwner: TPersistent;
begin
  Result := FOwner;
end;

{ TZForkToneColors }

constructor TZForkToneColors.Create(AOwner: TPersistent; ASpectrum: TZForkSpectrum);
begin
  inherited Create(AOwner, ASpectrum);
  FColor := clAqua;
  FPeak := clWhite;
end;

procedure TZForkToneColors.Assign(Source: TPersistent);
begin
  if Source is TZForkToneColors then
  begin
    FColor := TZForkToneColors(Source).Color;
    FPeak := TZForkToneColors(Source).Peak;
    Changed;
  end
  else
    inherited Assign(Source);
end;

procedure TZForkToneColors.SetColor(AValue: TColor);
begin
  if FColor = AValue then Exit;
  FColor := AValue;
  Changed;
end;

procedure TZForkToneColors.SetPeak(AValue: TColor);
begin
  if FPeak = AValue then Exit;
  FPeak := AValue;
  Changed;
end;

{ TZForkRampColors }

constructor TZForkRampColors.Create(AOwner: TPersistent; ASpectrum: TZForkSpectrum);
begin
  inherited Create(AOwner, ASpectrum);
  FLow := RGBToColor(180, 20, 0);
  FMid := RGBToColor(255, 120, 0);
  FHigh := RGBToColor(255, 240, 80);
end;

procedure TZForkRampColors.Assign(Source: TPersistent);
begin
  if Source is TZForkRampColors then
  begin
    FLow := TZForkRampColors(Source).Low;
    FMid := TZForkRampColors(Source).Mid;
    FHigh := TZForkRampColors(Source).High;
    Changed;
  end
  else
    inherited Assign(Source);
end;

procedure TZForkRampColors.SetLow(AValue: TColor);
begin
  if FLow = AValue then Exit;
  FLow := AValue;
  Changed;
end;

procedure TZForkRampColors.SetMid(AValue: TColor);
begin
  if FMid = AValue then Exit;
  FMid := AValue;
  Changed;
end;

procedure TZForkRampColors.SetHigh(AValue: TColor);
begin
  if FHigh = AValue then Exit;
  FHigh := AValue;
  Changed;
end;

{ TZForkNeonColors }

constructor TZForkNeonColors.Create(AOwner: TPersistent; ASpectrum: TZForkSpectrum);
begin
  inherited Create(AOwner, ASpectrum);
  FColor := RGBToColor(0, 255, 200);
  FGlow := RGBToColor(180, 255, 255);
end;

procedure TZForkNeonColors.Assign(Source: TPersistent);
begin
  if Source is TZForkNeonColors then
  begin
    FColor := TZForkNeonColors(Source).Color;
    FGlow := TZForkNeonColors(Source).Glow;
    Changed;
  end
  else
    inherited Assign(Source);
end;

procedure TZForkNeonColors.SetColor(AValue: TColor);
begin
  if FColor = AValue then Exit;
  FColor := AValue;
  Changed;
end;

procedure TZForkNeonColors.SetGlow(AValue: TColor);
begin
  if FGlow = AValue then Exit;
  FGlow := AValue;
  Changed;
end;

{ TZForkWaveColors }

constructor TZForkWaveColors.Create(AOwner: TPersistent; ASpectrum: TZForkSpectrum);
begin
  inherited Create(AOwner, ASpectrum);
  FColor := RGBToColor(80, 180, 255);
  FPeak := clWhite;
end;

procedure TZForkWaveColors.Assign(Source: TPersistent);
begin
  if Source is TZForkWaveColors then
  begin
    FColor := TZForkWaveColors(Source).Color;
    FPeak := TZForkWaveColors(Source).Peak;
    Changed;
  end
  else
    inherited Assign(Source);
end;

procedure TZForkWaveColors.SetColor(AValue: TColor);
begin
  if FColor = AValue then Exit;
  FColor := AValue;
  Changed;
end;

procedure TZForkWaveColors.SetPeak(AValue: TColor);
begin
  if FPeak = AValue then Exit;
  FPeak := AValue;
  Changed;
end;

{ TZForkSpectrumStyles }

constructor TZForkSpectrumStyles.Create(ASpectrum: TZForkSpectrum);
begin
  inherited Create;
  FSpectrum := ASpectrum;
  FBars := TZForkToneColors.Create(Self, ASpectrum);
  FFilled := TZForkToneColors.Create(Self, ASpectrum);
  FLine := TZForkToneColors.Create(Self, ASpectrum);
  FMirror := TZForkToneColors.Create(Self, ASpectrum);
  FDots := TZForkToneColors.Create(Self, ASpectrum);
  FLED := TZForkToneColors.Create(Self, ASpectrum);
  FGradient := TZForkToneColors.Create(Self, ASpectrum);
  FVU := TZForkToneColors.Create(Self, ASpectrum);
  FRainbow := TZForkRampColors.Create(Self, ASpectrum);
  FFire := TZForkRampColors.Create(Self, ASpectrum);
  FNeon := TZForkNeonColors.Create(Self, ASpectrum);
  FWaveGlow := TZForkWaveColors.Create(Self, ASpectrum);
  FPlasma := TZForkRampColors.Create(Self, ASpectrum);
  { Defaults je Style }
  FBars.Color := clAqua;       FBars.Peak := clWhite;
  FFilled.Color := clAqua;     FFilled.Peak := clWhite;
  FLine.Color := clLime;       FLine.Peak := clWhite;
  FMirror.Color := clAqua;     FMirror.Peak := clYellow;
  FDots.Color := clFuchsia;    FDots.Peak := clWhite;
  FLED.Color := clLime;        FLED.Peak := clYellow;
  FGradient.Color := clNavy;   FGradient.Peak := clAqua;
  FVU.Color := clLime;         FVU.Peak := clRed;
  FRainbow.Low := RGBToColor(255, 40, 40);
  FRainbow.Mid := RGBToColor(40, 255, 80);
  FRainbow.High := RGBToColor(60, 120, 255);
  FFire.Low := RGBToColor(180, 20, 0);
  FFire.Mid := RGBToColor(255, 120, 0);
  FFire.High := RGBToColor(255, 240, 80);
  FPlasma.Low := RGBToColor(255, 0, 180);
  FPlasma.Mid := RGBToColor(0, 220, 255);
  FPlasma.High := RGBToColor(255, 255, 80);
end;

destructor TZForkSpectrumStyles.Destroy;
begin
  FreeAndNil(FBars);
  FreeAndNil(FFilled);
  FreeAndNil(FLine);
  FreeAndNil(FMirror);
  FreeAndNil(FDots);
  FreeAndNil(FLED);
  FreeAndNil(FGradient);
  FreeAndNil(FVU);
  FreeAndNil(FRainbow);
  FreeAndNil(FFire);
  FreeAndNil(FNeon);
  FreeAndNil(FWaveGlow);
  FreeAndNil(FPlasma);
  inherited Destroy;
end;

function TZForkSpectrumStyles.GetOwner: TPersistent;
begin
  Result := FSpectrum;
end;

procedure TZForkSpectrumStyles.Assign(Source: TPersistent);
begin
  if Source is TZForkSpectrumStyles then
  begin
    FBars.Assign(TZForkSpectrumStyles(Source).Bars);
    FFilled.Assign(TZForkSpectrumStyles(Source).Filled);
    FLine.Assign(TZForkSpectrumStyles(Source).Line);
    FMirror.Assign(TZForkSpectrumStyles(Source).Mirror);
    FDots.Assign(TZForkSpectrumStyles(Source).Dots);
    FLED.Assign(TZForkSpectrumStyles(Source).LED);
    FGradient.Assign(TZForkSpectrumStyles(Source).Gradient);
    FVU.Assign(TZForkSpectrumStyles(Source).VU);
    FRainbow.Assign(TZForkSpectrumStyles(Source).Rainbow);
    FFire.Assign(TZForkSpectrumStyles(Source).Fire);
    FNeon.Assign(TZForkSpectrumStyles(Source).Neon);
    FWaveGlow.Assign(TZForkSpectrumStyles(Source).WaveGlow);
    FPlasma.Assign(TZForkSpectrumStyles(Source).Plasma);
  end
  else
    inherited Assign(Source);
end;

procedure TZForkSpectrumStyles.SetBars(AValue: TZForkToneColors);
begin
  FBars.Assign(AValue);
end;

procedure TZForkSpectrumStyles.SetFilled(AValue: TZForkToneColors);
begin
  FFilled.Assign(AValue);
end;

procedure TZForkSpectrumStyles.SetLine(AValue: TZForkToneColors);
begin
  FLine.Assign(AValue);
end;

procedure TZForkSpectrumStyles.SetMirror(AValue: TZForkToneColors);
begin
  FMirror.Assign(AValue);
end;

procedure TZForkSpectrumStyles.SetDots(AValue: TZForkToneColors);
begin
  FDots.Assign(AValue);
end;

procedure TZForkSpectrumStyles.SetLED(AValue: TZForkToneColors);
begin
  FLED.Assign(AValue);
end;

procedure TZForkSpectrumStyles.SetGradient(AValue: TZForkToneColors);
begin
  FGradient.Assign(AValue);
end;

procedure TZForkSpectrumStyles.SetVU(AValue: TZForkToneColors);
begin
  FVU.Assign(AValue);
end;

procedure TZForkSpectrumStyles.SetRainbow(AValue: TZForkRampColors);
begin
  FRainbow.Assign(AValue);
end;

procedure TZForkSpectrumStyles.SetFire(AValue: TZForkRampColors);
begin
  FFire.Assign(AValue);
end;

procedure TZForkSpectrumStyles.SetNeon(AValue: TZForkNeonColors);
begin
  FNeon.Assign(AValue);
end;

procedure TZForkSpectrumStyles.SetWaveGlow(AValue: TZForkWaveColors);
begin
  FWaveGlow.Assign(AValue);
end;

procedure TZForkSpectrumStyles.SetPlasma(AValue: TZForkRampColors);
begin
  FPlasma.Assign(AValue);
end;

{ TZForkSpectrum }

constructor TZForkSpectrum.Create(AOwner: TComponent);
begin
  inherited Create(AOwner);
  ControlStyle := ControlStyle + [csOpaque, csCaptureMouse, csDoubleClicks];
  { LCL-DoubleBuffer + eigenes FBuffer: kein Clear-Flackern auf dem Screen }
  DoubleBuffered := True;
  FAbout := '(About…)';
  Width := 320;
  Height := 80;
  FActive := True;
  FStyle := ssBars;
  FChannels := scMono;
  FBarCount := 64;
  FInterval := 11;
  FTargetFPS := 90;
  FFastDraw := True;
  FShowPeaks := True;
  FPeakDecay := 0.18;
  FIntensity := 6.0;
  FAutoIntensity := True;
  FAutoGain := 1.0;
  FPeakRaw := 0.0;
  FLogScale := False;  { linear = knackigere Peaks }
  FResponse := TZForkSpectrumResponse.Create(Self);
  FSyncMode := ssSyncIdle;
  FIdleHooked := False;
  FLastSampleMs := 0;
  FBuffer := nil;
  FBackColor := clBlack;
  FStyles := TZForkSpectrumStyles.Create(Self);
  FShowGrid := True;
  FGridColor := $4080FF; { orange-ish BGR on Win }
  FGridGlow := True;
  FGridAlpha := 90;
  FGridStepX := 24;
  FGridStepY := 16;
  FAntiAlias := True;
  FTimer := TTimer.Create(Self);
  FTimer.Enabled := False;
  FTimer.Interval := FInterval;
  FTimer.OnTimer := @TimerTick;
  Color := FBackColor;
end;

destructor TZForkSpectrum.Destroy;
begin
  HookIdle(False);
  FTimer.Enabled := False;
  if FPlayer <> nil then
  begin
    FPlayer.UnregisterClient(Self);
    FPlayer := nil;
  end;
  FreeAndNil(FBuffer);
  FreeAndNil(FResponse);
  FreeAndNil(FStyles);
  inherited Destroy;
end;

function TZForkSpectrum.GetAbout: string;
begin
  Result := '(About…)';
end;

procedure TZForkSpectrum.SetAbout(const AValue: string);
begin
  FAbout := AValue;
  if not (csLoading in ComponentState) then
    ShowAbout;
end;

procedure TZForkSpectrum.ShowAbout;
begin
  ShowZForkAbout;
end;

procedure TZForkSpectrum.Loaded;
begin
  inherited Loaded;
  if (FPlayer <> nil) and not (csDesigning in ComponentState) then
    FPlayer.RegisterClient(Self);
  UpdateTimer;
end;

procedure TZForkSpectrum.NotifyPlayerOpen(const AFileName: string);
begin
  Invalidate;
end;

procedure TZForkSpectrum.NotifyPlayerSong(SongIndex: Integer);
begin
  Invalidate;
end;

procedure TZForkSpectrum.NotifyPlayerClose;
begin
  FillChar(FMagL, SizeOf(FMagL), 0);
  FillChar(FMagR, SizeOf(FMagR), 0);
  FVuL := 0;
  FVuR := 0;
  Invalidate;
end;

procedure TZForkSpectrum.NotifyPlayerStop;
begin
  Invalidate;
end;

procedure TZForkSpectrum.Notification(AComponent: TComponent; Operation: TOperation);
begin
  inherited Notification(AComponent, Operation);
  if (Operation = opRemove) and (AComponent = FPlayer) then
    FPlayer := nil;
end;

procedure TZForkSpectrum.SetPlayer(AValue: TZForkPlayer);
begin
  if FPlayer = AValue then Exit;
  if FPlayer <> nil then
  begin
    FPlayer.UnregisterClient(Self);
    FPlayer.RemoveFreeNotification(Self);
  end;
  FPlayer := AValue;
  if FPlayer <> nil then
  begin
    FPlayer.FreeNotification(Self);
    if not (csLoading in ComponentState) then
      FPlayer.RegisterClient(Self);
  end;
  Invalidate;
end;

procedure TZForkSpectrum.SetActive(AValue: Boolean);
begin
  if FActive = AValue then Exit;
  FActive := AValue;
  UpdateTimer;
  if not FActive then Invalidate;
end;

procedure TZForkSpectrum.SetStyle(AValue: TZForkSpectrumStyle);
begin
  if FStyle = AValue then Exit;
  FStyle := AValue;
  Invalidate;
end;

procedure TZForkSpectrum.SetChannels(AValue: TZForkSpectrumChannels);
begin
  if FChannels = AValue then Exit;
  FChannels := AValue;
  Invalidate;
end;

procedure TZForkSpectrum.SetBarCount(AValue: Integer);
begin
  if AValue < 8 then AValue := 8;
  if AValue > ZFORK_SPECTRUM_MAX_BARS then AValue := ZFORK_SPECTRUM_MAX_BARS;
  if FBarCount = AValue then Exit;
  FBarCount := AValue;
  Invalidate;
end;

procedure TZForkSpectrum.SetInterval(AValue: Integer);
begin
  if AValue < 1 then AValue := 1;
  if AValue > 500 then AValue := 500;
  if FInterval = AValue then Exit;
  FInterval := AValue;
  FTimer.Interval := FInterval;
end;

procedure TZForkSpectrum.SetTargetFPS(AValue: Integer);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 240 then AValue := 240;
  if FTargetFPS = AValue then Exit;
  FTargetFPS := AValue;
  if FTargetFPS > 0 then
  begin
    FInterval := Max(1, Round(1000.0 / FTargetFPS));
    FTimer.Interval := FInterval;
  end;
end;

procedure TZForkSpectrum.SetFastDraw(AValue: Boolean);
begin
  if FFastDraw = AValue then Exit;
  FFastDraw := AValue;
  Invalidate;
end;

function TZForkSpectrum.GetFrameGapMs: Integer;
begin
  if FTargetFPS > 0 then
    Result := Max(1, Round(1000.0 / FTargetFPS))
  else
    Result := Max(1, FInterval);
end;

procedure TZForkSpectrum.EnsureBuffer;
var
  W, H: Integer;
begin
  W := Max(1, Width);
  H := Max(1, Height);
  if FBuffer = nil then
    FBuffer := TBitmap.Create;
  if (FBuffer.Width <> W) or (FBuffer.Height <> H) then
  begin
    FBuffer.SetSize(W, H);
    FBuffer.Canvas.Brush.Color := FBackColor;
    FBuffer.Canvas.FillRect(Rect(0, 0, W, H));
  end;
end;

procedure TZForkSpectrum.SetShowPeaks(AValue: Boolean);
begin
  if FShowPeaks = AValue then Exit;
  FShowPeaks := AValue;
  Invalidate;
end;

procedure TZForkSpectrum.SetPeakDecay(AValue: Single);
begin
  if AValue < 0.001 then AValue := 0.001;
  if AValue > 0.5 then AValue := 0.5;
  if Abs(FPeakDecay - AValue) < 1e-6 then Exit;
  FPeakDecay := AValue;
end;

procedure TZForkSpectrum.SetBarColor(AValue: TColor);
begin
  FStyles.Bars.Color := AValue;
end;

function TZForkSpectrum.GetBarColor: TColor;
begin
  Result := FStyles.Bars.Color;
end;

procedure TZForkSpectrum.SetPeakColor(AValue: TColor);
begin
  FStyles.Bars.Peak := AValue;
end;

function TZForkSpectrum.GetPeakColor: TColor;
begin
  Result := FStyles.Bars.Peak;
end;

procedure TZForkSpectrum.SetBackColor(AValue: TColor);
begin
  if FBackColor = AValue then Exit;
  FBackColor := AValue;
  Color := FBackColor;
  Invalidate;
end;

procedure TZForkSpectrum.SetGridColor(AValue: TColor);
begin
  if FGridColor = AValue then Exit;
  FGridColor := AValue;
  Invalidate;
end;

procedure TZForkSpectrum.SetStyles(AValue: TZForkSpectrumStyles);
begin
  FStyles.Assign(AValue);
end;

procedure TZForkSpectrum.SetShowGrid(AValue: Boolean);
begin
  if FShowGrid = AValue then Exit;
  FShowGrid := AValue;
  Invalidate;
end;

procedure TZForkSpectrum.SetGridGlow(AValue: Boolean);
begin
  if FGridGlow = AValue then Exit;
  FGridGlow := AValue;
  Invalidate;
end;

procedure TZForkSpectrum.SetGridAlpha(AValue: Integer);
begin
  if AValue < 0 then AValue := 0;
  if AValue > 255 then AValue := 255;
  if FGridAlpha = AValue then Exit;
  FGridAlpha := AValue;
  Invalidate;
end;

procedure TZForkSpectrum.SetGridStepX(AValue: Integer);
begin
  if AValue < 4 then AValue := 4;
  if AValue > 200 then AValue := 200;
  if FGridStepX = AValue then Exit;
  FGridStepX := AValue;
  Invalidate;
end;

procedure TZForkSpectrum.SetGridStepY(AValue: Integer);
begin
  if AValue < 4 then AValue := 4;
  if AValue > 200 then AValue := 200;
  if FGridStepY = AValue then Exit;
  FGridStepY := AValue;
  Invalidate;
end;

procedure TZForkSpectrum.SetAntiAlias(AValue: Boolean);
begin
  if FAntiAlias = AValue then Exit;
  FAntiAlias := AValue;
  Invalidate;
end;

procedure TZForkSpectrum.SetIntensity(AValue: Single);
begin
  if AValue < 0.1 then AValue := 0.1;
  if AValue > 40.0 then AValue := 40.0;
  if Abs(FIntensity - AValue) < 1e-4 then Exit;
  FIntensity := AValue;
  Invalidate;
end;

procedure TZForkSpectrum.SetAutoIntensity(AValue: Boolean);
begin
  if FAutoIntensity = AValue then Exit;
  FAutoIntensity := AValue;
  if not FAutoIntensity then
    FAutoGain := 1.0;
  Invalidate;
end;

procedure TZForkSpectrum.SetLogScale(AValue: Boolean);
begin
  if FLogScale = AValue then Exit;
  FLogScale := AValue;
  Invalidate;
end;

procedure TZForkSpectrum.SetResponse(AValue: TZForkSpectrumResponse);
begin
  if AValue <> nil then
    FResponse.Assign(AValue);
end;

function TZForkSpectrum.BoostLevel(Raw: Single): Single;
var
  V, Gain: Single;
begin
  Gain := FIntensity;
  if FAutoIntensity then
    Gain := Gain * FAutoGain;
  V := Max(0.0, Raw) * Gain;
  if FLogScale then
    V := Ln(1.0 + V * 14.0) / Ln(15.0)
  else if V > 0 then
    V := Sqrt(V); { Punch: Mitten anheben }
  Result := EnsureRange(V, 0.0, 1.0);
end;

function TZForkSpectrum.SmoothToward(Current, Target: Single): Single;
var
  Speed, Att, Rel, Sm: Single;
begin
  if (FResponse = nil) or FResponse.DirectResponse then
    Exit(Target);
  Att := FResponse.Attack;
  Rel := FResponse.Release;
  Sm := FResponse.Smoothing;
  { Smoothing als Mindest-Glaettung auf beide Richtungen }
  if Sm > Att then Att := Sm;
  if Sm > Rel then Rel := Sm;
  if Target >= Current then
  begin
    if Att <= 1e-6 then
      Exit(Target);
    Speed := 1.0 - Att;
  end
  else
  begin
    if Rel <= 1e-6 then
      Exit(Target);
    Speed := 1.0 - Rel;
  end;
  Result := Current + (Target - Current) * Speed;
end;

procedure TZForkSpectrum.UpdateAutoGain(RawPeak: Single);
var
  Desired, Need: Single;
begin
  if not FAutoIntensity then
  begin
    FAutoGain := 1.0;
    Exit;
  end;
  if RawPeak > FPeakRaw then
    FPeakRaw := FPeakRaw * 0.25 + RawPeak * 0.75
  else
    FPeakRaw := FPeakRaw * 0.94 + RawPeak * 0.06;
  Desired := 0.92;
  if FPeakRaw * FIntensity < 1e-5 then
    Need := 1.0
  else
    Need := Desired / Max(FPeakRaw * FIntensity, 1e-5);
  if Need < 0.4 then Need := 0.4;
  if Need > 16.0 then Need := 16.0;
  if Need > FAutoGain then
    FAutoGain := FAutoGain * 0.55 + Need * 0.45
  else
    FAutoGain := FAutoGain * 0.88 + Need * 0.12;
end;

procedure TZForkSpectrum.SetSyncMode(AValue: TZForkSpectrumSync);
begin
  if FSyncMode = AValue then Exit;
  FSyncMode := AValue;
  UpdateTimer;
end;

procedure TZForkSpectrum.HookIdle(Enable: Boolean);
begin
  if csDesigning in ComponentState then Exit;
  if Enable = FIdleHooked then Exit;
  if Enable then
  begin
    Application.AddOnIdleHandler(@AppIdle, False);
    FIdleHooked := True;
  end
  else
  begin
    Application.RemoveOnIdleHandler(@AppIdle);
    FIdleHooked := False;
  end;
end;

procedure TZForkSpectrum.UpdateTimer;
var
  WantTimer, WantIdle: Boolean;
begin
  { Im Designer KEIN Timer/Idle — sonst friert der Form-Designer ein }
  if csDesigning in ComponentState then
  begin
    FTimer.Enabled := False;
    HookIdle(False);
    Exit;
  end;
  WantTimer := FActive and not (csLoading in ComponentState) and
    (FSyncMode in [ssSyncTimer, ssSyncHybrid]);
  WantIdle := FActive and not (csLoading in ComponentState) and
    (FSyncMode in [ssSyncIdle, ssSyncHybrid]);
  FTimer.Enabled := WantTimer;
  HookIdle(WantIdle);
end;

procedure TZForkSpectrum.DoSampleAndPaint(ForcePaint: Boolean);
begin
  if not FActive then Exit;
  if csDesigning in ComponentState then
    SampleDesignTime
  else
    SampleData;
  DecayPeaks;
  { Immer Invalidate (nicht Repaint): sonst Flackern durch Sofort-Paint/Erase }
  if HandleAllocated and Visible then
    Invalidate;
end;

procedure TZForkSpectrum.TimerTick(Sender: TObject);
begin
  DoSampleAndPaint(FSyncMode = ssSyncTimer);
end;

procedure TZForkSpectrum.AppIdle(Sender: TObject; var Done: Boolean);
var
  NowMs: QWord;
  MinGap: QWord;
begin
  Done := True;
  if not FActive then Exit;
  if not HandleAllocated or not Visible then Exit;
  if (FPlayer = nil) or (FPlayer.Handle = nil) then
    if not (csDesigning in ComponentState) then Exit;

  NowMs := GetTickCount64;
  MinGap := QWord(GetFrameGapMs);
  if FSyncMode = ssSyncHybrid then
    MinGap := Max(QWord(1), MinGap div 2);

  if (FLastSampleMs <> 0) and (NowMs - FLastSampleMs < MinGap) then
    Exit;
  FLastSampleMs := NowMs;
  Inc(FTick);
  DoSampleAndPaint(True);
  Done := False; { weitere Idle-Zyklen anfordern solange aktiv }
end;

procedure TZForkSpectrum.SampleDesignTime;
var
  I: Integer;
  T, V: Single;
begin
  Inc(FTick);
  T := FTick * 0.12;
  for I := 0 to FBarCount - 1 do
  begin
    V := 0.35 + 0.45 * Abs(Sin(T + I * 0.22)) * (0.4 + 0.6 * ((I mod 7) / 7));
    FMagL[I] := V;
    FMagR[I] := V * (0.7 + 0.3 * Abs(Sin(T * 1.3 + I)));
  end;
  FVuL := 0.4 + 0.4 * Abs(Sin(T));
  FVuR := 0.4 + 0.4 * Abs(Sin(T + 1.1));
end;

procedure TZForkSpectrum.SampleData;
var
  L, R: array[0..ZFORK_SPECTRUM_MAX_BARS - 1] of Single;
  I: Integer;
  Ok: Boolean;
  Target, RawPeak: Single;
begin
  if (FPlayer = nil) or (FPlayer.Handle = nil) then
  begin
    FillChar(FMagL, SizeOf(FMagL), 0);
    FillChar(FMagR, SizeOf(FMagR), 0);
    FVuL := 0;
    FVuR := 0;
    Exit;
  end;

  if FStyle = ssVU then
  begin
    Ok := FPlayer.GetVUData(L[0], R[0]);
    if not Ok then
    begin
      FVuL := 0;
      FVuR := 0;
    end
    else
    begin
      RawPeak := Max(L[0], R[0]);
      UpdateAutoGain(RawPeak);
      FVuL := SmoothToward(FVuL, BoostLevel(L[0]));
      FVuR := SmoothToward(FVuR, BoostLevel(R[0]));
    end;
    Exit;
  end;

  FillChar(L, SizeOf(L), 0);
  FillChar(R, SizeOf(R), 0);
  Ok := FPlayer.GetFFTData(@L[0], @R[0], LongWord(FBarCount));
  if not Ok then
  begin
    FillChar(FMagL, SizeOf(FMagL), 0);
    FillChar(FMagR, SizeOf(FMagR), 0);
    Exit;
  end;
  RawPeak := 0;
  for I := 0 to FBarCount - 1 do
  begin
    if L[I] > RawPeak then RawPeak := L[I];
    if R[I] > RawPeak then RawPeak := R[I];
  end;
  UpdateAutoGain(RawPeak);
  for I := 0 to FBarCount - 1 do
  begin
    Target := BoostLevel(L[I]);
    FMagL[I] := SmoothToward(FMagL[I], Target);
    Target := BoostLevel(R[I]);
    FMagR[I] := SmoothToward(FMagR[I], Target);
  end;
end;

procedure TZForkSpectrum.DecayPeaks;
var
  I: Integer;
begin
  if FStyle = ssVU then
  begin
    if FVuL > FVuPeakL then FVuPeakL := FVuL
    else FVuPeakL := Max(0, FVuPeakL - FPeakDecay);
    if FVuR > FVuPeakR then FVuPeakR := FVuR
    else FVuPeakR := Max(0, FVuPeakR - FPeakDecay);
    Exit;
  end;
  for I := 0 to FBarCount - 1 do
  begin
    if FMagL[I] > FPeakL[I] then FPeakL[I] := FMagL[I]
    else FPeakL[I] := Max(0, FPeakL[I] - FPeakDecay);
    if FMagR[I] > FPeakR[I] then FPeakR[I] := FMagR[I]
    else FPeakR[I] := Max(0, FPeakR[I] - FPeakDecay);
  end;
end;

function TZForkSpectrum.MixRGB(C1, C2: TColor; T: Single): TColor;
var
  R1, G1, B1, R2, G2, B2: Integer;
begin
  T := EnsureRange(T, 0, 1);
  C1 := ColorToRGB(C1);
  C2 := ColorToRGB(C2);
  R1 := Red(C1); G1 := Green(C1); B1 := Blue(C1);
  R2 := Red(C2); G2 := Green(C2); B2 := Blue(C2);
  Result := RGBToColor(
    Round(R1 + (R2 - R1) * T),
    Round(G1 + (G2 - G1) * T),
    Round(B1 + (B2 - B1) * T));
end;

function TZForkSpectrum.BlendOnBack(Fg: TColor; Alpha: Integer): TColor;
begin
  Result := MixRGB(FBackColor, Fg, Alpha / 255.0);
end;

procedure TZForkSpectrum.SoftLine(ACanvas: TCanvas; X1, Y1, X2, Y2: Integer; Col: TColor);
begin
  if FAntiAlias and not FFastDraw then
  begin
    ACanvas.Pen.Width := 5;
    ACanvas.Pen.Color := BlendOnBack(Col, 35);
    ACanvas.Line(X1, Y1, X2, Y2);
    ACanvas.Pen.Width := 3;
    ACanvas.Pen.Color := BlendOnBack(Col, 90);
    ACanvas.Line(X1, Y1, X2, Y2);
    ACanvas.Pen.Width := 2;
    ACanvas.Pen.Color := BlendOnBack(Col, 160);
    ACanvas.Line(X1, Y1, X2, Y2);
  end;
  ACanvas.Pen.Width := 1;
  ACanvas.Pen.Color := Col;
  ACanvas.Line(X1, Y1, X2, Y2);
end;

procedure TZForkSpectrum.SoftEllipse(ACanvas: TCanvas; X, Y, R: Integer; Col: TColor);
begin
  if FAntiAlias and not FFastDraw and (R > 1) then
  begin
    ACanvas.Brush.Color := BlendOnBack(Col, 45);
    ACanvas.Pen.Style := psClear;
    ACanvas.Ellipse(X - R - 3, Y - R - 3, X + R + 3, Y + R + 3);
    ACanvas.Brush.Color := BlendOnBack(Col, 110);
    ACanvas.Ellipse(X - R - 1, Y - R - 1, X + R + 1, Y + R + 1);
  end;
  ACanvas.Brush.Color := Col;
  ACanvas.Pen.Style := psClear;
  ACanvas.Ellipse(X - R, Y - R, X + R, Y + R);
  ACanvas.Pen.Style := psSolid;
end;

function TZForkSpectrum.HueColor(Hue: Single; Bright: Single): TColor;
var
  H, S, V, F, P, Q, T, Rf, Gf, Bf: Single;
  I: Integer;
begin
  H := Hue - Floor(Hue);
  if H < 0 then
    H := H + 1.0;
  S := 1.0;
  V := EnsureRange(Bright, 0.25, 1.0);
  H := H * 6.0;
  I := Trunc(H) mod 6;
  F := H - Trunc(H);
  P := V * (1.0 - S);
  Q := V * (1.0 - S * F);
  T := V * (1.0 - S * (1.0 - F));
  case I of
    0: begin Rf := V; Gf := T; Bf := P; end;
    1: begin Rf := Q; Gf := V; Bf := P; end;
    2: begin Rf := P; Gf := V; Bf := T; end;
    3: begin Rf := P; Gf := Q; Bf := V; end;
    4: begin Rf := T; Gf := P; Bf := V; end;
  else
    begin Rf := V; Gf := P; Bf := Q; end;
  end;
  Result := RGBToColor(
    EnsureRange(Round(Rf * 255), 0, 255),
    EnsureRange(Round(Gf * 255), 0, 255),
    EnsureRange(Round(Bf * 255), 0, 255));
end;

function TZForkSpectrum.PlasmaTint(Hue, HeightT: Single): TColor;
var
  T: Single;
  Base: TColor;
begin
  T := Hue - Floor(Hue);
  if T < 0 then
    T := T + 1.0;
  if T < (1.0 / 3.0) then
    Base := MixRGB(FStyles.Plasma.Low, FStyles.Plasma.Mid, T * 3.0)
  else if T < (2.0 / 3.0) then
    Base := MixRGB(FStyles.Plasma.Mid, FStyles.Plasma.High, (T - 1.0 / 3.0) * 3.0)
  else
    Base := MixRGB(FStyles.Plasma.High, FStyles.Plasma.Low, (T - 2.0 / 3.0) * 3.0);
  Result := MixRGB(Base, FStyles.Plasma.High, EnsureRange(HeightT, 0, 1) * 0.45);
end;

procedure TZForkSpectrum.PaintGrid(ACanvas: TCanvas; const ARect: TRect);
var
  X, Y: Integer;
  LineCol, GlowCol: TColor;
begin
  if not FShowGrid then Exit;
  if FGridAlpha <= 0 then Exit;
  LineCol := BlendOnBack(FGridColor, FGridAlpha);
  GlowCol := BlendOnBack(FGridColor, Max(20, FGridAlpha div 3));

  ACanvas.Brush.Style := bsClear;
  if FGridGlow and FAntiAlias and not FFastDraw then
  begin
    ACanvas.Pen.Width := 3;
    ACanvas.Pen.Color := GlowCol;
    X := ARect.Left;
    while X < ARect.Right do
    begin
      ACanvas.Line(X, ARect.Top, X, ARect.Bottom);
      Inc(X, FGridStepX);
    end;
    Y := ARect.Top;
    while Y < ARect.Bottom do
    begin
      ACanvas.Line(ARect.Left, Y, ARect.Right, Y);
      Inc(Y, FGridStepY);
    end;
  end;

  ACanvas.Pen.Width := 1;
  ACanvas.Pen.Color := LineCol;
  X := ARect.Left;
  while X < ARect.Right do
  begin
    ACanvas.Line(X, ARect.Top, X, ARect.Bottom);
    Inc(X, FGridStepX);
  end;
  Y := ARect.Top;
  while Y < ARect.Bottom do
  begin
    ACanvas.Line(ARect.Left, Y, ARect.Right, Y);
    Inc(Y, FGridStepY);
  end;
  ACanvas.Brush.Style := bsSolid;
end;

procedure TZForkSpectrum.EraseBackground(DC: HDC);
begin
  { kein System-Clear — sonst Flackern zwischen Erase und Paint }
end;

procedure TZForkSpectrum.Paint;
var
  R: TRect;
  Split: Boolean;
  DC: TCanvas;
begin
  { Designer-Vorschau ohne Timer }
  if (csDesigning in ComponentState) and FActive then
    SampleDesignTime;

  R := ClientRect;
  EnsureBuffer;
  DC := FBuffer.Canvas;
  if FAntiAlias and not FFastDraw then
    DC.AntialiasingMode := amOn
  else
    DC.AntialiasingMode := amOff;

  DC.Brush.Color := FBackColor;
  DC.FillRect(R);
  PaintGrid(DC, R);

  DC.Pen.Width := 1;
  DC.Pen.Color := BlendOnBack(FGridColor, Max(FGridAlpha, 40));
  DC.Brush.Style := bsClear;
  DC.Rectangle(R.Left, R.Top, R.Right, R.Bottom);
  DC.Brush.Style := bsSolid;

  Split := (FChannels = scStereo) and (FStyle <> ssVU) and (FStyle <> ssMirror);
  case FStyle of
    ssBars:     PaintBars(DC, R, Split);
    ssFilled:   PaintFilled(DC, R, Split);
    ssVU:       PaintVU(DC, R);
    ssLine:     PaintLine(DC, R, Split);
    ssMirror:   PaintMirror(DC, R);
    ssDots:     PaintDots(DC, R, Split);
    ssLED:      PaintLED(DC, R, Split);
    ssGradient: PaintGradient(DC, R, Split);
    ssRainbow:  PaintRainbow(DC, R, Split);
    ssFire:     PaintFire(DC, R, Split);
    ssNeon:     PaintNeon(DC, R, Split);
    ssWaveGlow: PaintWaveGlow(DC, R, Split);
    ssPlasma:   PaintPlasma(DC, R, Split);
  end;

  Canvas.CopyRect(R, FBuffer.Canvas, R);
end;

procedure TZForkSpectrum.PaintBars(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
var
  I, N, Gap, BarW, X, Y0, Y1, H, Mid, PeakY: Integer;
  V: Single;
  Band: TRect;
  TopCol: TColor;
begin
  N := FBarCount;
  if N < 1 then Exit;
  Band := ARect;
  InflateRect(Band, -2, -2);
  Gap := IfThen(FAntiAlias, 2, 1);
  Mid := (Band.Top + Band.Bottom) div 2;
  BarW := Max(1, ((Band.Right - Band.Left) - (N - 1) * Gap) div N);

  for I := 0 to N - 1 do
  begin
    X := Band.Left + I * (BarW + Gap);
    if StereoSplit then
    begin
      V := FMagL[I];
      H := Round((Mid - Band.Top - 1) * V);
      Y1 := Mid - 1;
      Y0 := Y1 - H;
      ACanvas.Brush.Color := FStyles.Bars.Color;
      ACanvas.Pen.Style := psClear;
      ACanvas.FillRect(X, Y0, X + BarW, Y1);
      if FAntiAlias and (H > 2) then
      begin
        TopCol := MixRGB(FStyles.Bars.Color, FStyles.Bars.Peak, 0.45);
        ACanvas.Brush.Color := TopCol;
        ACanvas.FillRect(X, Y0, X + BarW, Min(Y0 + 3, Y1));
      end;
      if FShowPeaks then
      begin
        PeakY := Mid - 1 - Round((Mid - Band.Top - 1) * FPeakL[I]);
        SoftLine(ACanvas, X, PeakY, X + BarW - 1, PeakY, FStyles.Bars.Peak);
      end;
      V := FMagR[I];
      H := Round((Band.Bottom - Mid - 1) * V);
      Y0 := Mid + 1;
      Y1 := Y0 + H;
      ACanvas.Brush.Color := FStyles.Bars.Color;
      ACanvas.FillRect(X, Y0, X + BarW, Y1);
      if FShowPeaks then
      begin
        PeakY := Mid + 1 + Round((Band.Bottom - Mid - 1) * FPeakR[I]);
        SoftLine(ACanvas, X, PeakY, X + BarW - 1, PeakY, FStyles.Bars.Peak);
      end;
    end
    else
    begin
      V := (FMagL[I] + FMagR[I]) * 0.5;
      H := Round((Band.Bottom - Band.Top) * V);
      Y1 := Band.Bottom;
      Y0 := Y1 - H;
      ACanvas.Brush.Color := FStyles.Bars.Color;
      ACanvas.Pen.Style := psClear;
      ACanvas.FillRect(X, Y0, X + BarW, Y1);
      if FAntiAlias and (H > 2) then
      begin
        ACanvas.Brush.Color := MixRGB(FStyles.Bars.Color, FStyles.Bars.Peak, 0.4);
        ACanvas.FillRect(X, Y0, X + BarW, Min(Y0 + 3, Y1));
      end;
      if FShowPeaks then
      begin
        PeakY := Band.Bottom - Round((Band.Bottom - Band.Top) * ((FPeakL[I] + FPeakR[I]) * 0.5));
        SoftLine(ACanvas, X, PeakY, X + BarW - 1, PeakY, FStyles.Bars.Peak);
      end;
    end;
  end;
  ACanvas.Pen.Style := psSolid;
  if StereoSplit then
    SoftLine(ACanvas, Band.Left, Mid, Band.Right, Mid, BlendOnBack(FGridColor, Max(FGridAlpha, 100)));
end;

procedure TZForkSpectrum.PaintFilled(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
var
  I, N, Mid: Integer;
  Band: TRect;
  Pts: array of TPoint;
  V: Single;
begin
  N := FBarCount;
  if N < 2 then Exit;
  Band := ARect;
  InflateRect(Band, -2, -2);
  ACanvas.Brush.Color := BlendOnBack(FStyles.Filled.Color, IfThen(FAntiAlias, 200, 255));
  ACanvas.Pen.Color := FStyles.Filled.Color;
  ACanvas.Pen.Width := IfThen(FAntiAlias, 2, 1);

  if StereoSplit then
  begin
    Mid := (Band.Top + Band.Bottom) div 2;
    SetLength(Pts, N + 2);
    Pts[0] := Point(Band.Left, Mid - 1);
    for I := 0 to N - 1 do
    begin
      V := FMagL[I];
      Pts[I + 1] := Point(
        Band.Left + (I * (Band.Right - Band.Left)) div (N - 1),
        Mid - 1 - Round((Mid - Band.Top - 1) * V));
    end;
    Pts[N + 1] := Point(Band.Right, Mid - 1);
    ACanvas.Polygon(Pts);

    Pts[0] := Point(Band.Left, Mid + 1);
    for I := 0 to N - 1 do
    begin
      V := FMagR[I];
      Pts[I + 1] := Point(
        Band.Left + (I * (Band.Right - Band.Left)) div (N - 1),
        Mid + 1 + Round((Band.Bottom - Mid - 1) * V));
    end;
    Pts[N + 1] := Point(Band.Right, Mid + 1);
    ACanvas.Polygon(Pts);
    SoftLine(ACanvas, Band.Left, Mid, Band.Right, Mid, BlendOnBack(FGridColor, Max(FGridAlpha, 100)));
  end
  else
  begin
    SetLength(Pts, N + 2);
    Pts[0] := Point(Band.Left, Band.Bottom);
    for I := 0 to N - 1 do
    begin
      V := (FMagL[I] + FMagR[I]) * 0.5;
      Pts[I + 1] := Point(
        Band.Left + (I * (Band.Right - Band.Left)) div (N - 1),
        Band.Bottom - Round((Band.Bottom - Band.Top) * V));
    end;
    Pts[N + 1] := Point(Band.Right, Band.Bottom);
    ACanvas.Polygon(Pts);
  end;
  ACanvas.Pen.Width := 1;
end;

procedure TZForkSpectrum.PaintLine(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
var
  I, N, Mid, X0, Y0, X1, Y1: Integer;
  Band: TRect;
  V: Single;
begin
  N := FBarCount;
  if N < 2 then Exit;
  Band := ARect;
  InflateRect(Band, -2, -2);
  Mid := (Band.Top + Band.Bottom) div 2;

  if StereoSplit then
  begin
    for I := 0 to N - 2 do
    begin
      X0 := Band.Left + (I * (Band.Right - Band.Left)) div (N - 1);
      X1 := Band.Left + ((I + 1) * (Band.Right - Band.Left)) div (N - 1);
      Y0 := Mid - 1 - Round((Mid - Band.Top - 1) * FMagL[I]);
      Y1 := Mid - 1 - Round((Mid - Band.Top - 1) * FMagL[I + 1]);
      SoftLine(ACanvas, X0, Y0, X1, Y1, FStyles.Line.Color);
      Y0 := Mid + 1 + Round((Band.Bottom - Mid - 1) * FMagR[I]);
      Y1 := Mid + 1 + Round((Band.Bottom - Mid - 1) * FMagR[I + 1]);
      SoftLine(ACanvas, X0, Y0, X1, Y1, MixRGB(FStyles.Line.Color, FStyles.Line.Peak, 0.35));
    end;
    SoftLine(ACanvas, Band.Left, Mid, Band.Right, Mid, BlendOnBack(FGridColor, Max(FGridAlpha, 100)));
  end
  else
  begin
    for I := 0 to N - 2 do
    begin
      X0 := Band.Left + (I * (Band.Right - Band.Left)) div (N - 1);
      X1 := Band.Left + ((I + 1) * (Band.Right - Band.Left)) div (N - 1);
      V := (FMagL[I] + FMagR[I]) * 0.5;
      Y0 := Band.Bottom - Round((Band.Bottom - Band.Top) * V);
      V := (FMagL[I + 1] + FMagR[I + 1]) * 0.5;
      Y1 := Band.Bottom - Round((Band.Bottom - Band.Top) * V);
      SoftLine(ACanvas, X0, Y0, X1, Y1, FStyles.Line.Color);
    end;
  end;
end;

procedure TZForkSpectrum.PaintMirror(ACanvas: TCanvas; const ARect: TRect);
var
  I, N, Gap, BarW, X, Mid, H: Integer;
  Band: TRect;
  V: Single;
begin
  N := FBarCount;
  if N < 1 then Exit;
  Band := ARect;
  InflateRect(Band, -2, -2);
  Mid := (Band.Top + Band.Bottom) div 2;
  Gap := IfThen(FAntiAlias, 2, 1);
  BarW := Max(1, ((Band.Right - Band.Left) - (N - 1) * Gap) div N);
  ACanvas.Pen.Style := psClear;
  for I := 0 to N - 1 do
  begin
    X := Band.Left + I * (BarW + Gap);
    V := (FMagL[I] + FMagR[I]) * 0.5;
    H := Round(((Band.Bottom - Band.Top) div 2 - 1) * V);
    ACanvas.Brush.Color := FStyles.Mirror.Color;
    ACanvas.FillRect(X, Mid - H, X + BarW, Mid);
    ACanvas.Brush.Color := MixRGB(FStyles.Mirror.Color, FBackColor, 0.25);
    ACanvas.FillRect(X, Mid, X + BarW, Mid + H);
    if FAntiAlias and (H > 2) then
    begin
      ACanvas.Brush.Color := MixRGB(FStyles.Mirror.Color, FStyles.Mirror.Peak, 0.5);
      ACanvas.FillRect(X, Mid - H, X + BarW, Mid - H + 2);
    end;
  end;
  ACanvas.Pen.Style := psSolid;
  SoftLine(ACanvas, Band.Left, Mid, Band.Right, Mid, BlendOnBack(FStyles.Mirror.Peak, 180));
end;

procedure TZForkSpectrum.PaintDots(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
var
  I, N, Mid, X, Y, Rad: Integer;
  Band: TRect;
  V: Single;
begin
  N := FBarCount;
  if N < 1 then Exit;
  Band := ARect;
  InflateRect(Band, -4, -4);
  Mid := (Band.Top + Band.Bottom) div 2;
  Rad := Max(2, Min(6, (Band.Right - Band.Left) div (N * 3)));
  for I := 0 to N - 1 do
  begin
    X := Band.Left + (I * (Band.Right - Band.Left)) div Max(1, N - 1);
    if StereoSplit then
    begin
      Y := Mid - 1 - Round((Mid - Band.Top - 1) * FMagL[I]);
      SoftEllipse(ACanvas, X, Y, Rad, FStyles.Dots.Color);
      Y := Mid + 1 + Round((Band.Bottom - Mid - 1) * FMagR[I]);
      SoftEllipse(ACanvas, X, Y, Rad, MixRGB(FStyles.Dots.Color, FStyles.Dots.Peak, 0.4));
    end
    else
    begin
      V := (FMagL[I] + FMagR[I]) * 0.5;
      Y := Band.Bottom - Round((Band.Bottom - Band.Top) * V);
      SoftEllipse(ACanvas, X, Y, Rad, FStyles.Dots.Color);
      if FShowPeaks then
      begin
        V := (FPeakL[I] + FPeakR[I]) * 0.5;
        Y := Band.Bottom - Round((Band.Bottom - Band.Top) * V);
        SoftEllipse(ACanvas, X, Y, Max(1, Rad - 1), FStyles.Dots.Peak);
      end;
    end;
  end;
end;

procedure TZForkSpectrum.PaintLED(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
var
  I, N, Gap, BarW, X, Mid, Segs, S, SegH, OnCount, Y: Integer;
  Band, Cell: TRect;
  V: Single;
  Lit: TColor;
begin
  N := FBarCount;
  if N < 1 then Exit;
  Band := ARect;
  InflateRect(Band, -2, -2);
  Gap := 2;
  Mid := (Band.Top + Band.Bottom) div 2;
  BarW := Max(2, ((Band.Right - Band.Left) - (N - 1) * Gap) div N);
  Segs := Max(6, (Band.Bottom - Band.Top) div 10);
  ACanvas.Pen.Style := psClear;

  for I := 0 to N - 1 do
  begin
    X := Band.Left + I * (BarW + Gap);
    if StereoSplit then
    begin
      V := FMagL[I];
      OnCount := Round(Segs * V);
      SegH := Max(2, (Mid - Band.Top - Segs) div Segs);
      for S := 0 to Segs - 1 do
      begin
        Y := Mid - 2 - (S + 1) * (SegH + 1);
        Cell := Rect(X, Y, X + BarW, Y + SegH);
        if S < OnCount then
          Lit := MixRGB(FStyles.LED.Color, FStyles.LED.Peak, S / Max(1, Segs - 1))
        else
          Lit := BlendOnBack(FStyles.LED.Color, 35);
        ACanvas.Brush.Color := Lit;
        ACanvas.FillRect(Cell);
      end;
      V := FMagR[I];
      OnCount := Round(Segs * V);
      SegH := Max(2, (Band.Bottom - Mid - Segs) div Segs);
      for S := 0 to Segs - 1 do
      begin
        Y := Mid + 2 + S * (SegH + 1);
        Cell := Rect(X, Y, X + BarW, Y + SegH);
        if S < OnCount then
          Lit := MixRGB(FStyles.LED.Color, FStyles.LED.Peak, S / Max(1, Segs - 1))
        else
          Lit := BlendOnBack(FStyles.LED.Color, 35);
        ACanvas.Brush.Color := Lit;
        ACanvas.FillRect(Cell);
      end;
    end
    else
    begin
      V := (FMagL[I] + FMagR[I]) * 0.5;
      OnCount := Round(Segs * V);
      SegH := Max(2, (Band.Bottom - Band.Top - Segs) div Segs);
      for S := 0 to Segs - 1 do
      begin
        Y := Band.Bottom - (S + 1) * (SegH + 1);
        Cell := Rect(X, Y, X + BarW, Y + SegH);
        if S < OnCount then
          Lit := MixRGB(FStyles.LED.Color, FStyles.LED.Peak, S / Max(1, Segs - 1))
        else
          Lit := BlendOnBack(FStyles.LED.Color, 35);
        ACanvas.Brush.Color := Lit;
        ACanvas.FillRect(Cell);
      end;
    end;
  end;
  ACanvas.Pen.Style := psSolid;
end;

procedure TZForkSpectrum.PaintGradient(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
var
  I, N, Gap, BarW, X, Y0, Y1, H, Mid, YY: Integer;
  Band: TRect;
  V, T: Single;
  C: TColor;
begin
  N := FBarCount;
  if N < 1 then Exit;
  Band := ARect;
  InflateRect(Band, -2, -2);
  Gap := IfThen(FAntiAlias, 2, 1);
  Mid := (Band.Top + Band.Bottom) div 2;
  BarW := Max(1, ((Band.Right - Band.Left) - (N - 1) * Gap) div N);
  ACanvas.Pen.Style := psClear;

  for I := 0 to N - 1 do
  begin
    X := Band.Left + I * (BarW + Gap);
    if StereoSplit then
    begin
      V := FMagL[I];
      H := Round((Mid - Band.Top - 1) * V);
      Y1 := Mid - 1;
      Y0 := Y1 - H;
      for YY := Y0 to Y1 - 1 do
      begin
        T := (Y1 - YY) / Max(1, H);
        C := MixRGB(FStyles.Gradient.Color, FStyles.Gradient.Peak, T);
        ACanvas.Brush.Color := C;
        ACanvas.FillRect(X, YY, X + BarW, YY + 1);
      end;
      V := FMagR[I];
      H := Round((Band.Bottom - Mid - 1) * V);
      Y0 := Mid + 1;
      Y1 := Y0 + H;
      for YY := Y0 to Y1 - 1 do
      begin
        T := (YY - Y0) / Max(1, H);
        C := MixRGB(FStyles.Gradient.Color, FStyles.Gradient.Peak, T);
        ACanvas.Brush.Color := C;
        ACanvas.FillRect(X, YY, X + BarW, YY + 1);
      end;
    end
    else
    begin
      V := (FMagL[I] + FMagR[I]) * 0.5;
      H := Round((Band.Bottom - Band.Top) * V);
      Y1 := Band.Bottom;
      Y0 := Y1 - H;
      for YY := Y0 to Y1 - 1 do
      begin
        T := (Y1 - YY) / Max(1, H);
        C := MixRGB(FStyles.Gradient.Color, FStyles.Gradient.Peak, EnsureRange(T, 0, 1));
        ACanvas.Brush.Color := C;
        ACanvas.FillRect(X, YY, X + BarW, YY + 1);
      end;
    end;
  end;
  ACanvas.Pen.Style := psSolid;
end;

procedure TZForkSpectrum.PaintRainbow(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
var
  I, N, Gap, BarW, X, Y0, Y1, H, Mid: Integer;
  Band: TRect;
  V, T: Single;
  C: TColor;
begin
  N := FBarCount;
  if N < 1 then Exit;
  Band := ARect;
  InflateRect(Band, -2, -2);
  Gap := IfThen(FAntiAlias, 2, 1);
  Mid := (Band.Top + Band.Bottom) div 2;
  BarW := Max(1, ((Band.Right - Band.Left) - (N - 1) * Gap) div N);
  ACanvas.Pen.Style := psClear;
  for I := 0 to N - 1 do
  begin
    X := Band.Left + I * (BarW + Gap);
    begin
      T := I / Max(1, N - 1);
      if T < 0.5 then
        C := MixRGB(FStyles.Rainbow.Low, FStyles.Rainbow.Mid, T * 2.0)
      else
        C := MixRGB(FStyles.Rainbow.Mid, FStyles.Rainbow.High, (T - 0.5) * 2.0);
    end;
    if StereoSplit then
    begin
      V := FMagL[I];
      H := Round((Mid - Band.Top - 1) * V);
      ACanvas.Brush.Color := C;
      ACanvas.FillRect(X, Mid - 1 - H, X + BarW, Mid - 1);
      if FAntiAlias and (H > 2) then
      begin
        ACanvas.Brush.Color := BlendOnBack(C, 80);
        ACanvas.FillRect(X - 1, Mid - 1 - H - 1, X + BarW + 1, Mid - H);
      end;
      V := FMagR[I];
      H := Round((Band.Bottom - Mid - 1) * V);
      ACanvas.Brush.Color := C;
      ACanvas.FillRect(X, Mid + 1, X + BarW, Mid + 1 + H);
    end
    else
    begin
      V := (FMagL[I] + FMagR[I]) * 0.5;
      H := Round((Band.Bottom - Band.Top) * V);
      Y1 := Band.Bottom;
      Y0 := Y1 - H;
      if FAntiAlias and (H > 3) then
      begin
        ACanvas.Brush.Color := BlendOnBack(C, 70);
        ACanvas.FillRect(X - 1, Y0 - 1, X + BarW + 1, Y1);
      end;
      ACanvas.Brush.Color := C;
      ACanvas.FillRect(X, Y0, X + BarW, Y1);
      if FShowPeaks then
        SoftLine(ACanvas, X, Band.Bottom - Round((Band.Bottom - Band.Top) *
          ((FPeakL[I] + FPeakR[I]) * 0.5)), X + BarW - 1,
          Band.Bottom - Round((Band.Bottom - Band.Top) *
          ((FPeakL[I] + FPeakR[I]) * 0.5)), MixRGB(C, FStyles.Rainbow.High, 0.5));
    end;
  end;
  ACanvas.Pen.Style := psSolid;
end;

procedure TZForkSpectrum.PaintFire(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
var
  I, N, Gap, BarW, X, Y0, Y1, H, Mid, YY: Integer;
  Band: TRect;
  V, T: Single;
  C, CLow, CMid, CHi: TColor;
begin
  N := FBarCount;
  if N < 1 then Exit;
  Band := ARect;
  InflateRect(Band, -2, -2);
  Gap := IfThen(FAntiAlias, 1, 1);
  Mid := (Band.Top + Band.Bottom) div 2;
  BarW := Max(1, ((Band.Right - Band.Left) - (N - 1) * Gap) div N);
  CLow := FStyles.Fire.Low;
  CMid := FStyles.Fire.Mid;
  CHi := FStyles.Fire.High;
  ACanvas.Pen.Style := psClear;
  for I := 0 to N - 1 do
  begin
    X := Band.Left + I * (BarW + Gap);
    if StereoSplit then
      V := FMagL[I]
    else
      V := (FMagL[I] + FMagR[I]) * 0.5;
    if StereoSplit then
    begin
      H := Round((Mid - Band.Top - 1) * V);
      Y1 := Mid - 1;
      Y0 := Y1 - H;
      for YY := Y0 to Y1 - 1 do
      begin
        T := (Y1 - YY) / Max(1.0, H);
        if T < 0.5 then
          C := MixRGB(CLow, CMid, T * 2)
        else
          C := MixRGB(CMid, CHi, (T - 0.5) * 2);
        ACanvas.Brush.Color := C;
        ACanvas.FillRect(X, YY, X + BarW, YY + 1);
      end;
      V := FMagR[I];
      H := Round((Band.Bottom - Mid - 1) * V);
      Y0 := Mid + 1;
      Y1 := Y0 + H;
      for YY := Y0 to Y1 - 1 do
      begin
        T := (YY - Y0) / Max(1.0, H);
        if T < 0.5 then
          C := MixRGB(CLow, CMid, T * 2)
        else
          C := MixRGB(CMid, CHi, (T - 0.5) * 2);
        ACanvas.Brush.Color := C;
        ACanvas.FillRect(X, YY, X + BarW, YY + 1);
      end;
    end
    else
    begin
      H := Round((Band.Bottom - Band.Top) * V);
      Y1 := Band.Bottom;
      Y0 := Y1 - H;
      for YY := Max(Y0, Band.Top) to Y1 - 1 do
      begin
        T := (Y1 - YY) / Max(1.0, H);
        if T < 0.45 then
          C := MixRGB(CLow, CMid, T / 0.45)
        else
          C := MixRGB(CMid, CHi, (T - 0.45) / 0.55);
        ACanvas.Brush.Color := C;
        ACanvas.FillRect(X, YY, X + BarW, YY + 1);
      end;
      if FAntiAlias and (H > 4) then
      begin
        ACanvas.Brush.Color := BlendOnBack(CHi, 100);
        ACanvas.FillRect(X, Y0, X + BarW, Min(Y0 + 3, Y1));
      end;
    end;
  end;
  ACanvas.Pen.Style := psSolid;
end;

procedure TZForkSpectrum.PaintNeon(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
var
  I, N, Gap, BarW, X, Y0, Y1, H, Mid: Integer;
  Band: TRect;
  V: Single;
  C, Glow: TColor;
begin
  N := FBarCount;
  if N < 1 then Exit;
  Band := ARect;
  InflateRect(Band, -3, -3);
  Gap := IfThen(FAntiAlias, 3, 2);
  Mid := (Band.Top + Band.Bottom) div 2;
  BarW := Max(2, ((Band.Right - Band.Left) - (N - 1) * Gap) div N);
  for I := 0 to N - 1 do
  begin
    X := Band.Left + I * (BarW + Gap);
    C := MixRGB(FStyles.Neon.Color, FStyles.Neon.Glow,
      0.15 + 0.2 * (0.5 + 0.5 * Sin(FTick * 0.05 + I * 0.15)));
    Glow := MixRGB(C, FStyles.Neon.Glow, 0.45);
    if StereoSplit then
      V := FMagL[I]
    else
      V := (FMagL[I] + FMagR[I]) * 0.5;
    H := Round((Band.Bottom - Band.Top) * V);
    if StereoSplit then
      H := Round((Mid - Band.Top - 1) * V);
    if StereoSplit then
    begin
      Y1 := Mid - 1;
      Y0 := Y1 - H;
    end
    else
    begin
      Y1 := Band.Bottom;
      Y0 := Y1 - H;
    end;
    if FAntiAlias and not FFastDraw and (H > 2) then
    begin
      ACanvas.Pen.Style := psClear;
      ACanvas.Brush.Color := BlendOnBack(C, 50);
      ACanvas.FillRect(X - 2, Y0 - 2, X + BarW + 2, Y1);
      ACanvas.Brush.Color := BlendOnBack(Glow, 120);
      ACanvas.FillRect(X - 1, Y0 - 1, X + BarW + 1, Y1);
    end;
    ACanvas.Pen.Style := psClear;
    ACanvas.Brush.Color := C;
    ACanvas.FillRect(X, Y0, X + BarW, Y1);
    ACanvas.Brush.Color := Glow;
    ACanvas.FillRect(X, Y0, X + BarW, Min(Y0 + 2, Y1));
    if (not StereoSplit) and FShowPeaks and not FFastDraw then
      SoftLine(ACanvas, X, Band.Bottom - Round((Band.Bottom - Band.Top) *
        ((FPeakL[I] + FPeakR[I]) * 0.5)), X + BarW - 1,
        Band.Bottom - Round((Band.Bottom - Band.Top) *
        ((FPeakL[I] + FPeakR[I]) * 0.5)), Glow);
  end;
  ACanvas.Pen.Style := psSolid;
end;

procedure TZForkSpectrum.PaintWaveGlow(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
var
  I, N, Mid, X0, Y0, X1, Y1: Integer;
  Band: TRect;
  Pts: array of TPoint;
  V: Single;
  C: TColor;
begin
  N := FBarCount;
  if N < 2 then Exit;
  Band := ARect;
  InflateRect(Band, -2, -2);
  Mid := (Band.Top + Band.Bottom) div 2;
  C := FStyles.WaveGlow.Color;
  ACanvas.Brush.Color := BlendOnBack(C, IfThen(FAntiAlias, 140, 200));
  ACanvas.Pen.Style := psClear;
  SetLength(Pts, N + 2);
  if StereoSplit then
  begin
    Pts[0] := Point(Band.Left, Mid);
    for I := 0 to N - 1 do
    begin
      V := FMagL[I];
      Pts[I + 1] := Point(Band.Left + (I * (Band.Right - Band.Left)) div (N - 1),
        Mid - Round((Mid - Band.Top) * V));
    end;
    Pts[N + 1] := Point(Band.Right, Mid);
    ACanvas.Polygon(Pts);
  end
  else
  begin
    Pts[0] := Point(Band.Left, Band.Bottom);
    for I := 0 to N - 1 do
    begin
      V := (FMagL[I] + FMagR[I]) * 0.5;
      Pts[I + 1] := Point(Band.Left + (I * (Band.Right - Band.Left)) div (N - 1),
        Band.Bottom - Round((Band.Bottom - Band.Top) * V));
    end;
    Pts[N + 1] := Point(Band.Right, Band.Bottom);
    ACanvas.Polygon(Pts);
  end;
  for I := 0 to N - 2 do
  begin
    X0 := Band.Left + (I * (Band.Right - Band.Left)) div (N - 1);
    X1 := Band.Left + ((I + 1) * (Band.Right - Band.Left)) div (N - 1);
    if StereoSplit then
    begin
      Y0 := Mid - Round((Mid - Band.Top) * FMagL[I]);
      Y1 := Mid - Round((Mid - Band.Top) * FMagL[I + 1]);
    end
    else
    begin
      Y0 := Band.Bottom - Round((Band.Bottom - Band.Top) * ((FMagL[I] + FMagR[I]) * 0.5));
      Y1 := Band.Bottom - Round((Band.Bottom - Band.Top) * ((FMagL[I + 1] + FMagR[I + 1]) * 0.5));
    end;
    SoftLine(ACanvas, X0, Y0, X1, Y1, MixRGB(C, FStyles.WaveGlow.Peak, 0.4));
  end;
  ACanvas.Pen.Style := psSolid;
end;

procedure TZForkSpectrum.PaintPlasma(ACanvas: TCanvas; const ARect: TRect; StereoSplit: Boolean);
var
  I, N, Gap, BarW, X, Y0, Y1, H, Mid, YY: Integer;
  Band: TRect;
  V, T, Hue: Single;
  C: TColor;
begin
  N := FBarCount;
  if N < 1 then Exit;
  Band := ARect;
  InflateRect(Band, -2, -2);
  Gap := IfThen(FAntiAlias, 2, 1);
  Mid := (Band.Top + Band.Bottom) div 2;
  BarW := Max(1, ((Band.Right - Band.Left) - (N - 1) * Gap) div N);
  ACanvas.Pen.Style := psClear;
  for I := 0 to N - 1 do
  begin
    X := Band.Left + I * (BarW + Gap);
    Hue := Frac(I / Max(1.0, N) + FTick * 0.008);
    if StereoSplit then
      V := FMagL[I]
    else
      V := (FMagL[I] + FMagR[I]) * 0.5;
    H := Round((Band.Bottom - Band.Top) * V);
    if StereoSplit then
      H := Round((Mid - Band.Top - 1) * V);
    if StereoSplit then
    begin
      Y1 := Mid - 1;
      Y0 := Y1 - H;
    end
    else
    begin
      Y1 := Band.Bottom;
      Y0 := Y1 - H;
    end;
    for YY := Max(Y0, Band.Top) to Y1 - 1 do
    begin
      T := (Y1 - YY) / Max(1.0, H);
      C := PlasmaTint(Hue + T * 0.15, 0.55 + 0.45 * T);
      ACanvas.Brush.Color := C;
      ACanvas.FillRect(X, YY, X + BarW, YY + 1);
    end;
    if FAntiAlias and (H > 3) then
    begin
      ACanvas.Brush.Color := BlendOnBack(PlasmaTint(Hue, 1.0), 90);
      ACanvas.FillRect(X - 1, Y0 - 1, X + BarW + 1, Y0 + 2);
    end;
  end;
  ACanvas.Pen.Style := psSolid;
end;

procedure TZForkSpectrum.PaintVU(ACanvas: TCanvas; const ARect: TRect);
var
  Band, LRect, RRect: TRect;
  Mid, W: Integer;
  procedure DrawMeter(const M: TRect; Level, Peak: Single; const Caption: string);
  var
    FillW, PeakX, XX: Integer;
    T: Single;
  begin
    ACanvas.Brush.Color := BlendOnBack(FGridColor, 50);
    ACanvas.Pen.Style := psClear;
    ACanvas.FillRect(M);
    FillW := Round(Max(0.0, Min(1.0, Level)) * (M.Right - M.Left));
    for XX := 0 to FillW - 1 do
    begin
      T := XX / Max(1, M.Right - M.Left);
      ACanvas.Brush.Color := MixRGB(FStyles.VU.Color, FStyles.VU.Peak, T);
      ACanvas.FillRect(M.Left + XX, M.Top, M.Left + XX + 1, M.Bottom);
    end;
    if FShowPeaks then
    begin
      PeakX := M.Left + Round(Max(0.0, Min(1.0, Peak)) * (M.Right - M.Left));
      SoftLine(ACanvas, PeakX, M.Top, PeakX, M.Bottom - 1, FStyles.VU.Peak);
    end;
    ACanvas.Font.Color := FStyles.VU.Peak;
    ACanvas.Brush.Style := bsClear;
    ACanvas.TextOut(M.Left + 4, M.Top + ((M.Bottom - M.Top) - ACanvas.TextHeight(Caption)) div 2, Caption);
    ACanvas.Brush.Style := bsSolid;
    ACanvas.Pen.Style := psSolid;
  end;
begin
  Band := ARect;
  InflateRect(Band, -4, -4);
  Mid := (Band.Top + Band.Bottom) div 2;
  W := Band.Right - Band.Left;
  LRect := Rect(Band.Left, Band.Top, Band.Left + W, Mid - 2);
  RRect := Rect(Band.Left, Mid + 2, Band.Left + W, Band.Bottom);
  DrawMeter(LRect, FVuL, FVuPeakL, 'L');
  DrawMeter(RRect, FVuR, FVuPeakR, 'R');
end;

end.
