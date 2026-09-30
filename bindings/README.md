# libZFork — Language Bindings

Bindings for **libZFork.dll** against the flat C APIs:

| API | Header | Convention | Files |
|-----|--------|------------|-------|
| Player v1 | `libzfork.h` | `__stdcall` (`zfork_*`) | `*/libzfork.*` / `LibZFork.cs` |
| Game Audio v2 | `libzfork2.h` | `cdecl` (`zfork2_*`) | `*/libzfork2.*` / `LibZFork2.cs` |

Coverage: **83/83** `zfork_*` and **52/52** `zfork2_*` (synced via `scripts/sync_bindings.py`).

## Prerequisites

Copy **`libZFork.dll`** (from `dist/win-x64/Release/` or Debug) next to your executable.
The DLL is self-contained aside from the MSVC CRT redistributable.

---

## Pascal / Delphi / Free Pascal

**Files:** `pascal/libzfork.pas`, `pascal/libzfork2.pas`

```pascal
uses libzfork;

var h: TZFORK_HANDLE;
begin
  h := zfork_Create;
  zfork_OpenFile(h, 'C:\music\song.mp3', ZFORK_FMT_AUTODETECT);
  zfork_SetMasterVolume(h, 80, 80);
  zfork_Play(h);
  Sleep(5000);
  zfork_Stop(h);
  zfork_Destroy(h);
end.
```

Tested with Delphi 10.x and Free Pascal / Lazarus 3.x (Unicode mode, x64).

---

## PureBasic

**Files:** `purebasic/libzfork.pb`, `purebasic/libzfork2.pb`

```purebasic
XIncludeFile "libzfork.pb"

If Not ZForkLoad()
  MessageRequester("Error", "libZFork.dll not found")
  End 1
EndIf

Define h.i = zfork_Create()
ZForkOpenFile(h, "C:\music\song.mp3")
zfork_SetMasterVolume(h, 80, 80)
zfork_Play(h)
Delay(5000)
zfork_Stop(h)
zfork_Destroy(h)
ZForkUnload()
```

Tested with PureBasic 6.x (x64, Unicode mode).

---

## C# / .NET

**File:** `csharp/LibZFork.cs`

Add `LibZFork.cs` to your project. Targets **.NET 6+** and **.NET Framework 4.7.2+**.

```csharp
using LibZFork;

using var player = new ZFork();

if (!player.OpenFile(@"C:\music\song.mp3"))
    Console.WriteLine("Error: " + player.GetError());

player.SetMasterVolume(80, 80);
player.Play();

Console.ReadKey();
// 'using' calls Dispose() → Stop() + Destroy() automatically
```

FFT / spectrum example:

```csharp
float[] specL = new float[1024];
float[] specR = new float[1024];
player.GetFFTData(specL, specR);
// specL[0..1023] = left channel magnitudes, index 0 = lowest bin
```

---

## Covered C API functions

| Function | Description |
|---|---|
| `zfork_Create` / `zfork_Destroy` | Create / destroy player instance |
| `zfork_OpenFile` / `zfork_OpenFileA` | Open file (Unicode / ANSI path) |
| `zfork_AddFile` | Add file to playlist |
| `zfork_OpenStream` | Open from memory buffer |
| `zfork_Close` | Close stream |
| `zfork_Play` / `zfork_PlayLoop` | Start playback |
| `zfork_Pause` / `zfork_Resume` / `zfork_Stop` | Playback control |
| `zfork_SetMasterVolume` / `zfork_GetMasterVolume` | Volume 0–100 |
| `zfork_SetRate` / `zfork_SetPitch` / `zfork_SetTempo` | Speed / pitch |
| `zfork_EnableEqualizer` / `zfork_SetEQBandGain` | Parametric EQ |
| `zfork_EnableEcho` | Echo effect |
| `zfork_GetFFTData` | FFT spectrum data |
| `zfork_DetectBPM` | BPM detection |
| `zfork_GetError` | Last error string |
