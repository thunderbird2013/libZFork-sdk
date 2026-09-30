libZFork.dll — Beschreibung für Downloader
============================================

Was ist libZFork.dll?
---------------------
libZFork.dll ist die Windows-x64-Laufzeitbibliothek von libZFork — einer
modernen Audio-Engine für Wiedergabe, Verarbeitung und Ausgabe von Musik und
Sound. Sie ersetzt bzw. erweitert den Funktionsumfang von libZPlay 2.02 und
bietet eine stabile C-API (__stdcall) sowie ein C++20-Objekt-Interface.

Die im SDK enthaltene Release-DLL ist self-contained: MP3, OGG, FLAC, AAC,
Opus und weitere Codecs sind eingebaut. Zusätzlich zu Windows-System-DLLs
(msvcrt, winmm, kernel32 …) sind keine weiteren Codec-DLLs nötig.


Wofür eignet sich die DLL?
--------------------------
  • Musik-Player, Jukebox, Playlist-Apps
  • Audio-Analyse (FFT, BPM, Spektrum, VU-Meter)
  • Echtzeit-DSP: Equalizer, Echo, Pitch/Tempo (SoundTouch)
  • Parallel-Encoding (MP3, OGG, FLAC, WAV) während der Wiedergabe
  • Push-PCM / Custom-Decoder-Pipelines
  • Icecast-/Shoutcast-Broadcast (MP3-Stream) — siehe optionale DLL unten
  • Game-Audio über libZFork2 (Voices, 3D, Soundbanks) in derselben DLL


Zwei API-Ebenen in einer DLL
----------------------------
  v1 (Player)     zfork_*     — Dateien, Playlists, DSP, Metadaten, Broadcast
  libZFork2       zfork2_*    — Game-Sounds, Gruppen, Listener, 3D-Position
  C++             ZFork.h     — Objekt-Wrapper über beide Ebenen

Beide APIs teilen sich libZFork.dll; libZFork2 ist keine separate Bibliothek.


Dateien im SDK (Release, x64)
-----------------------------
  libZFork.dll          Laufzeit-DLL (neben Ihrer EXE ablegen)
  libZFork.lib          Import-Library zum Linken (MSVC)
  libZFork_static.lib   Alternative: statisches Linken ohne DLL

Debug-Variante:
  libZForkd.dll / libZForkd.lib / libZFork_staticd.lib


Optionale Zusatz-DLL (nur Broadcast)
------------------------------------
Für Icecast-/Shoutcast-Streaming (StartBroadcast / zfork_StartBroadcast):

  libshout.dll  oder  libshout-3.dll

Diese wird zur Laufzeit dynamisch geladen. Ohne libshout schlagen
Broadcast-Funktionen fehl; alle anderen Features arbeiten normal weiter.


Systemvoraussetzungen
---------------------
  • Windows 10 oder 11 (64-Bit)
  • Visual C++ Redistributable 2022 (x64) — falls noch nicht installiert
  • Kein separater Codec-Installer nötig (self-contained Build)


Einbindung in eigene Programme
------------------------------
  C/C++     Header in include\libzfork\, linken gegen libZFork.lib
  C#        bindings\csharp\
  Delphi    bindings\pascal\
  PureBasic bindings\purebasic\

Weitergabe / Redistribution
---------------------------
Bei Auslieferung von libZFork.dll (oder einer App, die dagegen gelinkt ist)
den Ordner license\ mitliefern (GPLv2 + Drittanbieter-Volltexte).
Corresponding Source liegt im SDK unter source\ (siehe source\BUILD.txt).
Inventar: license\README.txt

Ausführliche API-Dokumentation:
  help\libZFork.chm       Compiled Help (API, Lizenzen)


Version
-------
Zentrale Quelle: Datei VERSION im Repo-Root (MAJOR.MINOR.PATCH).
CMake, DLL-Dateiversion, Header (libzfork_version.h), vcpkg.json, Lazarus-Package
und der SDK-Installer lesen diese Datei. Siehe Dateiversion von libZFork.dll
(Eigenschaften → Details) oder AppVersion im SDK-Installer
(z. B. libZFork-1.0.0-x64-sdk.exe).
