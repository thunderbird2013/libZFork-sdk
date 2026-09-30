{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  ZForkLicenses â€” Copyright- und Lizenztexte fuer About

  Was dieses Unit kann:
  - Konstanten: Holder, Jahr, URL (solid-software.net), Produktname
  - ZForkGetAboutLicenseText: Uebersicht libZFork + Drittlizenzen
  - Optional Volltexte aus dem Ordner license/ im Repo
******************************************************************************}
unit ZForkLicenses;

{$mode objfpc}{$H+}

interface

{ Volltext fuer About-Memo: Copyright + Uebersicht + ggf. Dateien aus license/. }
function ZForkGetAboutLicenseText: string;

const
  ZFORK_COPYRIGHT_HOLDER = 'Matthias Stoltze';
  ZFORK_COPYRIGHT_ORG    = 'solid-software.net';
  ZFORK_COPYRIGHT_YEAR   = '2026';
  ZFORK_PRODUCT_NAME     = 'libZFork';
  ZFORK_PRODUCT_VERSION = '1.0.0';
  ZFORK_PRODUCT_URL      = 'https://solid-software.net';

implementation

uses
  SysUtils, Classes;

function EmbeddedSummary: string;
begin
  Result :=
    ZFORK_PRODUCT_NAME + ' â€” Audio Library' + LineEnding +
    'Copyright (C) ' + ZFORK_COPYRIGHT_YEAR + ' ' + ZFORK_COPYRIGHT_HOLDER +
    ' / ' + ZFORK_COPYRIGHT_ORG + LineEnding +
    ZFORK_PRODUCT_URL + LineEnding + LineEnding +
    'libZFork is free software under the GNU General Public License v2 (GPLv2).' + LineEnding +
    'Based on / forked from libZPlay by Zoran Cindori.' + LineEnding + LineEnding +
    'â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•' + LineEnding +
    'Third-party components (typical Release DLL / static lib)' + LineEnding +
    'â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•' + LineEnding + LineEnding +
    '  libZFork .............. GPLv2' + LineEnding +
    '  mpg123 ................ LGPL 2.1' + LineEnding +
    '  libvorbis / libogg .... BSD-3-Clause' + LineEnding +
    '  libFLAC ............... BSD-3-Clause / Xiph' + LineEnding +
    '  faad2 ................. GPL 2' + LineEnding +
    '  LAME (mp3lame) ........ LGPL 2' + LineEnding +
    '  SoundTouch ............ LGPL 2.1  (SoundTouch.txt + LGPL.TXT)' + LineEnding +
    '  libopus ............... BSD-3-Clause  (opus.txt)' + LineEnding +
    '  opusfile .............. BSD-3-Clause  (opusfile.txt)' + LineEnding +
    '  PortAudio ............. MIT-style  (portaudio.txt)' + LineEnding +
    '  zlib .................. zlib License  (zlib.txt)' + LineEnding +
    '  libpng ................ libpng  (libpng.txt)' + LineEnding +
    '  libjpeg-turbo ......... IJG/BSD  (ijglib.txt)' + LineEnding + LineEnding +
    'When redistributing libZFork you must comply with GPLv2 and the license' + LineEnding +
    'terms of the embedded libraries (provide license texts / source offer).' + LineEnding + LineEnding +
    'Full license texts ship with the SDK in the "license" folder' + LineEnding +
    '(see license\README.txt for the inventory).' + LineEnding;
end;

function TryAppendLicenseDir(const Dir: string; var Text: string): Boolean;
var
  SR: TSearchRec;
  Names: TStringList;
  I: Integer;
  Path, Body: string;
  FS: TFileStream;
  Buf: AnsiString;
begin
  Result := False;
  if (Dir = '') or not DirectoryExists(Dir) then
    Exit;
  Names := TStringList.Create;
  try
    if FindFirst(IncludeTrailingPathDelimiter(Dir) + '*', faAnyFile, SR) = 0 then
    try
      repeat
        if (SR.Attr and faDirectory) = 0 then
        begin
          if (CompareText(ExtractFileExt(SR.Name), '.txt') = 0) or
             (CompareText(ExtractFileExt(SR.Name), '.TXT') = 0) then
            Names.Add(SR.Name);
        end;
      until FindNext(SR) <> 0;
    finally
      FindClose(SR);
    end;
    if Names.Count = 0 then
      Exit;
    Names.Sort;
    Text := Text + LineEnding + LineEnding +
      'â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•' + LineEnding +
      'License files from: ' + Dir + LineEnding +
      'â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•' + LineEnding;
    for I := 0 to Names.Count - 1 do
    begin
      Path := IncludeTrailingPathDelimiter(Dir) + Names[I];
      try
        FS := TFileStream.Create(Path, fmOpenRead or fmShareDenyWrite);
        try
          SetLength(Buf, FS.Size);
          if FS.Size > 0 then
            FS.ReadBuffer(Pointer(Buf)^, FS.Size);
          Body := string(Buf);
        finally
          FS.Free;
        end;
      except
        Body := '(could not read file)';
      end;
      Text := Text + LineEnding + LineEnding +
        'â”€â”€ ' + Names[I] + ' â”€â”€' + LineEnding + Body;
    end;
    Result := True;
  finally
    Names.Free;
  end;
end;

function ZForkGetAboutLicenseText: string;
var
  Candidates: array[0..4] of string;
  I: Integer;
  Base, LicDir: string;
begin
  Result := EmbeddedSummary;

  Base := ExtractFilePath(ParamStr(0));
  Candidates[0] := IncludeTrailingPathDelimiter(Base) + 'license';
  Candidates[1] := IncludeTrailingPathDelimiter(Base) + 'licenses';
  Candidates[2] := ExpandFileName(IncludeTrailingPathDelimiter(Base) +
    '..' + PathDelim + 'license');
  Candidates[3] := ExpandFileName(IncludeTrailingPathDelimiter(Base) +
    '..' + PathDelim + '..' + PathDelim + 'license');
  Candidates[4] := ExpandFileName(IncludeTrailingPathDelimiter(Base) +
    '..' + PathDelim + '..' + PathDelim + '..' + PathDelim + 'license');

  for I := Low(Candidates) to High(Candidates) do
  begin
    LicDir := ExpandFileName(Candidates[I]);
    if (LicDir <> '') and TryAppendLicenseDir(LicDir, Result) then
      Break;
  end;
end;

end.
