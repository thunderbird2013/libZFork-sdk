{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  ZForkIntf — gemeinsame Verdrahtung Player / Engine / Clients

  Was dieses Unit kann:
  - IZForkPlayerClient: Open / SongChange / Close / Stop vom Player
  - IZForkEngineClient: EngineStarted / EngineStopped
  - Ermoeglicht lose Kopplung von Spectrum, Waveform, ID3, Bank, …
******************************************************************************}
unit ZForkIntf;

{$mode objfpc}{$H+}

interface

type
  { Von ID3 / Spectrum / Broadcast / eigenen Komponenten implementieren.
    Am Player: RegisterClient(Self) — typischerweise in SetPlayer. }
  IZForkPlayerClient = interface
    ['{8F3C2A91-6B4E-4D17-9E2A-1C0B7F45D8E3}']
    procedure NotifyPlayerOpen(const AFileName: string);
    procedure NotifyPlayerSong(SongIndex: Integer);
    procedure NotifyPlayerClose;
    procedure NotifyPlayerStop;
  end;

  { Von SoundBank / eigenen Game-Audio-Komponenten.
    An Engine: RegisterClient(Self) — typischerweise in SetEngine. }
  IZForkEngineClient = interface
    ['{B4E91C70-2D8A-4F15-9C33-6A1E8D0F2B57}']
    procedure NotifyEngineStart;
    procedure NotifyEngineStop;
  end;

implementation

end.
