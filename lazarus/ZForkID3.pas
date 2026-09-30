{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  TZForkID3 — ID3-Tags und Cover-Art anzeigen/laden

  Was die Komponente kann:
  - Tags aus Datei oder aktuellem Player-Song laden (Load / LoadFromPlayer)
  - Cover als TPicture (JPEG/PNG), AutoLoad und AutoSync am Player
  - Read-only Felder: Title, Artist, Album, Year, Comment, Genre, BPM, …
  - AlbumArtist, Composer, Copyright, URL, Publisher
  - Reagiert als IZForkPlayerClient auf Open/NextSong/Close
******************************************************************************}
unit ZForkID3;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Graphics, LibZFork, ZForkIntf, ZForkPlayer;

type
  TZForkID3 = class(TComponent, IZForkPlayerClient)
  private
    FHandle: TZFORK_HANDLE;
    FPlayer: TZForkPlayer;
    FFileName: string;
    FAutoLoad: Boolean;
    FAutoSync: Boolean;
    FTitle: string;
    FArtist: string;
    FAlbum: string;
    FYear: string;
    FComment: string;
    FAlbumArtist: string;
    FComposer: string;
    FCopyright: string;
    FUrl: string;
    FPublisher: string;
    FTrackNumber: Integer;
    FGenre: Integer;
    FBpm: Integer;
    FKey: string;
    FHasPicture: Boolean;
    FPicture: TPicture;
    FLastError: string;
    FAbout: string;
    FOnLoaded: TNotifyEvent;
    FOnError: TZForkErrorEvent;
    procedure SetPlayer(AValue: TZForkPlayer);
    procedure SetFileName(const AValue: string);
    procedure SetAutoLoad(AValue: Boolean);
    procedure SetAutoSync(AValue: Boolean);
    function GetAbout: string;
    procedure SetAbout(const AValue: string);
    procedure ClearTags;
    function EnsureHandle: Boolean;
    procedure Fail(const Msg: string);
    procedure LoadPictureFromFile;
    procedure LoadPictureFromSong(SongIndex: Integer);
    procedure ApplyInfo(const Info: TID3InfoEx);
  protected
    procedure Loaded; override;
    procedure Notification(AComponent: TComponent; Operation: TOperation); override;
  public
    constructor Create(AOwner: TComponent); override;
    destructor Destroy; override;

    { IZForkPlayerClient }
    procedure NotifyPlayerOpen(const AFileName: string);
    procedure NotifyPlayerSong(SongIndex: Integer);
    procedure NotifyPlayerClose;
    procedure NotifyPlayerStop;

    function Load: Boolean;
    function LoadFromPlayerSong(SongIndex: Integer): Boolean;
    procedure Clear;
    procedure ShowAbout;
    property LastError: string read FLastError;
    property Picture: TPicture read FPicture;
    property HasPicture: Boolean read FHasPicture;
  published
    { Object Inspector: Schaltflaeche „…“ oeffnet About/Lizenzen. }
    property About: string read GetAbout write SetAbout stored False;
    { Object Inspector: auf TZForkPlayer zeigen — dann Open/NextSong laedt Tags. }
    property Player: TZForkPlayer read FPlayer write SetPlayer;
    property FileName: string read FFileName write SetFileName;
    property AutoLoad: Boolean read FAutoLoad write SetAutoLoad default True;
    { Bei Player-Open / NextSong automatisch Tags laden. }
    property AutoSync: Boolean read FAutoSync write SetAutoSync default True;
    property Title: string read FTitle;
    property Artist: string read FArtist;
    property Album: string read FAlbum;
    property Year: string read FYear;
    property Comment: string read FComment;
    property AlbumArtist: string read FAlbumArtist;
    property Composer: string read FComposer;
    property Copyright: string read FCopyright;
    property Url: string read FUrl;
    property Publisher: string read FPublisher;
    property TrackNumber: Integer read FTrackNumber default 0;
    property Genre: Integer read FGenre default 0;
    property Bpm: Integer read FBpm default 0;
    { Musical / Camelot key from ID3 TKEY (z.B. "Am", "8A"). }
    property Key: string read FKey;
    property OnLoaded: TNotifyEvent read FOnLoaded write FOnLoaded;
    property OnError: TZForkErrorEvent read FOnError write FOnError;
  end;

implementation

uses
  ZForkAbout;

function AnsiBufToStr(const Buf: array of AnsiChar): string;
begin
  Result := string(AnsiString(PAnsiChar(@Buf[0])));
end;

constructor TZForkID3.Create(AOwner: TComponent);
begin
  inherited Create(AOwner);
  FHandle := nil;
  FPlayer := nil;
  FAutoLoad := True;
  FAutoSync := True;
  FAbout := '(About…)';
  FPicture := TPicture.Create;
  ClearTags;
end;

destructor TZForkID3.Destroy;
begin
  if FPlayer <> nil then
  begin
    FPlayer.UnregisterClient(Self);
    FPlayer := nil;
  end;
  if FHandle <> nil then
  begin
    zfork_Destroy(FHandle);
    FHandle := nil;
  end;
  FPicture.Free;
  inherited Destroy;
end;

procedure TZForkID3.Loaded;
begin
  inherited Loaded;
  if csDesigning in ComponentState then Exit;
  if FPlayer <> nil then
    FPlayer.RegisterClient(Self);
  if FAutoLoad and (Trim(FFileName) <> '') then
    Load
  else if FAutoSync and (FPlayer <> nil) and (Trim(FPlayer.FileName) <> '') then
  begin
    FFileName := FPlayer.FileName;
    Load;
  end;
end;

procedure TZForkID3.Notification(AComponent: TComponent; Operation: TOperation);
begin
  inherited Notification(AComponent, Operation);
  if (Operation = opRemove) and (AComponent = FPlayer) then
  begin
    FPlayer := nil;
  end;
end;

procedure TZForkID3.SetPlayer(AValue: TZForkPlayer);
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
  if csLoading in ComponentState then Exit;
  if csDesigning in ComponentState then Exit;
  if FAutoSync and (FPlayer <> nil) and (Trim(FPlayer.FileName) <> '') then
  begin
    FFileName := FPlayer.FileName;
    Load;
  end;
end;

procedure TZForkID3.ClearTags;
begin
  FTitle := '';
  FArtist := '';
  FAlbum := '';
  FYear := '';
  FComment := '';
  FAlbumArtist := '';
  FComposer := '';
  FCopyright := '';
  FUrl := '';
  FPublisher := '';
  FTrackNumber := 0;
  FGenre := 0;
  FBpm := 0;
  FKey := '';
  FHasPicture := False;
  FPicture.Clear;
end;

procedure TZForkID3.Clear;
begin
  ClearTags;
  FLastError := '';
end;

function TZForkID3.GetAbout: string;
begin
  Result := '(About…)';
end;

procedure TZForkID3.SetAbout(const AValue: string);
begin
  FAbout := AValue;
  if not (csLoading in ComponentState) then
    ShowAbout;
end;

procedure TZForkID3.ShowAbout;
begin
  ShowZForkAbout;
end;

procedure TZForkID3.Fail(const Msg: string);
begin
  FLastError := Msg;
  if Assigned(FOnError) then
    FOnError(Self, Msg);
end;

function TZForkID3.EnsureHandle: Boolean;
begin
  Result := FHandle <> nil;
  if Result then Exit;
  FHandle := zfork_Create;
  if FHandle = nil then
  begin
    Fail('zfork_Create failed');
    Exit(False);
  end;
  Result := True;
end;

procedure TZForkID3.SetFileName(const AValue: string);
begin
  if FFileName = AValue then Exit;
  FFileName := AValue;
  if csLoading in ComponentState then Exit;
  if csDesigning in ComponentState then Exit;
  if FAutoLoad then
    Load;
end;

procedure TZForkID3.SetAutoLoad(AValue: Boolean);
begin
  FAutoLoad := AValue;
end;

procedure TZForkID3.SetAutoSync(AValue: Boolean);
begin
  FAutoSync := AValue;
end;

procedure TZForkID3.ApplyInfo(const Info: TID3InfoEx);
begin
  FTitle := AnsiBufToStr(Info.title);
  FArtist := AnsiBufToStr(Info.artist);
  FAlbum := AnsiBufToStr(Info.album);
  FYear := AnsiBufToStr(Info.year);
  FComment := AnsiBufToStr(Info.comment);
  FAlbumArtist := AnsiBufToStr(Info.albumArtist);
  FComposer := AnsiBufToStr(Info.composer);
  FCopyright := AnsiBufToStr(Info.copyright);
  FUrl := AnsiBufToStr(Info.url);
  FPublisher := AnsiBufToStr(Info.publisher);
  FTrackNumber := Info.trackNumber;
  FGenre := Info.genre;
  FBpm := Info.bpm;
  FKey := AnsiBufToStr(Info.key);
end;

procedure TZForkID3.LoadPictureFromFile;
var
  H: TZFORK_HANDLE;
  Data: Pointer;
  Size: LongWord;
  PicType: Integer;
  Stream: TMemoryStream;
begin
  FHasPicture := False;
  FPicture.Clear;
  if Trim(FFileName) = '' then Exit;

  if (FPlayer <> nil) and (FPlayer.Handle <> nil) then
    H := FPlayer.Handle
  else
  begin
    if not EnsureHandle then Exit;
    H := FHandle;
  end;

  Data := nil;
  Size := 0;
  PicType := -1;
  if zfork_LoadFileID3PictureA(H, PAnsiChar(AnsiString(FFileName)),
    0, @Data, @Size, @PicType) = 0 then Exit;
  if (Data = nil) or (Size = 0) then Exit;
  Stream := TMemoryStream.Create;
  try
    Stream.WriteBuffer(Data^, Size);
    Stream.Position := 0;
    try
      FPicture.LoadFromStream(Stream);
      FHasPicture := True;
    except
      FHasPicture := False;
      FPicture.Clear;
    end;
  finally
    Stream.Free;
    zfork_FreeMemory(Data);
  end;
end;

procedure TZForkID3.LoadPictureFromSong(SongIndex: Integer);
var
  Data: Pointer;
  Size: LongWord;
  PicType: Integer;
  Stream: TMemoryStream;
begin
  FHasPicture := False;
  FPicture.Clear;
  if (FPlayer = nil) or (FPlayer.Handle = nil) then Exit;
  if SongIndex < 0 then Exit;

  Data := nil;
  Size := 0;
  PicType := -1;
  if zfork_LoadID3Picture(FPlayer.Handle, SongIndex, 0, @Data, @Size, @PicType) = 0 then Exit;
  if (Data = nil) or (Size = 0) then Exit;
  Stream := TMemoryStream.Create;
  try
    Stream.WriteBuffer(Data^, Size);
    Stream.Position := 0;
    try
      FPicture.LoadFromStream(Stream);
      FHasPicture := True;
    except
      FHasPicture := False;
      FPicture.Clear;
    end;
  finally
    Stream.Free;
    zfork_FreeMemory(Data);
  end;
end;

function TZForkID3.Load: Boolean;
var
  Info: TID3InfoEx;
  H: TZFORK_HANDLE;
begin
  Result := False;
  FLastError := '';
  ClearTags;
  if Trim(FFileName) = '' then
  begin
    Fail('FileName is empty');
    Exit;
  end;

  if (FPlayer <> nil) and (FPlayer.Handle <> nil) then
    H := FPlayer.Handle
  else
  begin
    if not EnsureHandle then Exit;
    H := FHandle;
  end;

  FillChar(Info, SizeOf(Info), 0);
  if zfork_LoadFileID3ExA(H, PAnsiChar(AnsiString(FFileName)), @Info) = 0 then
  begin
    Fail(ZForkGetError(H));
    Exit;
  end;
  ApplyInfo(Info);
  LoadPictureFromFile;
  Result := True;
  if Assigned(FOnLoaded) then
    FOnLoaded(Self);
end;

function TZForkID3.LoadFromPlayerSong(SongIndex: Integer): Boolean;
var
  Info: TID3InfoEx;
  Path: string;
begin
  Result := False;
  FLastError := '';
  ClearTags;
  if (FPlayer = nil) or (FPlayer.Handle = nil) then
  begin
    Fail('Player handle is nil');
    Exit;
  end;
  if FPlayer.GetQueueItemPath(LongWord(SongIndex), Path) and (Path <> '') then
    FFileName := Path;
  FillChar(Info, SizeOf(Info), 0);
  if not FPlayer.LoadID3Ex(SongIndex, Info) then
  begin
    Fail(FPlayer.LastError);
    Exit;
  end;
  ApplyInfo(Info);
  if Trim(FFileName) <> '' then
    LoadPictureFromFile
  else
    LoadPictureFromSong(SongIndex);
  Result := True;
  if Assigned(FOnLoaded) then
    FOnLoaded(Self);
end;

procedure TZForkID3.NotifyPlayerOpen(const AFileName: string);
begin
  if not FAutoSync then Exit;
  if Trim(AFileName) = '' then Exit;
  FFileName := AFileName;
  Load;
end;

procedure TZForkID3.NotifyPlayerSong(SongIndex: Integer);
begin
  if not FAutoSync then Exit;
  LoadFromPlayerSong(SongIndex);
end;

procedure TZForkID3.NotifyPlayerClose;
begin
  if not FAutoSync then Exit;
  Clear;
end;

procedure TZForkID3.NotifyPlayerStop;
begin
  { Tags bleiben stehen }
end;

end.
