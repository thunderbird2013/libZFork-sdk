{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  ZForkReg — Package-Registration (Palette „libZFork“)

  Was dieses Unit kann:
  - Registriert alle sichtbaren Komponenten auf der Palette
  - Laedt Palette-Icons (zfork_icons.lrs)
  - Haengt Design-Time Property-Editoren ein
******************************************************************************}
unit ZForkReg;

{$mode objfpc}{$H+}

interface

procedure Register;

implementation

uses
  Classes, LResources, ZForkPlayer, ZForkID3, ZForkSpectrum, ZForkBroadcast,
  ZForkEngine, ZForkBank, ZForkWaveform, ZForkCrossfader, ZForkChannelStrip,
  ZForkStoryboard, ZForkPlaylist, ZForkDeck, ZForkMixer, ZForkJogWheel,
  ZForkBeatGrid, ZForkEQPanel, ZForkPropEdit;

procedure Register;
begin
  RegisterComponents('libZFork',
    [TZForkPlayer, TZForkID3, TZForkSpectrum, TZForkBroadcast,
     TZForkAudioEngine, TZForkSoundBank, TZForkWaveform,
     TZForkCrossfader, TZForkChannelStrip, TZForkStoryboard, TZForkPlaylist,
     TZForkDeck, TZForkMixer, TZForkJogWheel, TZForkBeatGrid, TZForkEQPanel]);
  ZForkPropEdit.Register;
end;

initialization
  {$I zfork_icons.lrs}

end.
