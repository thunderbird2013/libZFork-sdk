{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  TZForkPlaylist — Owner-Draw-Playlist mit Mini-Peaks

  Was die Komponente kann:
  - Liste mit Titel/Artist/BPM/Dauer und Peak-Vorschau
  - Dateien/Ordner hinzufuegen, DropFiles, Kontextmenue
  - Doppelklick / Play startet am gebundenen TZForkPlayer
  - Async-Analyse im Hintergrund; Remove/Clear/Reorder
******************************************************************************}
unit ZForkPlaylist;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Math, Controls, Graphics, ExtCtrls, StdCtrls, Menus, Forms,
  LCLType, LCLIntf, LMessages, Windows, ShellAPI, Dialogs, ZForkPlayer,
  ZForkAbout, LibZFork;

type
  TZForkPlaylist = class;
  TZForkPlaylistAnalyzeThread = class;

  TZForkPlaylistItem = class(TCollectionItem)
  private
    FFileName: string;
    FTitle: string;
    FArtist: string;
    FDurationMs: LongWord;
    FBpm: Single;
    FColor: TColor;
    FPeaks: array of Byte;
    FAnalyzing: Boolean;
    FGen: Integer;
    procedure SetFileName(const AValue: string);
    procedure SetTitle(const AValue: string);
    procedure SetArtist(const AValue: string);
    procedure SetDurationMs(AValue: LongWord);
    procedure SetBpm(AValue: Single);
    procedure SetColor(AValue: TColor);
    function GetPeakCount: Integer;
  protected
    function GetDisplayName: string; override;
  public
    constructor Create(ACollection: TCollection); override;
    property Analyzing: Boolean read FAnalyzing;
    property PeakCount: Integer read GetPeakCount;
  published
    property FileName: string read FFileName write SetFileName;
    property Title: string read FTitle write SetTitle;
    property Artist: string read FArtist write SetArtist;
    property DurationMs: LongWord read FDurationMs write SetDurationMs default 0;
    property Bpm: Single read FBpm write SetBpm;
    property Color: TColor read FColor write SetColor default clAqua;
  end;

  TZForkPlaylistItems = class(TOwnedCollection)
  private
    function GetItem(Index: Integer): TZForkPlaylistItem;
    procedure SetItem(Index: Integer; AValue: TZForkPlaylistItem);
  protected
    procedure Update(Item: TCollectionItem); override;
  public
    property Items[Index: Integer]: TZForkPlaylistItem read GetItem write SetItem; default;
  end;

  TZForkPlaylist = class(TCustomControl)
  private
    FPlayer: TZForkPlayer;
    FItems: TZForkPlaylistItems;
    FAbout: string;
    FActive: Boolean;
    FRowHeight: Integer;
    FPeakWidth: Integer;
    FPeakColor: TColor;
    FBackColor: TColor;
    FSelColor: TColor;
    FTextColor: TColor;
    FShowBpm: Boolean;
    FShowDuration: Boolean;
    FAllowDropFiles: Boolean;
    FAutoAnalyze: Boolean;
    FDoubleClickPlay: Boolean;
    FSelected: Integer;
    FScrollY: Integer;
    FVScroll: TScrollBar;
    FUpdatingScroll: Boolean;
    FPopup: TPopupMenu;
    FOnChange: TNotifyEvent;
    FOnSelect: TNotifyEvent;
    procedure SetPlayer(AValue: TZForkPlayer);
    procedure SetItems(AValue: TZForkPlaylistItems);
    procedure SetActive(AValue: Boolean);
    procedure SetRowHeight(AValue: Integer);
    procedure SetPeakWidth(AValue: Integer);
    procedure SetPeakColor(AValue: TColor);
    procedure SetBackColor(AValue: TColor);
    procedure SetSelColor(AValue: TColor);
    procedure SetTextColor(AValue: TColor);
    procedure SetShowBpm(AValue: Boolean);
    procedure SetShowDuration(AValue: Boolean);
    procedure SetAllowDropFiles(AValue: Boolean);
    procedure SetAutoAnalyze(AValue: Boolean);
    procedure SetDoubleClickPlay(AValue: Boolean);
    function GetAbout: string;
    procedure SetAbout(const AValue: string);
    procedure Changed;
    procedure UpdateVScroll;
    procedure VScrollChange(Sender: TObject);
    procedure StartAnalyzeItem(Item: TZForkPlaylistItem);
    procedure ApplyItemAnalysis(Item: TZForkPlaylistItem; const Peaks: array of Byte;
      PeakCount: Integer; Bpm: Single; LengthMs: LongWord);
    function ContentHeight: Integer;
    function HitItem(Y: Integer): Integer;
    function ItemRect(Index: Integer): TRect;
    procedure DrawItem(ACanvas: TCanvas; Index: Integer; const R: TRect);
    procedure DrawPeaks(ACanvas: TCanvas; const R: TRect; Item: TZForkPlaylistItem);
    procedure BuildPopup;
    procedure PopupPlay(Sender: TObject);
    procedure PopupRemove(Sender: TObject);
    procedure PopupClear(Sender: TObject);
    procedure PopupAddFiles(Sender: TObject);
    procedure HandleHDROP(Drop: HDROP);
    function IsAudioFile(const Path: string): Boolean;
  protected
    procedure Notification(AComponent: TComponent; Operation: TOperation); override;
    procedure Paint; override;
    procedure CreateWnd; override;
    procedure DestroyWnd; override;
    procedure WndProc(var Message: TLMessage); override;
    procedure Resize; override;
    procedure MouseDown(Button: TMouseButton; Shift: TShiftState; X, Y: Integer); override;
    procedure DblClick; override;
    function DoMouseWheel(Shift: TShiftState; WheelDelta: Integer;
      MousePos: TPoint): Boolean; override;
    procedure Loaded; override;
  public
    constructor Create(AOwner: TComponent); override;
    destructor Destroy; override;
    procedure ShowAbout;
    function AddFile(const Path: string): TZForkPlaylistItem;
    procedure AddFiles(const Paths: array of string);
    procedure DropFilesAt(const FileNames: array of string);
    procedure Clear;
    procedure PlaySelected;
    procedure PlayIndex(Index: Integer);
    function LoadFolder(const Dir: string; Recursive: Boolean = False): Integer;
    procedure BrowseAddFiles;
    property SelectedIndex: Integer read FSelected;
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
    property Items: TZForkPlaylistItems read FItems write SetItems;
    property Active: Boolean read FActive write SetActive default True;
    property RowHeight: Integer read FRowHeight write SetRowHeight default 36;
    property PeakWidth: Integer read FPeakWidth write SetPeakWidth default 120;
    property PeakColor: TColor read FPeakColor write SetPeakColor default clAqua;
    property BackColor: TColor read FBackColor write SetBackColor default clBlack;
    property SelColor: TColor read FSelColor write SetSelColor default clNavy;
    property TextColor: TColor read FTextColor write SetTextColor default clWhite;
    property ShowBpm: Boolean read FShowBpm write SetShowBpm default True;
    property ShowDuration: Boolean read FShowDuration write SetShowDuration default True;
    property AllowDropFiles: Boolean read FAllowDropFiles write SetAllowDropFiles default True;
    property AutoAnalyze: Boolean read FAutoAnalyze write SetAutoAnalyze default True;
    property DoubleClickPlay: Boolean read FDoubleClickPlay write SetDoubleClickPlay default True;
    property OnChange: TNotifyEvent read FOnChange write FOnChange;
    property OnSelect: TNotifyEvent read FOnSelect write FOnSelect;
  end;

  TZForkPlaylistAnalyzeThread = class(TThread)
  private
    FList: TZForkPlaylist;
    FItem: TZForkPlaylistItem;
    FGen: Integer;
    FPath: string;
    FOk: Boolean;
    FBpm: Single;
    FLengthMs: LongWord;
    FPeaks: array of Byte;
    procedure ApplyResult;
  protected
    procedure Execute; override;
  public
    constructor Create(AList: TZForkPlaylist; AItem: TZForkPlaylistItem;
      AGen: Integer; const APath: string);
  end;

implementation

const
  PL_PEAK_BINS = 128;
  PL_RAW_BINS  = 1024;

function MkRect(L, T, R, B: Integer): TRect;
begin
  Result.Left := L;
  Result.Top := T;
  Result.Right := R;
  Result.Bottom := B;
end;

{ TZForkPlaylistAnalyzeThread }

constructor TZForkPlaylistAnalyzeThread.Create(AList: TZForkPlaylist;
  AItem: TZForkPlaylistItem; AGen: Integer; const APath: string);
begin
  inherited Create(True);
  FreeOnTerminate := True;
  FList := AList;
  FItem := AItem;
  FGen := AGen;
  FPath := APath;
  FOk := False;
  FBpm := 0;
  FLengthMs := 0;
end;

procedure TZForkPlaylistAnalyzeThread.Execute;
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
    SetLength(Raw, PL_RAW_BINS * 3);
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
        SetLength(FPeaks, PL_PEAK_BINS);
        for I := 0 to PL_PEAK_BINS - 1 do
        begin
          Src := (I * TimeBins) div PL_PEAK_BINS;
          if Src >= TimeBins then Src := TimeBins - 1;
          J := Src * 3;
          K := Raw[J];
          M := Raw[J + 1];
          Hi := Raw[J + 2];
          Peak := K;
          if M > Peak then Peak := M;
          if Hi > Peak then Peak := Hi;
          FPeaks[I] := Byte(Peak);
        end;
        FOk := True;
      end;
    finally
      zfork_FreeAnalysis(H, Analysis);
    end;
  finally
    zfork_Destroy(H);
  end;
  if not Terminated and (FList <> nil) then
    Synchronize(@ApplyResult);
end;

procedure TZForkPlaylistAnalyzeThread.ApplyResult;
begin
  if (FList = nil) or (FItem = nil) then Exit;
  if FGen <> FItem.FGen then Exit;
  if FOk then
    FList.ApplyItemAnalysis(FItem, FPeaks, Length(FPeaks), FBpm, FLengthMs)
  else
  begin
    FItem.FAnalyzing := False;
    FList.Invalidate;
  end;
end;

{ TZForkPlaylistItem }

constructor TZForkPlaylistItem.Create(ACollection: TCollection);
begin
  inherited Create(ACollection);
  FColor := clAqua;
  FBpm := 0;
  FDurationMs := 0;
  FAnalyzing := False;
  FGen := 0;
  SetLength(FPeaks, 0);
end;

function TZForkPlaylistItem.GetPeakCount: Integer;
begin
  Result := Length(FPeaks);
end;

function TZForkPlaylistItem.GetDisplayName: string;
begin
  if FTitle <> '' then
    Result := FTitle
  else if FFileName <> '' then
    Result := ExtractFileName(FFileName)
  else
    Result := 'Item ' + IntToStr(Index);
end;

procedure TZForkPlaylistItem.SetFileName(const AValue: string);
begin
  if FFileName = AValue then Exit;
  FFileName := AValue;
  if FTitle = '' then
    FTitle := ChangeFileExt(ExtractFileName(AValue), '');
  Changed(True);
end;

procedure TZForkPlaylistItem.SetTitle(const AValue: string);
begin
  if FTitle = AValue then Exit;
  FTitle := AValue;
  Changed(True);
end;

procedure TZForkPlaylistItem.SetArtist(const AValue: string);
begin
  if FArtist = AValue then Exit;
  FArtist := AValue;
  Changed(True);
end;

procedure TZForkPlaylistItem.SetDurationMs(AValue: LongWord);
begin
  if FDurationMs = AValue then Exit;
  FDurationMs := AValue;
  Changed(True);
end;

procedure TZForkPlaylistItem.SetBpm(AValue: Single);
begin
  if Abs(FBpm - AValue) < 0.01 then Exit;
  FBpm := AValue;
  Changed(True);
end;

procedure TZForkPlaylistItem.SetColor(AValue: TColor);
begin
  if FColor = AValue then Exit;
  FColor := AValue;
  Changed(True);
end;

{ TZForkPlaylistItems }

function TZForkPlaylistItems.GetItem(Index: Integer): TZForkPlaylistItem;
begin
  Result := TZForkPlaylistItem(inherited Items[Index]);
end;

procedure TZForkPlaylistItems.SetItem(Index: Integer; AValue: TZForkPlaylistItem);
begin
  inherited Items[Index] := AValue;
end;

procedure TZForkPlaylistItems.Update(Item: TCollectionItem);
begin
  inherited Update(Item);
  if Owner is TZForkPlaylist then
  begin
    TZForkPlaylist(Owner).UpdateVScroll;
    TZForkPlaylist(Owner).Invalidate;
  end;
end;

{ TZForkPlaylist }

constructor TZForkPlaylist.Create(AOwner: TComponent);
begin
  inherited Create(AOwner);
  ControlStyle := ControlStyle + [csOpaque, csDoubleClicks, csDisplayDragImage];
  Width := 320;
  Height := 200;
  FActive := True;
  FAbout := '(About…)';
  FRowHeight := 36;
  FPeakWidth := 120;
  FPeakColor := clAqua;
  FBackColor := RGBToColor(24, 24, 28);
  FSelColor := RGBToColor(40, 70, 120);
  FTextColor := clWhite;
  FShowBpm := True;
  FShowDuration := True;
  FAllowDropFiles := True;
  FAutoAnalyze := True;
  FDoubleClickPlay := True;
  FSelected := -1;
  FScrollY := 0;
  FUpdatingScroll := False;
  FItems := TZForkPlaylistItems.Create(Self, TZForkPlaylistItem);
  FVScroll := TScrollBar.Create(Self);
  FVScroll.Parent := Self;
  FVScroll.Kind := sbVertical;
  FVScroll.Align := alRight;
  FVScroll.Width := 16;
  FVScroll.TabStop := False;
  FVScroll.OnChange := @VScrollChange;
  BuildPopup;
  Color := FBackColor;
  DoubleBuffered := True;
  TabStop := True;
end;

destructor TZForkPlaylist.Destroy;
begin
  FreeAndNil(FPopup);
  FreeAndNil(FItems);
  inherited Destroy;
end;

procedure TZForkPlaylist.BuildPopup;
var
  M: TMenuItem;
begin
  FPopup := TPopupMenu.Create(Self);
  M := TMenuItem.Create(FPopup);
  M.Caption := 'Play';
  M.OnClick := @PopupPlay;
  FPopup.Items.Add(M);
  M := TMenuItem.Create(FPopup);
  M.Caption := 'Dateien hinzufügen…';
  M.OnClick := @PopupAddFiles;
  FPopup.Items.Add(M);
  M := TMenuItem.Create(FPopup);
  M.Caption := 'Entfernen';
  M.OnClick := @PopupRemove;
  FPopup.Items.Add(M);
  M := TMenuItem.Create(FPopup);
  M.Caption := 'Liste leeren';
  M.OnClick := @PopupClear;
  FPopup.Items.Add(M);
  PopupMenu := FPopup;
end;

function TZForkPlaylist.GetAbout: string;
begin
  Result := FAbout;
end;

procedure TZForkPlaylist.SetAbout(const AValue: string);
begin
  ShowAbout;
end;

procedure TZForkPlaylist.ShowAbout;
begin
  ShowZForkAbout;
end;

procedure TZForkPlaylist.Changed;
begin
  if Assigned(FOnChange) then FOnChange(Self);
end;

procedure TZForkPlaylist.Loaded;
begin
  inherited Loaded;
  if HandleAllocated and not (csDesigning in ComponentState) and FAllowDropFiles then
    DragAcceptFiles(Handle, True);
  UpdateVScroll;
end;

procedure TZForkPlaylist.Notification(AComponent: TComponent; Operation: TOperation);
begin
  inherited Notification(AComponent, Operation);
  if (Operation = opRemove) and (AComponent = FPlayer) then
    FPlayer := nil;
end;

procedure TZForkPlaylist.CreateWnd;
begin
  inherited CreateWnd;
  if not (csDesigning in ComponentState) and FAllowDropFiles then
    DragAcceptFiles(Handle, True);
end;

procedure TZForkPlaylist.DestroyWnd;
begin
  if HandleAllocated then
    DragAcceptFiles(Handle, False);
  inherited DestroyWnd;
end;

procedure TZForkPlaylist.WndProc(var Message: TLMessage);
begin
  if (Message.msg = Integer(WM_DROPFILES)) and FAllowDropFiles then
  begin
    HandleHDROP(HDROP(Message.wParam));
    Message.Result := 0;
    Exit;
  end;
  inherited WndProc(Message);
end;

procedure TZForkPlaylist.HandleHDROP(Drop: HDROP);
var
  N, I: Integer;
  Len: Integer;
  Buf: array[0..MAX_PATH] of WideChar;
  Names: array of string;
begin
  N := DragQueryFileW(Drop, $FFFFFFFF, nil, 0);
  SetLength(Names, 0);
  for I := 0 to N - 1 do
  begin
    Len := DragQueryFileW(Drop, I, @Buf[0], MAX_PATH);
    if Len > 0 then
    begin
      SetLength(Names, Length(Names) + 1);
      Names[High(Names)] := string(WideCharLenToString(@Buf[0], Len));
    end;
  end;
  DragFinish(Drop);
  if Length(Names) > 0 then
    DropFilesAt(Names);
end;

function TZForkPlaylist.IsAudioFile(const Path: string): Boolean;
var
  Ext: string;
begin
  Ext := LowerCase(ExtractFileExt(Path));
  Result := (Ext = '.mp3') or (Ext = '.wav') or (Ext = '.ogg') or
    (Ext = '.flac') or (Ext = '.m4a') or (Ext = '.aac') or (Ext = '.wma') or
    (Ext = '.aiff') or (Ext = '.aif') or (Ext = '.opus') or (Ext = '.mp2') or
    (Ext = '.mp4') or (Ext = '.cda');
end;

procedure TZForkPlaylist.SetPlayer(AValue: TZForkPlayer);
begin
  if FPlayer = AValue then Exit;
  if FPlayer <> nil then FPlayer.RemoveFreeNotification(Self);
  FPlayer := AValue;
  if FPlayer <> nil then FPlayer.FreeNotification(Self);
end;

procedure TZForkPlaylist.SetItems(AValue: TZForkPlaylistItems);
begin
  if AValue <> nil then
    FItems.Assign(AValue);
end;

procedure TZForkPlaylist.SetActive(AValue: Boolean);
begin
  FActive := AValue;
end;

procedure TZForkPlaylist.SetRowHeight(AValue: Integer);
begin
  if AValue < 20 then AValue := 20;
  if AValue > 80 then AValue := 80;
  if FRowHeight = AValue then Exit;
  FRowHeight := AValue;
  UpdateVScroll;
  Invalidate;
end;

procedure TZForkPlaylist.SetPeakWidth(AValue: Integer);
begin
  if AValue < 40 then AValue := 40;
  if AValue > 400 then AValue := 400;
  if FPeakWidth = AValue then Exit;
  FPeakWidth := AValue;
  Invalidate;
end;

procedure TZForkPlaylist.SetPeakColor(AValue: TColor);
begin
  if FPeakColor = AValue then Exit;
  FPeakColor := AValue;
  Invalidate;
end;

procedure TZForkPlaylist.SetBackColor(AValue: TColor);
begin
  if FBackColor = AValue then Exit;
  FBackColor := AValue;
  Color := AValue;
  Invalidate;
end;

procedure TZForkPlaylist.SetSelColor(AValue: TColor);
begin
  if FSelColor = AValue then Exit;
  FSelColor := AValue;
  Invalidate;
end;

procedure TZForkPlaylist.SetTextColor(AValue: TColor);
begin
  if FTextColor = AValue then Exit;
  FTextColor := AValue;
  Invalidate;
end;

procedure TZForkPlaylist.SetShowBpm(AValue: Boolean);
begin
  if FShowBpm = AValue then Exit;
  FShowBpm := AValue;
  Invalidate;
end;

procedure TZForkPlaylist.SetShowDuration(AValue: Boolean);
begin
  if FShowDuration = AValue then Exit;
  FShowDuration := AValue;
  Invalidate;
end;

procedure TZForkPlaylist.SetAllowDropFiles(AValue: Boolean);
begin
  FAllowDropFiles := AValue;
  if HandleAllocated and not (csDesigning in ComponentState) then
    DragAcceptFiles(Handle, AValue);
end;

procedure TZForkPlaylist.SetAutoAnalyze(AValue: Boolean);
begin
  FAutoAnalyze := AValue;
end;

procedure TZForkPlaylist.SetDoubleClickPlay(AValue: Boolean);
begin
  FDoubleClickPlay := AValue;
end;

function TZForkPlaylist.ContentHeight: Integer;
begin
  Result := FItems.Count * FRowHeight;
end;

procedure TZForkPlaylist.UpdateVScroll;
var
  MaxS, ViewH: Integer;
begin
  if FVScroll = nil then Exit;
  ViewH := Height;
  MaxS := Max(0, ContentHeight - ViewH);
  FUpdatingScroll := True;
  try
    if FScrollY > MaxS then FScrollY := MaxS;
    if FScrollY < 0 then FScrollY := 0;
    FVScroll.Min := 0;
    FVScroll.Max := MaxS;
    FVScroll.Enabled := MaxS > 0;
    if FVScroll.Position <> FScrollY then
      FVScroll.Position := FScrollY;
  finally
    FUpdatingScroll := False;
  end;
end;

procedure TZForkPlaylist.VScrollChange(Sender: TObject);
begin
  if FUpdatingScroll or (FVScroll = nil) then Exit;
  FScrollY := FVScroll.Position;
  Invalidate;
end;

procedure TZForkPlaylist.Resize;
begin
  inherited Resize;
  UpdateVScroll;
end;

function TZForkPlaylist.HitItem(Y: Integer): Integer;
var
  Idx: Integer;
begin
  Result := -1;
  Idx := (Y + FScrollY) div Max(1, FRowHeight);
  if (Idx >= 0) and (Idx < FItems.Count) then
    Result := Idx;
end;

function TZForkPlaylist.ItemRect(Index: Integer): TRect;
var
  Y: Integer;
  W: Integer;
begin
  Y := Index * FRowHeight - FScrollY;
  W := Width;
  if (FVScroll <> nil) and FVScroll.Visible then
    W := Width - FVScroll.Width;
  Result := MkRect(0, Y, W, Y + FRowHeight);
end;

procedure TZForkPlaylist.StartAnalyzeItem(Item: TZForkPlaylistItem);
var
  Th: TZForkPlaylistAnalyzeThread;
begin
  if (Item = nil) or (Item.FileName = '') then Exit;
  if csDesigning in ComponentState then Exit;
  if not FAutoAnalyze then Exit;
  Inc(Item.FGen);
  Item.FAnalyzing := True;
  SetLength(Item.FPeaks, 0);
  Th := TZForkPlaylistAnalyzeThread.Create(Self, Item, Item.FGen, Item.FileName);
  Th.Start;
end;

procedure TZForkPlaylist.ApplyItemAnalysis(Item: TZForkPlaylistItem;
  const Peaks: array of Byte; PeakCount: Integer; Bpm: Single; LengthMs: LongWord);
var
  I: Integer;
begin
  if Item = nil then Exit;
  SetLength(Item.FPeaks, PeakCount);
  for I := 0 to PeakCount - 1 do
    Item.FPeaks[I] := Peaks[I];
  if Bpm > 40 then Item.FBpm := Bpm;
  if LengthMs > 0 then Item.FDurationMs := LengthMs;
  Item.FAnalyzing := False;
  Invalidate;
  Changed;
end;

function TZForkPlaylist.AddFile(const Path: string): TZForkPlaylistItem;
begin
  Result := nil;
  if not FileExists(Path) then Exit;
  if not IsAudioFile(Path) then Exit;
  Result := TZForkPlaylistItem(FItems.Add);
  Result.FileName := Path;
  Result.Title := ChangeFileExt(ExtractFileName(Path), '');
  Result.Color := FPeakColor;
  StartAnalyzeItem(Result);
  if FSelected < 0 then FSelected := Result.Index;
  UpdateVScroll;
  Invalidate;
  Changed;
end;

procedure TZForkPlaylist.AddFiles(const Paths: array of string);
var
  I: Integer;
begin
  for I := 0 to High(Paths) do
    AddFile(Paths[I]);
end;

procedure TZForkPlaylist.DropFilesAt(const FileNames: array of string);
begin
  AddFiles(FileNames);
end;

procedure TZForkPlaylist.Clear;
begin
  FItems.Clear;
  FSelected := -1;
  FScrollY := 0;
  UpdateVScroll;
  Invalidate;
  Changed;
end;

procedure TZForkPlaylist.PlayIndex(Index: Integer);
var
  It: TZForkPlaylistItem;
begin
  if (FPlayer = nil) or (Index < 0) or (Index >= FItems.Count) then Exit;
  It := FItems[Index];
  if It.FileName = '' then Exit;
  if not FileExists(It.FileName) then Exit;
  if FPlayer.OpenFile(It.FileName) then
    FPlayer.Play;
  FSelected := Index;
  Invalidate;
  if Assigned(FOnSelect) then FOnSelect(Self);
end;

procedure TZForkPlaylist.PlaySelected;
begin
  PlayIndex(FSelected);
end;

function TZForkPlaylist.LoadFolder(const Dir: string; Recursive: Boolean): Integer;
var
  Rec: TSearchRec;
  Code: Integer;
begin
  Result := 0;
  if not DirectoryExists(Dir) then Exit;
  Code := FindFirst(IncludeTrailingPathDelimiter(Dir) + '*.*', faAnyFile, Rec);
  try
    while Code = 0 do
    begin
      if (Rec.Name <> '.') and (Rec.Name <> '..') then
      begin
        if (Rec.Attr and faDirectory) <> 0 then
        begin
          if Recursive then
            Inc(Result, LoadFolder(IncludeTrailingPathDelimiter(Dir) + Rec.Name, True));
        end
        else if IsAudioFile(Rec.Name) then
        begin
          if AddFile(IncludeTrailingPathDelimiter(Dir) + Rec.Name) <> nil then
            Inc(Result);
        end;
      end;
      Code := FindNext(Rec);
    end;
  finally
    SysUtils.FindClose(Rec);
  end;
end;

procedure TZForkPlaylist.PopupPlay(Sender: TObject);
begin
  PlaySelected;
end;

procedure TZForkPlaylist.PopupRemove(Sender: TObject);
begin
  if (FSelected < 0) or (FSelected >= FItems.Count) then Exit;
  FItems.Delete(FSelected);
  if FSelected >= FItems.Count then
    FSelected := FItems.Count - 1;
  UpdateVScroll;
  Invalidate;
  Changed;
end;

procedure TZForkPlaylist.PopupClear(Sender: TObject);
begin
  Clear;
end;

procedure TZForkPlaylist.PopupAddFiles(Sender: TObject);
begin
  BrowseAddFiles;
end;

procedure TZForkPlaylist.BrowseAddFiles;
var
  Dlg: TOpenDialog;
  I: Integer;
begin
  Dlg := TOpenDialog.Create(Self);
  try
    Dlg.Title := 'Audio-Dateien hinzufügen';
    Dlg.Options := Dlg.Options + [ofAllowMultiSelect, ofFileMustExist];
    Dlg.Filter :=
      'Audio|*.mp3;*.wav;*.ogg;*.flac;*.m4a;*.aac;*.wma;*.aiff;*.aif;*.opus|Alle|*.*';
    if Dlg.Execute then
      for I := 0 to Dlg.Files.Count - 1 do
        AddFile(Dlg.Files[I]);
  finally
    Dlg.Free;
  end;
end;

procedure TZForkPlaylist.DrawPeaks(ACanvas: TCanvas; const R: TRect;
  Item: TZForkPlaylistItem);
var
  W, H, MidY, X, Idx, Amp, N: Integer;
  Col: TColor;
begin
  if (Item = nil) or (Length(Item.FPeaks) < 2) then Exit;
  W := R.Right - R.Left;
  H := R.Bottom - R.Top;
  if (W < 4) or (H < 4) then Exit;
  MidY := (R.Top + R.Bottom) div 2;
  N := Length(Item.FPeaks);
  if Item.Color <> clDefault then
    Col := Item.Color
  else
    Col := FPeakColor;
  ACanvas.Pen.Color := Col;
  ACanvas.Pen.Width := 1;
  for X := 0 to W - 1 do
  begin
    Idx := (X * N) div W;
    if Idx >= N then Idx := N - 1;
    Amp := (Integer(Item.FPeaks[Idx]) * (H div 2 - 1)) div 255;
    if Amp < 1 then Continue;
    ACanvas.MoveTo(R.Left + X, MidY - Amp);
    ACanvas.LineTo(R.Left + X, MidY + Amp);
  end;
end;

procedure TZForkPlaylist.DrawItem(ACanvas: TCanvas; Index: Integer; const R: TRect);
var
  It: TZForkPlaylistItem;
  PeakR, TextR: TRect;
  Line, Dur: string;
  Sec: Integer;
begin
  if (Index < 0) or (Index >= FItems.Count) then Exit;
  It := FItems[Index];
  if Index = FSelected then
    ACanvas.Brush.Color := FSelColor
  else if Odd(Index) then
    ACanvas.Brush.Color := RGBToColor(28, 28, 32)
  else
    ACanvas.Brush.Color := FBackColor;
  ACanvas.FillRect(R);

  PeakR := MkRect(R.Left + 4, R.Top + 4, R.Left + 4 + FPeakWidth, R.Bottom - 4);
  if It.Analyzing then
  begin
    ACanvas.Font.Color := clGray;
    ACanvas.Brush.Style := bsClear;
    ACanvas.TextOut(PeakR.Left, PeakR.Top + 4, '…');
    ACanvas.Brush.Style := bsSolid;
  end
  else
    DrawPeaks(ACanvas, PeakR, It);

  TextR := MkRect(PeakR.Right + 8, R.Top + 4, R.Right - 4, R.Bottom - 4);
  ACanvas.Font.Color := FTextColor;
  ACanvas.Font.Height := -11;
  ACanvas.Brush.Style := bsClear;
  Line := It.DisplayName;
  if It.Artist <> '' then
    Line := It.Artist + ' — ' + Line;
  if FShowBpm and (It.Bpm > 40) then
    Line := Line + Format('  %.0f BPM', [It.Bpm]);
  if FShowDuration and (It.DurationMs > 0) then
  begin
    Sec := Integer(It.DurationMs div 1000);
    Dur := Format('%d:%02d', [Sec div 60, Sec mod 60]);
    Line := Line + '  ' + Dur;
  end;
  ACanvas.TextRect(TextR, TextR.Left, TextR.Top + 2, Line);
  ACanvas.Brush.Style := bsSolid;

  ACanvas.Pen.Color := RGBToColor(50, 50, 56);
  ACanvas.MoveTo(R.Left, R.Bottom - 1);
  ACanvas.LineTo(R.Right, R.Bottom - 1);
end;

procedure TZForkPlaylist.Paint;
var
  I: Integer;
  R: TRect;
  First, Last: Integer;
begin
  Canvas.Brush.Color := FBackColor;
  Canvas.FillRect(ClientRect);
  if FItems.Count = 0 then
  begin
    Canvas.Font.Color := clGray;
    Canvas.Font.Height := -11;
    Canvas.Brush.Style := bsClear;
    Canvas.TextOut(12, 12, 'Playlist — Dateien hierher ziehen oder Rechtsklick');
    Canvas.Brush.Style := bsSolid;
    Exit;
  end;
  First := Max(0, FScrollY div Max(1, FRowHeight) - 1);
  Last := Min(FItems.Count - 1, (FScrollY + Height) div Max(1, FRowHeight) + 1);
  for I := First to Last do
  begin
    R := ItemRect(I);
    if R.Bottom < 0 then Continue;
    if R.Top > Height then Continue;
    DrawItem(Canvas, I, R);
  end;
end;

procedure TZForkPlaylist.MouseDown(Button: TMouseButton; Shift: TShiftState;
  X, Y: Integer);
var
  Idx: Integer;
begin
  inherited MouseDown(Button, Shift, X, Y);
  if CanFocus and not Focused then SetFocus;
  Idx := HitItem(Y);
  if Idx >= 0 then
  begin
    FSelected := Idx;
    Invalidate;
    if Assigned(FOnSelect) then FOnSelect(Self);
  end;
end;

procedure TZForkPlaylist.DblClick;
begin
  inherited DblClick;
  if FDoubleClickPlay then
    PlaySelected;
end;

function TZForkPlaylist.DoMouseWheel(Shift: TShiftState; WheelDelta: Integer;
  MousePos: TPoint): Boolean;
begin
  Result := True;
  if WheelDelta > 0 then
    FScrollY := Max(0, FScrollY - FRowHeight * 2)
  else
    FScrollY := Min(Max(0, ContentHeight - Height), FScrollY + FRowHeight * 2);
  UpdateVScroll;
  Invalidate;
end;

end.
