{ This file was automatically created by Lazarus. Do not edit!
  This source is only used to compile and install the package.
 }

unit lazlibzfork;

{$warn 5023 off : no warning about unused units}
interface

uses
  LibZFork, LibZFork2, ZForkIntf, ZForkLicenses, ZForkAbout, ZForkPlayer, 
  ZForkID3, ZForkSpectrum, ZForkWaveform, ZForkCrossfader, ZForkChannelStrip, 
  ZForkStoryboard, ZForkStoryAuto, ZForkPlaylist, ZForkDeck, ZForkMixer, 
  ZForkJogWheel, ZForkBeatGrid, ZForkEQPanel, ZForkBroadcast, ZForkEngine, 
  ZForkBank, ZForkPropEdit, ZForkReg, LazarusPackageIntf;

implementation

procedure Register;
begin
  RegisterUnit('ZForkReg', @ZForkReg.Register);
end;

initialization
  RegisterPackage('lazlibzfork', @Register);
end.
