#!/usr/bin/env python3
"""Rewrite Lazarus unit file headers: copyright + detailed component description."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(r"H:/Devel/CPP/libZFork/Lazarus")

COPY = (
    "  Copyright (C) 2026 Matthias Stoltze\n"
    "  https://solid-software.net\n"
)

# Detailed descriptions (German) — body lines after copyright, indented with two spaces
DESCS: dict[str, str] = {
    "LibZFork.pas": """\
  libZFork — Low-Level-Wrapper fuer libZFork.dll (stdcall, Windows x64)

  Was dieses Unit kann:
  - Player-Handle erzeugen/zerstoeren (Create / CreateWithConfig / Destroy)
  - Dateien und Playlists oeffnen, Queue (Add/Remove/Move), Stream aus Speicher
  - Transport: Play/Pause/Resume/Stop, Seek, Position, Status, StreamInfo
  - Lautstaerke (Master/Player), VU, Mono-Mix, Rate / Pitch / Tempo
  - DSP: Equalizer, Echo, VocalCut, SideCut, Stereo-Enhancer, BufferSize
  - FFT-Daten, BPM-Erkennung, Datei-Analyse (BPM/Waveform/Peaks)
  - Broadcast Icecast/Shoutcast inkl. Mic, Ducking, Metadata, Stats
  - Callbacks (Player-Events, PCM-Output), Ausgabegeraete (WASAPI/WaveOut)
  - ID3Ex + Cover laden; Pascal-Helfer (Strings/Boolean/Init-Structs)
""",
    "LibZFork2.pas": """\
  libZFork2 — Game-Audio-API (zfork2_*, cdecl) ueber dieselbe DLL

  Was dieses Unit kann:
  - Audio-Context erzeugen (optional Output-Backend), Start/Stop/Update
  - Mix-Gruppen registrieren (master/music/sfx/ui/eigene)
  - Sounds laden (PreDecoded / Streaming), Sound-Cache (Budget/Stats/Clear)
  - SoundBank V1 laden/kompilieren (optional Samples eingebettet), Events abspielen
  - Voices: Play/Stop/Pause, Volume/Pan/Pitch/Tempo/Loop/Priority, Marker
  - Positionales Audio 2D/3D + Listener (Position/Richtung/Range)
  - Gruppen-Effekte Echo/Reverb setzen/loeschen, Gruppen-VU
  - Datei-Utilities: ProbeFile, DetectFileBPM, OutputBackendInfo
""",
    "ZForkPlayer.pas": """\
  TZForkPlayer — zentrale LCL-Player-Komponente (Palette libZFork)

  Was die Komponente kann:
  - Datei/Playlist oeffnen, AutoPlay, Active Start/Stop des Engines
  - Transport, Queue, Seek, Volume, Pitch/Tempo als Properties/Methoden
  - DSP live: Equalizer, Echo, VocalCut, SideCut, Stereo-Enhancer
  - FFT/VU, BPM, Analyse, ID3, Broadcast-Methoden an der Komponente
  - OutputBackend / OutputDevice (WASAPI Exclusive oder WaveOut)
  - Events asynchron auf dem Hauptthread (Play/Stop/Position/Queue/Broadcast)
  - Client-Anbindung (IZForkPlayerClient) fuer Spectrum/Waveform/ID3/…
  - About im Object Inspector (Property-Editor ".")
""",
    "ZForkID3.pas": """\
  TZForkID3 — ID3-Tags und Cover-Art anzeigen/laden

  Was die Komponente kann:
  - Tags aus Datei oder aktuellem Player-Song laden (Load / LoadFromPlayer)
  - Cover als TPicture (JPEG/PNG), AutoLoad und AutoSync am Player
  - Read-only Felder: Title, Artist, Album, Year, Comment, Genre, BPM, …
  - AlbumArtist, Composer, Copyright, URL, Publisher
  - Reagiert als IZForkPlayerClient auf Open/NextSong/Close
""",
    "ZForkSpectrum.pas": """\
  TZForkSpectrum — Echtzeit-FFT- / VU-Visualisierung

  Was die Komponente kann:
  - Styles: Bars, Lines, Area, VU, LED, Neon, Fire, Plasma, Rainbow, …
  - Anbindung an TZForkPlayer; SyncMode Timer/Idle/Hybrid, TargetFPS
  - Attack/Release (Response), LogScale, Grid, Peak-Hold
  - Pro-Style-Farben, AutoIntensity, FastDraw fuer hohe FPS
  - Stereo links/rechts oder gemittelt; Resize-sicheres Offscreen-Painting
""",
    "ZForkBroadcast.pas": """\
  TZForkBroadcast — Icecast/Shoutcast-Streaming am Player

  Was die Komponente kann:
  - Verbindung: Host, Port, Mount, User/Password, StreamName
  - Encoder-Parameter: Bitrate, SampleRate, Channels, StereoMode, ServerType
  - AutoReconnect + Delay; AttachToPlayback (mit laufender Wiedergabe)
  - Metadata setzen (Title/Artist/Album); Stats abfragen
  - Mic-Capture starten/stoppen, MicActive, Ducking-Config
  - Events: Connected / Disconnected / Reconnecting / Error / Stats
""",
    "ZForkEngine.pas": """\
  TZForkAudioEngine — Game-Audio-Engine als LCL-Komponente (libZFork2)

  Was die Komponente kann:
  - Active startet Context + periodisches Update (Voice-Events auf UI-Thread)
  - OutputBackend waehlen; Standard-Gruppen music/sfx/ui
  - LoadSound / UnloadSound / PlaySound; Bank laden und PlayEvent
  - Voice-Steuerung (Volume/Pan/Pitch/Tempo/Loop/Position/Marker)
  - Gruppen: Volume/Mute/Levels, Echo/Reverb setzen oder loeschen
  - Listener 2D/3D; Events VoiceStart/Finished/Loop/PositionCrossed
  - Client-Anbindung (IZForkEngineClient) z.B. fuer TZForkSoundBank
""",
    "ZForkBank.pas": """\
  TZForkSoundBank — SoundBank V1 an TZForkAudioEngine

  Was die Komponente kann:
  - Bank-Datei (.txt / kompiliertes .bank) ueber FileName laden
  - AutoLoad wenn Engine Active wird; Load / Unload / Reload
  - PlayEvent / PlayEventOnGroup; EntryCount und SkippedCount
  - Design-Time Dateiauswahl (BrowseFileName)
  - Reagiert als IZForkEngineClient auf Engine-Start/Stop
""",
    "ZForkWaveform.pas": """\
  TZForkWaveform — DJ-Waveform (Kick/Mid/High) fuer Deck A/B

  Was die Komponente kann:
  - Stacked-, Overlay- oder Single-Ansicht; Farben pro Deck/Band
  - Async-Datei-Analyse (Peaks/Waveform); eigener Tick-Thread (60/90 FPS)
  - Playhead/Needle, Beat-Strip, Zoom, Qualitaet, Latency/SyncOffset
  - ColorBoost, Strg+Mausrad-Zoom; Bindung an einen oder zwei Player
  - FastDraw / Smooth-Position fuer ruckelfreie Nadel
""",
    "ZForkCrossfader.pas": """\
  TZForkCrossfader — A/B-Mix (DJ und Radio Auto-Fade)

  Was die Komponente kann:
  - Position 0.0 = nur A … 1.0 = nur B; Kurven Linear oder ConstPower
  - Setzt PlayerA/PlayerB-Volumen live (Master oder Player-Vol)
  - FadeTo / FadeToA / FadeToB / FadeToCenter mit AutoFadeMs
  - Events OnChange und OnFadeDone
""",
    "ZForkChannelStrip.pas": """\
  TZForkChannelStrip — Gain + 3-Band-EQ als Drehregler (DJ-Channel)

  Was die Komponente kann:
  - Gain und Low/Mid/High live am gebundenen TZForkPlayer
  - Mitte = 0 dB (flat), links Richtung Kill, rechts Boost
  - Optional VU-Anzeige; ResetEQ auf Flat
  - OnChange bei Reglerbewegung
""",
    "ZForkStoryboard.pas": """\
  TZForkStoryboard — Radio-Programm-Timeline (Spuren A/B/C)

  Was die Komponente kann:
  - Clips (Musik/Spot) auf Spuren A/B/C: anlegen, trimmen, verschieben
  - Drag&Drop von Dateien, Zoom, Playhead, Minuten-Lineal
  - Scheduler/Wiedergabe des Programms ohne zusaetzliche DLL-Features
  - DuckOnC, AutoBeatmatch, Beat-Grid, Wave-Analyse pro Clip
  - Clip-Automation (Volumen/EQ/Echo/Reverb/SideCut) zeichnen/abspielen
  - Play/Pause/SeekProgram; Anbindung an bis zu drei Player
""",
    "ZForkStoryAuto.pas": """\
  ZForkStoryAuto — Automationskurven fuer Storyboard-Clips

  Was dieses Unit kann:
  - Parameter: Volume, Bass/Mid/High, Echo, Reverb, SideCut
  - Kurvenpunkte (TimeMs, Wert 0..1), lineare Auswertung EvalAt
  - Farbgruppe TZForkStoryAutoColors fuer die Timeline-Darstellung
  - Wird von TZForkStoryboard genutzt (kein eigenes Palette-Control)
""",
    "ZForkPlaylist.pas": """\
  TZForkPlaylist — Owner-Draw-Playlist mit Mini-Peaks

  Was die Komponente kann:
  - Liste mit Titel/Artist/BPM/Dauer und Peak-Vorschau
  - Dateien/Ordner hinzufuegen, DropFiles, Kontextmenue
  - Doppelklick / Play startet am gebundenen TZForkPlayer
  - Async-Analyse im Hintergrund; Remove/Clear/Reorder
""",
    "ZForkDeck.pas": """\
  TZForkDeck — DJ-Deck: Transport, Cue, Pitch/Tempo, Wave-Strip

  Was die Komponente kann:
  - Play/Pause/Stop/Cue am gebundenen Player
  - Pitch- und Tempo-Fader; BPM-Erkennung
  - Bis zu 8 Hotcues setzen und anspringen
  - Integrierter Wave-Strip / Positionsanzeige (PosMs/LengthMs)
""",
    "ZForkMixer.pas": """\
  TZForkMixer — Master- und Kanalpegel A/B/C

  Was die Komponente kann:
  - Master-Fader + Kanaele A/B/C mit Mute
  - Steuert die gebundenen Player-Volumen live
  - Optional VU-Anzeige; OnChange bei Pegelwechsel
""",
    "ZForkJogWheel.pas": """\
  TZForkJogWheel — Drehrad zum Seeken oder Nudgen

  Was die Komponente kann:
  - Modus jmSeek (Position springen) oder jmNudge (kurz vor/zurueck)
  - Sensitivity einstellbar; Nudge(Ms) programmierbar
  - Anbindung an TZForkPlayer; Maus-/Touch-Drehung
""",
    "ZForkBeatGrid.pas": """\
  TZForkBeatGrid — BPM-Anzeige und Tempo-Sync A/B

  Was die Komponente kann:
  - BPM von Player A und/oder B anzeigen; DetectNow
  - SyncAtoB / SyncBtoA (Tempo angleichen, MaxTempoPct-Limit)
  - Optional AutoDetect beim Oeffnen neuer Tracks
""",
    "ZForkEQPanel.pas": """\
  TZForkEQPanel — 3-Band-EQ als vertikale Fader

  Was die Komponente kann:
  - Low / Mid / High in dB live am TZForkPlayer
  - EqualizerEnabled; ResetFlat (z.B. Doppelklick auf Fader)
  - Kompaktes Layout fuer Mixer-/Booth-UIs
""",
    "ZForkAbout.pas": """\
  ZForkAbout — About-Dialog fuer libZFork (Lazarus)

  Was dieses Unit kann:
  - Modalen Dialog erzeugen (UI im Code, kein separates Form-Resource)
  - Logo, Produktname, Copyright, klickbare URL
  - Lizenz-Memo mit ZForkGetAboutLicenseText
  - Aufruf: ShowZForkAbout / ShowZForkAboutDialog
""",
    "ZForkLicenses.pas": """\
  ZForkLicenses — Copyright- und Lizenztexte fuer About

  Was dieses Unit kann:
  - Konstanten: Holder, Jahr, URL (solid-software.net), Produktname
  - ZForkGetAboutLicenseText: Uebersicht libZFork + Drittlizenzen
  - Optional Volltexte aus dem Ordner license/ im Repo
""",
    "ZForkIntf.pas": """\
  ZForkIntf — gemeinsame Verdrahtung Player / Engine / Clients

  Was dieses Unit kann:
  - IZForkPlayerClient: Open / SongChange / Close / Stop vom Player
  - IZForkEngineClient: EngineStarted / EngineStopped
  - Ermoeglicht lose Kopplung von Spectrum, Waveform, ID3, Bank, …
""",
    "ZForkPropEdit.pas": """\
  ZForkPropEdit — Design-Time Property-Editoren (Object Inspector)

  Was dieses Unit kann:
  - About-Property oeffnet den About-Dialog
  - FileName: Dateiauswahl-Dialog
  - OutputDevice: Dropdown der Ausgabegeraete je Backend
  - Wird von ZForkReg beim Package-Install registriert
""",
    "ZForkReg.pas": """\
  ZForkReg — Package-Registration (Palette „libZFork“)

  Was dieses Unit kann:
  - Registriert alle sichtbaren Komponenten auf der Palette
  - Laedt Palette-Icons (zfork_icons.lrs)
  - Haengt Design-Time Property-Editoren ein
""",
}

BANNER_RE = re.compile(r"^\{[*]{10,}.*?[*]{10,}\}\s*\n", re.S)


def make_banner(desc: str) -> str:
    body = COPY + "\n" + desc.rstrip() + "\n"
    return (
        "{******************************************************************************\n"
        + body
        + "******************************************************************************}\n"
    )


def main() -> None:
    changed = 0
    for name, desc in DESCS.items():
        path = ROOT / name
        if not path.is_file():
            print("MISSING", name)
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        banner = make_banner(desc)
        if BANNER_RE.match(text):
            new = BANNER_RE.sub(banner, text, count=1)
        else:
            new = banner + text
        if new != text:
            path.write_text(new, encoding="utf-8", newline="\n")
            changed += 1
            print("updated", name)
        else:
            print("same   ", name)
    print(f"done, {changed} files")


if __name__ == "__main__":
    main()
