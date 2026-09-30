#!/usr/bin/env python3
"""Sync PureBasic / Pascal / C# bindings from C headers + Lazarus reference."""
from __future__ import annotations

import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sync_pascal() -> None:
    mapping = [
        ("Lazarus/LibZFork.pas", "bindings/pascal/libzfork.pas", "LibZFork", "libzfork"),
        ("Lazarus/LibZFork2.pas", "bindings/pascal/libzfork2.pas", "LibZFork2", "libzfork2"),
    ]
    for src_rel, dst_rel, old_unit, new_unit in mapping:
        src = ROOT / src_rel
        dst = ROOT / dst_rel
        text = src.read_text(encoding="utf-8")
        text = text.replace(f"unit {old_unit};", f"unit {new_unit};", 1)
        text = text.replace(
            "{$mode objfpc}{$H+}",
            "{$IFDEF FPC}\n  {$MODE DELPHI}\n  {$H+}\n{$ENDIF}",
            1,
        )
        header = (
            "{******************************************************************************\n"
            f"  {new_unit} — Pascal / Delphi / Free Pascal Bindings (Windows x64)\n"
            f"  Synced from {src_rel} against include/libzfork/*.h\n"
            "******************************************************************************}\n"
        )
        # Drop original banner up to unit line if present
        m = re.search(r"^unit\s+\w+\s*;", text, re.M)
        if m:
            rest = text[m.start() :]
            text = header + rest
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_text(text, encoding="utf-8", newline="\n")
        print(f"OK pascal  {dst_rel}")


# ── C API inventory ──────────────────────────────────────────────────────────

ZFORK1_FUNCS = [
    # (name, ret, args_c_like for docs, cs_sig, pb_proto, pb_ok)
    ("zfork_Create", "IntPtr", "()", "()", "Prototype.i  zfork_Create()"),
    ("zfork_CreateWithConfig", "IntPtr", "(config)",
     "(ref ZForkConfig config)", "Prototype.i  zfork_CreateWithConfig(*config)"),
    ("zfork_Destroy", "void", "(h)", "(IntPtr h)", "Prototype    zfork_Destroy(handle.i)"),
    ("zfork_OpenFile", "int", "(h,pathW,fmt)",
     "(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] string path, int format)",
     "Prototype.i  zfork_OpenFile(handle.i, *pathW, format.i)"),
    ("zfork_OpenFileA", "int", "(h,pathA,fmt)",
     "(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string path, int format)",
     "Prototype.i  zfork_OpenFileA(handle.i, *pathA, format.i)"),
    ("zfork_LoadPlaylistFile", "int", "",
     "(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] string path, int fallbackFormat)",
     "Prototype.i  zfork_LoadPlaylistFile(handle.i, *pathW, fallbackFormat.i)"),
    ("zfork_LoadPlaylistFileA", "int", "",
     "(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string path, int fallbackFormat)",
     "Prototype.i  zfork_LoadPlaylistFileA(handle.i, *pathA, fallbackFormat.i)"),
    ("zfork_AddFile", "int", "",
     "(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] string path, int format)",
     "Prototype.i  zfork_AddFile(handle.i, *pathW, format.i)"),
    ("zfork_AddFileA", "int", "",
     "(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string path, int format)",
     "Prototype.i  zfork_AddFileA(handle.i, *pathA, format.i)"),
    ("zfork_RemoveQueueItem", "int", "",
     "(IntPtr h, uint index)", "Prototype.i  zfork_RemoveQueueItem(handle.i, index.l)"),
    ("zfork_MoveQueueItem", "int", "",
     "(IntPtr h, uint fromIndex, uint toIndex)",
     "Prototype.i  zfork_MoveQueueItem(handle.i, fromIndex.l, toIndex.l)"),
    ("zfork_GetQueueCount", "uint", "",
     "(IntPtr h)", "Prototype.l  zfork_GetQueueCount(handle.i)"),
    ("zfork_GetQueueItemPathA", "int", "",
     "(IntPtr h, uint index, byte[] outBuf, uint outBytes)",
     "Prototype.i  zfork_GetQueueItemPathA(handle.i, index.l, *outPath, outBytes.l)"),
    ("zfork_OpenStream", "int", "",
     "(IntPtr h, IntPtr data, uint size, int format)",
     "Prototype.i  zfork_OpenStream(handle.i, *data, size.l, format.i)"),
    ("zfork_Close", "void", "", "(IntPtr h)", "Prototype    zfork_Close(handle.i)"),
    ("zfork_Play", "int", "", "(IntPtr h)", "Prototype.i  zfork_Play(handle.i)"),
    ("zfork_PlayLoop", "int", "", "(IntPtr h, uint count)",
     "Prototype.i  zfork_PlayLoop(handle.i, count.l)"),
    ("zfork_Pause", "int", "", "(IntPtr h)", "Prototype.i  zfork_Pause(handle.i)"),
    ("zfork_Resume", "int", "", "(IntPtr h)", "Prototype.i  zfork_Resume(handle.i)"),
    ("zfork_Stop", "int", "", "(IntPtr h)", "Prototype.i  zfork_Stop(handle.i)"),
    ("zfork_SeekMs", "int", "", "(IntPtr h, int origin, uint positionMs)",
     "Prototype.i  zfork_SeekMs(handle.i, origin.i, positionMs.l)"),
    ("zfork_GetPositionMs", "int", "", "(IntPtr h, out uint positionMs)",
     "Prototype.i  zfork_GetPositionMs(handle.i, *positionMs)"),
    ("zfork_GetStreamInfo", "int", "", "(IntPtr h, int songIndex, out StreamInfo outInfo)",
     "Prototype.i  zfork_GetStreamInfo(handle.i, songIndex.i, *outInfo)"),
    ("zfork_GetStatus", "int", "", "(IntPtr h, out StreamStatus outStatus)",
     "Prototype.i  zfork_GetStatus(handle.i, *outStatus)"),
    ("zfork_SetMasterVolume", "int", "", "(IntPtr h, int left, int right)",
     "Prototype.i  zfork_SetMasterVolume(handle.i, left.i, right.i)"),
    ("zfork_GetMasterVolume", "int", "", "(IntPtr h, out int left, out int right)",
     "Prototype.i  zfork_GetMasterVolume(handle.i, *outLeft, *outRight)"),
    ("zfork_SetPlayerVolume", "int", "", "(IntPtr h, int left, int right)",
     "Prototype.i  zfork_SetPlayerVolume(handle.i, left.i, right.i)"),
    ("zfork_GetPlayerVolume", "int", "", "(IntPtr h, out int left, out int right)",
     "Prototype.i  zfork_GetPlayerVolume(handle.i, *outLeft, *outRight)"),
    ("zfork_GetVUData", "int", "", "(IntPtr h, out float left, out float right)",
     "Prototype.i  zfork_GetVUData(handle.i, *outLeft, *outRight)"),
    ("zfork_MixChannels", "int", "", "(IntPtr h, int mono)",
     "Prototype.i  zfork_MixChannels(handle.i, mono.i)"),
    ("zfork_SetRate", "int", "", "(IntPtr h, float rate)",
     "Prototype.i  zfork_SetRate(handle.i, rate.f)"),
    ("zfork_SetPitch", "int", "", "(IntPtr h, float semitones)",
     "Prototype.i  zfork_SetPitch(handle.i, semitones.f)"),
    ("zfork_SetTempo", "int", "", "(IntPtr h, float tempo)",
     "Prototype.i  zfork_SetTempo(handle.i, tempo.f)"),
    ("zfork_EnableEqualizer", "int", "", "(IntPtr h, int enable)",
     "Prototype.i  zfork_EnableEqualizer(handle.i, enable.i)"),
    ("zfork_SetEQBandGain", "int", "", "(IntPtr h, int band, float gainDb)",
     "Prototype.i  zfork_SetEQBandGain(handle.i, band.i, gainDb.f)"),
    ("zfork_GetEQBandGain", "int", "", "(IntPtr h, int band, out float gainDb)",
     "Prototype.i  zfork_GetEQBandGain(handle.i, band.i, *gainDb)"),
    ("zfork_ResetEqualizer", "int", "", "(IntPtr h)",
     "Prototype.i  zfork_ResetEqualizer(handle.i)"),
    ("zfork_EnableEcho", "int", "", "(IntPtr h, int enable)",
     "Prototype.i  zfork_EnableEcho(handle.i, enable.i)"),
    ("zfork_SetEchoParams", "int", "", "(IntPtr h, ref EchoParams params)",
     "Prototype.i  zfork_SetEchoParams(handle.i, *params)"),
    ("zfork_GetEchoParams", "int", "", "(IntPtr h, out EchoParams outParams)",
     "Prototype.i  zfork_GetEchoParams(handle.i, *outParams)"),
    ("zfork_EnableVocalCut", "int", "", "(IntPtr h, int enable)",
     "Prototype.i  zfork_EnableVocalCut(handle.i, enable.i)"),
    ("zfork_EnableSideCut", "int", "", "(IntPtr h, int enable)",
     "Prototype.i  zfork_EnableSideCut(handle.i, enable.i)"),
    ("zfork_EnableStereoEnhancer", "int", "", "(IntPtr h, int enable)",
     "Prototype.i  zfork_EnableStereoEnhancer(handle.i, enable.i)"),
    ("zfork_SetStereoWidth", "int", "", "(IntPtr h, float width)",
     "Prototype.i  zfork_SetStereoWidth(handle.i, width.f)"),
    ("zfork_GetStereoWidth", "int", "", "(IntPtr h, out float outWidth)",
     "Prototype.i  zfork_GetStereoWidth(handle.i, *outWidth)"),
    ("zfork_SetBufferSize", "int", "", "(IntPtr h, uint milliseconds)",
     "Prototype.i  zfork_SetBufferSize(handle.i, milliseconds.l)"),
    ("zfork_GetFFTData", "int", "",
     "(IntPtr h, [In, Out] float[] left, [In, Out] float[] right, uint points)",
     "Prototype.i  zfork_GetFFTData(handle.i, *leftBuf, *rightBuf, points.l)"),
    ("zfork_DetectBPM", "int", "", "(IntPtr h, int algo, out float outBPM)",
     "Prototype.i  zfork_DetectBPM(handle.i, algo.i, *outBPM)"),
    ("zfork_DetectFileBPMA", "int", "",
     "(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string path, int algo, out float outBPM)",
     "Prototype.i  zfork_DetectFileBPMA(handle.i, *pathA, algo.i, *outBPM)"),
    ("zfork_AnalyzeFile", "int", "",
     "(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] string path, uint flags, out IntPtr outHandle)",
     "Prototype.i  zfork_AnalyzeFile(handle.i, *pathW, flags.l, *outHandle)"),
    ("zfork_AnalyzeFileA", "int", "",
     "(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string path, uint flags, out IntPtr outHandle)",
     "Prototype.i  zfork_AnalyzeFileA(handle.i, *pathA, flags.l, *outHandle)"),
    ("zfork_FreeAnalysis", "void", "", "(IntPtr h, IntPtr analysis)",
     "Prototype    zfork_FreeAnalysis(handle.i, analysis.i)"),
    ("zfork_GetAnalysisSummary", "int", "",
     "(IntPtr h, IntPtr analysis, out float outBpm, out uint outLengthMs, out uint outFlags)",
     "Prototype.i  zfork_GetAnalysisSummary(handle.i, analysis.i, *outBpm, *outLengthMs, *outFlags)"),
    ("zfork_CopyAnalysisWaveform", "uint", "",
     "(IntPtr h, IntPtr analysis, byte[] outBins, uint maxBins)",
     "Prototype.l  zfork_CopyAnalysisWaveform(handle.i, analysis.i, *outBins, maxBins.l)"),
    ("zfork_CopyAnalysisPeaks", "uint", "",
     "(IntPtr h, IntPtr analysis, [In, Out] float[] outTimesMs, [In, Out] float[] outValues, uint maxPeaks)",
     "Prototype.l  zfork_CopyAnalysisPeaks(handle.i, analysis.i, *outTimesMs, *outValues, maxPeaks.l)"),
    ("zfork_SetPlaylistAllowDuplicates", "int", "", "(IntPtr h, int allow)",
     "Prototype.i  zfork_SetPlaylistAllowDuplicates(handle.i, allow.i)"),
    ("zfork_GetPlaylistAllowDuplicates", "int", "", "(IntPtr h)",
     "Prototype.i  zfork_GetPlaylistAllowDuplicates(handle.i)"),
    ("zfork_StartBroadcast", "int", "", "(IntPtr h, ref BroadcastConfig config)",
     "Prototype.i  zfork_StartBroadcast(handle.i, *config)"),
    ("zfork_StopBroadcast", "int", "", "(IntPtr h)",
     "Prototype.i  zfork_StopBroadcast(handle.i)"),
    ("zfork_PushBroadcastPCM", "int", "",
     "(IntPtr h, [In] float[] interleaved, uint frames, int channels, int sampleRate)",
     "Prototype.i  zfork_PushBroadcastPCM(handle.i, *interleaved, frames.l, channels.i, sampleRate.i)"),
    ("zfork_StartBroadcastFile", "int", "",
     "(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] string path, int format)",
     "Prototype.i  zfork_StartBroadcastFile(handle.i, *pathW, format.i)"),
    ("zfork_StartBroadcastFileA", "int", "",
     "(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string path, int format)",
     "Prototype.i  zfork_StartBroadcastFileA(handle.i, *pathA, format.i)"),
    ("zfork_AttachBroadcastToCurrentPlayback", "int", "", "(IntPtr h, int enable)",
     "Prototype.i  zfork_AttachBroadcastToCurrentPlayback(handle.i, enable.i)"),
    ("zfork_SetBroadcastMetadata", "int", "",
     "(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string title, [MarshalAs(UnmanagedType.LPStr)] string artist, [MarshalAs(UnmanagedType.LPStr)] string album)",
     "Prototype.i  zfork_SetBroadcastMetadata(handle.i, *titleA, *artistA, *albumA)"),
    ("zfork_PushBroadcastMicPCM", "int", "",
     "(IntPtr h, [In] float[] interleaved, uint frames, int channels, int sampleRate)",
     "Prototype.i  zfork_PushBroadcastMicPCM(handle.i, *interleaved, frames.l, channels.i, sampleRate.i)"),
    ("zfork_SetBroadcastMicActive", "int", "", "(IntPtr h, int active)",
     "Prototype.i  zfork_SetBroadcastMicActive(handle.i, active.i)"),
    ("zfork_SetBroadcastDucking", "int", "", "(IntPtr h, ref BroadcastDuckingConfig config)",
     "Prototype.i  zfork_SetBroadcastDucking(handle.i, *config)"),
    ("zfork_GetBroadcastDucking", "int", "", "(IntPtr h, out BroadcastDuckingConfig outConfig)",
     "Prototype.i  zfork_GetBroadcastDucking(handle.i, *outConfig)"),
    ("zfork_StartBroadcastMicCapture", "int", "",
     "(IntPtr h, int deviceId, int sampleRate, int channels)",
     "Prototype.i  zfork_StartBroadcastMicCapture(handle.i, deviceId.i, sampleRate.i, channels.i)"),
    ("zfork_StopBroadcastMicCapture", "int", "", "(IntPtr h)",
     "Prototype.i  zfork_StopBroadcastMicCapture(handle.i)"),
    ("zfork_GetBroadcastStats", "int", "", "(IntPtr h, out BroadcastStats outStats)",
     "Prototype.i  zfork_GetBroadcastStats(handle.i, *outStats)"),
    ("zfork_GetBroadcastError", "string?", "",
     "(IntPtr h)", "Prototype.i  zfork_GetBroadcastError(handle.i)"),
    ("zfork_SetPCMOutputCallback", "int", "",
     "(IntPtr h, PCMOutputCallback? callback, IntPtr userData, int disableBuiltInOutput)",
     "Prototype.i  zfork_SetPCMOutputCallback(handle.i, *callback, userData.i, disableBuiltInOutput.i)"),
    ("zfork_SetCallback", "int", "",
     "(IntPtr h, PlayerCallback? callback, IntPtr userData)",
     "Prototype.i  zfork_SetCallback(handle.i, *callback, userData.i)"),
    ("zfork_EnumerateOutputDevices", "int", "", "(IntPtr h)",
     "Prototype.i  zfork_EnumerateOutputDevices(handle.i)"),
    ("zfork_GetOutputDeviceInfo", "int", "",
     "(IntPtr h, int deviceId, out WaveDeviceInfo outInfo)",
     "Prototype.i  zfork_GetOutputDeviceInfo(handle.i, deviceId.i, *outInfo)"),
    ("zfork_SetOutputDevice", "int", "", "(IntPtr h, int deviceId)",
     "Prototype.i  zfork_SetOutputDevice(handle.i, deviceId.i)"),
    ("zfork_LoadID3Ex", "int", "", "(IntPtr h, int songIndex, out ID3InfoEx outInfo)",
     "Prototype.i  zfork_LoadID3Ex(handle.i, songIndex.i, *outInfo)"),
    ("zfork_LoadID3Picture", "int", "",
     "(IntPtr h, int songIndex, int picIndex, out IntPtr outData, out uint outSize, out int outType)",
     "Prototype.i  zfork_LoadID3Picture(handle.i, songIndex.i, picIndex.i, *outData, *outSize, *outType)"),
    ("zfork_LoadFileID3ExA", "int", "",
     "(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string path, out ID3InfoEx outInfo)",
     "Prototype.i  zfork_LoadFileID3ExA(handle.i, *pathA, *outInfo)"),
    ("zfork_LoadFileID3PictureA", "int", "",
     "(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string path, int picIndex, out IntPtr outData, out uint outSize, out int outType)",
     "Prototype.i  zfork_LoadFileID3PictureA(handle.i, *pathA, picIndex.i, *outData, *outSize, *outType)"),
    ("zfork_FreeMemory", "void", "", "(IntPtr ptr)",
     "Prototype    zfork_FreeMemory(*ptr)"),
    ("zfork_GetError", "string?", "", "(IntPtr h)",
     "Prototype.i  zfork_GetError(handle.i)"),
]


def cs_ret(r: str) -> str:
    if r == "void":
        return "void"
    if r == "string?":
        return "string?"
    if r == "IntPtr":
        return "IntPtr"
    if r == "uint":
        return "uint"
    return "int"


def gen_csharp_v1() -> None:
    structs = r'''
    [StructLayout(LayoutKind.Sequential)]
    public struct StreamHMSTime
    {
        public uint hour, minute, second, millisecond;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct StreamTime
    {
        public uint sec, ms;
        public ulong samples;
        public StreamHMSTime hms;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct StreamInfo
    {
        public int samplingRate, channelNumber, bitrate;
        [MarshalAs(UnmanagedType.U1)] public bool vbr;
        public StreamTime length;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct StreamStatus
    {
        [MarshalAs(UnmanagedType.U1)] public bool playing;
        [MarshalAs(UnmanagedType.U1)] public bool paused;
        [MarshalAs(UnmanagedType.U1)] public bool echoEnabled;
        [MarshalAs(UnmanagedType.U1)] public bool equalizerEnabled;
        [MarshalAs(UnmanagedType.U1)] public bool vocalCutEnabled;
        [MarshalAs(UnmanagedType.U1)] public bool channelMixEnabled;
        [MarshalAs(UnmanagedType.U1)] public bool slidingVolume;
        private byte _pad;
        public int loopCount, songIndex, songsInQueue;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct EchoParams
    {
        public int leftDelay, leftSourceVol, leftEchoVol;
        public int rightDelay, rightSourceVol, rightEchoVol;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct WaveDeviceInfo
    {
        public uint id;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string name;
        public uint channels, formats;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct ID3InfoEx
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string title;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string artist;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string album;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 16)]  public string year;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)] public string comment;
        public int trackNumber, genre;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string albumArtist;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string composer;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string copyright;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)] public string url;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string publisher;
        public int bpm;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct BroadcastConfig
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string host;
        public ushort port;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string mount;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]  public string user;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string password;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string streamName;
        public int bitrateKbps, sampleRate, channels;
        public int stereoMode;   // 0=Auto 1=Stereo 2=JointStereo 3=Mono
        public int serverType;   // 0=Icecast 1=Shoutcast
        [MarshalAs(UnmanagedType.U1)] public bool autoReconnect;
        private byte _p0, _p1, _p2;
        public uint reconnectDelayMs;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct BroadcastStats
    {
        public ulong bytesSent, framesSent, reconnectCount, droppedFrames;
        [MarshalAs(UnmanagedType.U1)] public bool connected;
        private byte _p0, _p1, _p2, _p3, _p4, _p5, _p6;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct BroadcastDuckingConfig
    {
        [MarshalAs(UnmanagedType.U1)] public bool enabled;
        private byte _p0, _p1, _p2;
        public float duckDb;
        public uint attackMs, releaseMs;
        public float threshold;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct ZForkConfig
    {
        public uint structSize;
        public int waveOutDeviceId;
        public uint bufferMs;
        public int masterVolumeLeft, masterVolumeRight;
        public int playerVolumeLeft, playerVolumeRight;
        public int monoMix;
        public int outputBackend;
        public static ZForkConfig Default()
        {
            return new ZForkConfig {
                structSize = (uint)Marshal.SizeOf<ZForkConfig>(),
                waveOutDeviceId = -1, bufferMs = 0,
                masterVolumeLeft = -1, masterVolumeRight = -1,
                playerVolumeLeft = -1, playerVolumeRight = -1,
                monoMix = -1, outputBackend = 0
            };
        }
    }

    public delegate int PlayerCallback(IntPtr instance, IntPtr userData,
        int message, uint param1, uint param2);
    public delegate void PCMOutputCallback(IntPtr instance, IntPtr userData,
        IntPtr interleaved, uint frames, int channels, int sampleRate);
'''

    lines = []
    lines.append("""/*
 * libZFork — C# / .NET Bindings (v1 Player API)
 * Synced against include/libzfork/libzfork.h
 * Target: .NET 6+ / .NET Framework 4.7.2+  |  Windows x64  |  stdcall
 */
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace LibZFork
{
    public enum StreamFormat : int
    {
        Unknown = 0, Mp3 = 1, Ogg = 2, Wav = 3, Pcm = 4,
        Flac = 5, FlacOgg = 6, Reserved7 = 7, AacAdts = 8, WaveIn = 9,
        Autodetect = 10, Aiff = 11, Opus = 12, CdAudio = 13, M4a = 14,
    }

    public enum BpmAlgorithm : int { Peaks = 0, AutoCorrelation = 1, Advanced = 2 }
    public enum SeekOrigin : int { Begin = 0, Current = 1, End = 2 }
    public static class ZForkOutput
    {
        public const int Auto = 0, Wasapi = 1, WaveOut = 2;
    }
    public static class AnalyzeFlags
    {
        public const uint Bpm = 1, Waveform = 2, Peaks = 4, Length = 8;
        public const uint All = Bpm | Waveform | Peaks | Length;
    }
""")
    lines.append(structs)
    lines.append("""
    internal static class Native
    {
        internal const string DllName = "libZFork.dll";
        private const CallingConvention CC = CallingConvention.StdCall;
""")
    for name, ret, _c, cs_args, _pb in ZFORK1_FUNCS:
        extra = ""
        if ret == "string?":
            extra = "\n        [return: MarshalAs(UnmanagedType.LPStr)]"
        lines.append(f"""
        [DllImport(DllName, CallingConvention = CC)]{extra}
        internal static extern {cs_ret(ret)} {name}{cs_args};""")
    lines.append("    }")

    # Managed wrapper — keep usable surface covering all APIs
    lines.append("""
    public sealed class ZFork : IDisposable
    {
        private IntPtr _h;
        private bool _disposed;
        private PlayerCallback? _cbKeepAlive;
        private PCMOutputCallback? _pcmKeepAlive;

        public ZFork()
        {
            _h = Native.zfork_Create();
            if (_h == IntPtr.Zero)
                throw new InvalidOperationException("zfork_Create failed — is libZFork.dll present?");
        }

        public ZFork(ZForkConfig config)
        {
            _h = Native.zfork_CreateWithConfig(ref config);
            if (_h == IntPtr.Zero)
                throw new InvalidOperationException("zfork_CreateWithConfig failed.");
        }

        public IntPtr Handle { get { ThrowIfDisposed(); return _h; } }

        public bool OpenFile(string path, StreamFormat format = StreamFormat.Autodetect)
        { ThrowIfDisposed(); return Native.zfork_OpenFile(_h, path, (int)format) != 0; }
        public bool OpenFileAnsi(string path, StreamFormat format = StreamFormat.Autodetect)
        { ThrowIfDisposed(); return Native.zfork_OpenFileA(_h, path, (int)format) != 0; }
        public bool AddFile(string path, StreamFormat format = StreamFormat.Autodetect)
        { ThrowIfDisposed(); return Native.zfork_AddFile(_h, path, (int)format) != 0; }
        public bool AddFileAnsi(string path, StreamFormat format = StreamFormat.Autodetect)
        { ThrowIfDisposed(); return Native.zfork_AddFileA(_h, path, (int)format) != 0; }
        public bool LoadPlaylist(string path, StreamFormat fallback = StreamFormat.Autodetect)
        { ThrowIfDisposed(); return Native.zfork_LoadPlaylistFile(_h, path, (int)fallback) != 0; }
        public bool RemoveQueueItem(uint index)
        { ThrowIfDisposed(); return Native.zfork_RemoveQueueItem(_h, index) != 0; }
        public bool MoveQueueItem(uint from, uint to)
        { ThrowIfDisposed(); return Native.zfork_MoveQueueItem(_h, from, to) != 0; }
        public uint GetQueueCount()
        { ThrowIfDisposed(); return Native.zfork_GetQueueCount(_h); }
        public string? GetQueueItemPath(uint index)
        {
            ThrowIfDisposed();
            var buf = new byte[1024];
            if (Native.zfork_GetQueueItemPathA(_h, index, buf, (uint)buf.Length) == 0) return null;
            int n = Array.IndexOf(buf, (byte)0); if (n < 0) n = buf.Length;
            return Encoding.UTF8.GetString(buf, 0, n);
        }
        public bool OpenStream(byte[] data, StreamFormat format = StreamFormat.Autodetect)
        {
            ThrowIfDisposed();
            var pin = GCHandle.Alloc(data, GCHandleType.Pinned);
            try { return Native.zfork_OpenStream(_h, pin.AddrOfPinnedObject(), (uint)data.Length, (int)format) != 0; }
            finally { pin.Free(); }
        }
        public void Close() { ThrowIfDisposed(); Native.zfork_Close(_h); }
        public bool Play() { ThrowIfDisposed(); return Native.zfork_Play(_h) != 0; }
        public bool PlayLoop(uint count) { ThrowIfDisposed(); return Native.zfork_PlayLoop(_h, count) != 0; }
        public bool Pause() { ThrowIfDisposed(); return Native.zfork_Pause(_h) != 0; }
        public bool Resume() { ThrowIfDisposed(); return Native.zfork_Resume(_h) != 0; }
        public bool Stop() { ThrowIfDisposed(); return Native.zfork_Stop(_h) != 0; }
        public bool SeekMs(SeekOrigin origin, uint positionMs)
        { ThrowIfDisposed(); return Native.zfork_SeekMs(_h, (int)origin, positionMs) != 0; }
        public uint GetPositionMs()
        { ThrowIfDisposed(); Native.zfork_GetPositionMs(_h, out uint ms); return ms; }
        public bool TryGetStreamInfo(int songIndex, out StreamInfo info)
        { ThrowIfDisposed(); return Native.zfork_GetStreamInfo(_h, songIndex, out info) != 0; }
        public bool TryGetStatus(out StreamStatus status)
        { ThrowIfDisposed(); return Native.zfork_GetStatus(_h, out status) != 0; }
        public bool SetMasterVolume(int left, int right)
        { ThrowIfDisposed(); return Native.zfork_SetMasterVolume(_h, left, right) != 0; }
        public (int Left, int Right) GetMasterVolume()
        { ThrowIfDisposed(); Native.zfork_GetMasterVolume(_h, out int l, out int r); return (l, r); }
        public bool SetPlayerVolume(int left, int right)
        { ThrowIfDisposed(); return Native.zfork_SetPlayerVolume(_h, left, right) != 0; }
        public (int Left, int Right) GetPlayerVolume()
        { ThrowIfDisposed(); Native.zfork_GetPlayerVolume(_h, out int l, out int r); return (l, r); }
        public (float Left, float Right) GetVUData()
        { ThrowIfDisposed(); Native.zfork_GetVUData(_h, out float l, out float r); return (l, r); }
        public bool MixChannels(bool mono)
        { ThrowIfDisposed(); return Native.zfork_MixChannels(_h, mono ? 1 : 0) != 0; }
        public bool SetRate(float rate) { ThrowIfDisposed(); return Native.zfork_SetRate(_h, rate) != 0; }
        public bool SetPitch(float semitones) { ThrowIfDisposed(); return Native.zfork_SetPitch(_h, semitones) != 0; }
        public bool SetTempo(float tempo) { ThrowIfDisposed(); return Native.zfork_SetTempo(_h, tempo) != 0; }
        public bool EnableEqualizer(bool enable)
        { ThrowIfDisposed(); return Native.zfork_EnableEqualizer(_h, enable ? 1 : 0) != 0; }
        public bool SetEqBandGain(int band, float gainDb)
        { ThrowIfDisposed(); return Native.zfork_SetEQBandGain(_h, band, gainDb) != 0; }
        public float GetEqBandGain(int band)
        { ThrowIfDisposed(); Native.zfork_GetEQBandGain(_h, band, out float g); return g; }
        public bool ResetEqualizer() { ThrowIfDisposed(); return Native.zfork_ResetEqualizer(_h) != 0; }
        public bool EnableEcho(bool enable)
        { ThrowIfDisposed(); return Native.zfork_EnableEcho(_h, enable ? 1 : 0) != 0; }
        public bool SetEchoParams(EchoParams p)
        { ThrowIfDisposed(); return Native.zfork_SetEchoParams(_h, ref p) != 0; }
        public bool TryGetEchoParams(out EchoParams p)
        { ThrowIfDisposed(); return Native.zfork_GetEchoParams(_h, out p) != 0; }
        public bool EnableVocalCut(bool enable)
        { ThrowIfDisposed(); return Native.zfork_EnableVocalCut(_h, enable ? 1 : 0) != 0; }
        public bool EnableSideCut(bool enable)
        { ThrowIfDisposed(); return Native.zfork_EnableSideCut(_h, enable ? 1 : 0) != 0; }
        public bool EnableStereoEnhancer(bool enable)
        { ThrowIfDisposed(); return Native.zfork_EnableStereoEnhancer(_h, enable ? 1 : 0) != 0; }
        public bool SetStereoWidth(float width)
        { ThrowIfDisposed(); return Native.zfork_SetStereoWidth(_h, width) != 0; }
        public float GetStereoWidth()
        { ThrowIfDisposed(); Native.zfork_GetStereoWidth(_h, out float w); return w; }
        public bool SetBufferSize(uint milliseconds)
        { ThrowIfDisposed(); return Native.zfork_SetBufferSize(_h, milliseconds) != 0; }
        public bool GetFFTData(float[] left, float[] right)
        {
            ThrowIfDisposed();
            if (left.Length != right.Length) throw new ArgumentException("array length mismatch");
            return Native.zfork_GetFFTData(_h, left, right, (uint)left.Length) != 0;
        }
        public float DetectBpm(BpmAlgorithm algo = BpmAlgorithm.Advanced)
        { ThrowIfDisposed(); Native.zfork_DetectBPM(_h, (int)algo, out float bpm); return bpm; }
        public float DetectFileBpm(string path, BpmAlgorithm algo = BpmAlgorithm.Advanced)
        { ThrowIfDisposed(); Native.zfork_DetectFileBPMA(_h, path, (int)algo, out float bpm); return bpm; }
        public IntPtr AnalyzeFile(string path, uint flags = AnalyzeFlags.All)
        {
            ThrowIfDisposed();
            return Native.zfork_AnalyzeFile(_h, path, flags, out IntPtr ah) != 0 ? ah : IntPtr.Zero;
        }
        public void FreeAnalysis(IntPtr analysis)
        { ThrowIfDisposed(); Native.zfork_FreeAnalysis(_h, analysis); }
        public bool GetAnalysisSummary(IntPtr analysis, out float bpm, out uint lengthMs, out uint flags)
        { ThrowIfDisposed(); return Native.zfork_GetAnalysisSummary(_h, analysis, out bpm, out lengthMs, out flags) != 0; }
        public uint CopyAnalysisWaveform(IntPtr analysis, byte[] bins)
        { ThrowIfDisposed(); return Native.zfork_CopyAnalysisWaveform(_h, analysis, bins, (uint)bins.Length); }
        public uint CopyAnalysisPeaks(IntPtr analysis, float[] timesMs, float[] values)
        {
            ThrowIfDisposed();
            uint n = (uint)Math.Min(timesMs.Length, values.Length);
            return Native.zfork_CopyAnalysisPeaks(_h, analysis, timesMs, values, n);
        }
        public bool SetPlaylistAllowDuplicates(bool allow)
        { ThrowIfDisposed(); return Native.zfork_SetPlaylistAllowDuplicates(_h, allow ? 1 : 0) != 0; }
        public bool GetPlaylistAllowDuplicates()
        { ThrowIfDisposed(); return Native.zfork_GetPlaylistAllowDuplicates(_h) != 0; }
        public bool StartBroadcast(BroadcastConfig config)
        { ThrowIfDisposed(); return Native.zfork_StartBroadcast(_h, ref config) != 0; }
        public bool StopBroadcast() { ThrowIfDisposed(); return Native.zfork_StopBroadcast(_h) != 0; }
        public bool PushBroadcastPcm(float[] interleaved, uint frames, int channels, int sampleRate)
        { ThrowIfDisposed(); return Native.zfork_PushBroadcastPCM(_h, interleaved, frames, channels, sampleRate) != 0; }
        public bool StartBroadcastFile(string path, StreamFormat format = StreamFormat.Autodetect)
        { ThrowIfDisposed(); return Native.zfork_StartBroadcastFile(_h, path, (int)format) != 0; }
        public bool AttachBroadcastToCurrentPlayback(bool enable)
        { ThrowIfDisposed(); return Native.zfork_AttachBroadcastToCurrentPlayback(_h, enable ? 1 : 0) != 0; }
        public bool SetBroadcastMetadata(string title, string artist, string album)
        { ThrowIfDisposed(); return Native.zfork_SetBroadcastMetadata(_h, title, artist, album) != 0; }
        public bool PushBroadcastMicPcm(float[] interleaved, uint frames, int channels, int sampleRate)
        { ThrowIfDisposed(); return Native.zfork_PushBroadcastMicPCM(_h, interleaved, frames, channels, sampleRate) != 0; }
        public bool SetBroadcastMicActive(bool active)
        { ThrowIfDisposed(); return Native.zfork_SetBroadcastMicActive(_h, active ? 1 : 0) != 0; }
        public bool SetBroadcastDucking(BroadcastDuckingConfig config)
        { ThrowIfDisposed(); return Native.zfork_SetBroadcastDucking(_h, ref config) != 0; }
        public bool TryGetBroadcastDucking(out BroadcastDuckingConfig config)
        { ThrowIfDisposed(); return Native.zfork_GetBroadcastDucking(_h, out config) != 0; }
        public bool StartBroadcastMicCapture(int deviceId, int sampleRate, int channels)
        { ThrowIfDisposed(); return Native.zfork_StartBroadcastMicCapture(_h, deviceId, sampleRate, channels) != 0; }
        public bool StopBroadcastMicCapture()
        { ThrowIfDisposed(); return Native.zfork_StopBroadcastMicCapture(_h) != 0; }
        public bool TryGetBroadcastStats(out BroadcastStats stats)
        { ThrowIfDisposed(); return Native.zfork_GetBroadcastStats(_h, out stats) != 0; }
        public string GetBroadcastError()
        { if (_h == IntPtr.Zero) return "(disposed)"; return Native.zfork_GetBroadcastError(_h) ?? ""; }
        public bool SetCallback(PlayerCallback? callback, IntPtr userData = default)
        {
            ThrowIfDisposed();
            _cbKeepAlive = callback;
            return Native.zfork_SetCallback(_h, callback, userData) != 0;
        }
        public bool SetPcmOutputCallback(PCMOutputCallback? callback, IntPtr userData = default, bool disableBuiltIn = false)
        {
            ThrowIfDisposed();
            _pcmKeepAlive = callback;
            return Native.zfork_SetPCMOutputCallback(_h, callback, userData, disableBuiltIn ? 1 : 0) != 0;
        }
        public int EnumerateOutputDevices()
        { ThrowIfDisposed(); return Native.zfork_EnumerateOutputDevices(_h); }
        public bool TryGetOutputDeviceInfo(int deviceId, out WaveDeviceInfo info)
        { ThrowIfDisposed(); return Native.zfork_GetOutputDeviceInfo(_h, deviceId, out info) != 0; }
        public bool SetOutputDevice(int deviceId)
        { ThrowIfDisposed(); return Native.zfork_SetOutputDevice(_h, deviceId) != 0; }
        public bool TryLoadId3Ex(int songIndex, out ID3InfoEx info)
        { ThrowIfDisposed(); return Native.zfork_LoadID3Ex(_h, songIndex, out info) != 0; }
        public bool TryLoadFileId3Ex(string path, out ID3InfoEx info)
        { ThrowIfDisposed(); return Native.zfork_LoadFileID3ExA(_h, path, out info) != 0; }
        public byte[]? LoadId3Picture(int songIndex, int picIndex, out int picType)
        {
            ThrowIfDisposed();
            picType = 0;
            if (Native.zfork_LoadID3Picture(_h, songIndex, picIndex, out IntPtr p, out uint sz, out picType) == 0 || p == IntPtr.Zero)
                return null;
            try {
                var data = new byte[sz];
                Marshal.Copy(p, data, 0, (int)sz);
                return data;
            } finally { Native.zfork_FreeMemory(p); }
        }
        public string GetError()
        { if (_h == IntPtr.Zero) return "(disposed)"; return Native.zfork_GetError(_h) ?? ""; }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            if (_h != IntPtr.Zero)
            {
                Native.zfork_Stop(_h);
                Native.zfork_Destroy(_h);
                _h = IntPtr.Zero;
            }
            _cbKeepAlive = null;
            _pcmKeepAlive = null;
        }
        private void ThrowIfDisposed()
        {
            if (_disposed || _h == IntPtr.Zero)
                throw new ObjectDisposedException(nameof(ZFork));
        }
    }
}
""")
    out = ROOT / "bindings/csharp/LibZFork.cs"
    out.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"OK csharp   {out.relative_to(ROOT)}  ({len(ZFORK1_FUNCS)} exports)")


def gen_purebasic_v1() -> None:
    names = [f[0] for f in ZFORK1_FUNCS]
    protos = [f[4] for f in ZFORK1_FUNCS]
    lines = []
    lines.append(""";
; libZFork — PureBasic Bindings (v1 Player API)
; Synced against include/libzfork/libzfork.h
; Platform: Windows x64  |  PureBasic 5.73 / 6.x Unicode
;
;   XIncludeFile "libzfork.pb"
;   If Not ZForkLoad() : End 1 : EndIf
;   ...
;   ZForkUnload()
;

#ZFORK_FMT_UNKNOWN    = 0
#ZFORK_FMT_MP3        = 1
#ZFORK_FMT_OGG        = 2
#ZFORK_FMT_WAV        = 3
#ZFORK_FMT_PCM        = 4
#ZFORK_FMT_FLAC       = 5
#ZFORK_FMT_FLACOGG    = 6
#ZFORK_FMT_RESERVED7  = 7
#ZFORK_FMT_AACADTS    = 8
#ZFORK_FMT_WAVEIN     = 9
#ZFORK_FMT_AUTODETECT = 10
#ZFORK_FMT_AIFF       = 11
#ZFORK_FMT_OPUS       = 12
#ZFORK_FMT_CDAUDIO    = 13
#ZFORK_FMT_M4A        = 14

#ZFORK_BPM_PEAKS           = 0
#ZFORK_BPM_AUTOCORRELATION = 1
#ZFORK_BPM_ADVANCED        = 2

#ZFORK_SEEK_BEGIN   = 0
#ZFORK_SEEK_CURRENT = 1
#ZFORK_SEEK_END     = 2

#ZFORK_OUTPUT_AUTO    = 0
#ZFORK_OUTPUT_WASAPI  = 1
#ZFORK_OUTPUT_WAVEOUT = 2

#ZFORK_ANALYZE_BPM      = $00000001
#ZFORK_ANALYZE_WAVEFORM = $00000002
#ZFORK_ANALYZE_PEAKS    = $00000004
#ZFORK_ANALYZE_LENGTH   = $00000008
#ZFORK_ANALYZE_ALL      = $0000000F

; Structures (C layout / MSVC x64)
Structure ZFORK_StreamHMSTime
  hour.l : minute.l : second.l : millisecond.l
EndStructure

Structure ZFORK_StreamTime
  sec.l : ms.l
  samples.q
  hms.ZFORK_StreamHMSTime
EndStructure

Structure ZFORK_StreamInfo Align #PB_Structure_AlignC
  samplingRate.l : channelNumber.l : bitrate.l
  vbr.a : _pad0.a : _pad1.a : _pad2.a
  length.ZFORK_StreamTime
EndStructure

Structure ZFORK_StreamStatus Align #PB_Structure_AlignC
  playing.a : paused.a : echoEnabled.a : equalizerEnabled.a
  vocalCutEnabled.a : channelMixEnabled.a : slidingVolume.a : _pad.a
  loopCount.l : songIndex.l : songsInQueue.l
EndStructure

Structure ZFORK_EchoParams
  leftDelay.l : leftSourceVol.l : leftEchoVol.l
  rightDelay.l : rightSourceVol.l : rightEchoVol.l
EndStructure

Structure ZFORK_WaveDeviceInfo
  id.l
  name.a[64]
  channels.l : formats.l
EndStructure

Structure ZFORK_ID3InfoEx
  title.a[256] : artist.a[256] : album.a[256]
  year.a[16] : comment.a[512]
  trackNumber.l : genre.l
  albumArtist.a[256] : composer.a[256] : copyright.a[256]
  url.a[512] : publisher.a[256]
  bpm.l
EndStructure

Structure ZFORK_BroadcastConfig Align #PB_Structure_AlignC
  host.a[256]
  port.w
  mount.a[128] : user.a[64] : password.a[128] : streamName.a[128]
  bitrateKbps.l : sampleRate.l : channels.l
  stereoMode.l : serverType.l
  autoReconnect.a : _p0.a : _p1.a : _p2.a
  reconnectDelayMs.l
EndStructure

Structure ZFORK_BroadcastStats Align #PB_Structure_AlignC
  bytesSent.q : framesSent.q : reconnectCount.q : droppedFrames.q
  connected.a : _p0.a : _p1.a : _p2.a : _p3.a : _p4.a : _p5.a : _p6.a
EndStructure

Structure ZFORK_BroadcastDuckingConfig Align #PB_Structure_AlignC
  enabled.a : _p0.a : _p1.a : _p2.a
  duckDb.f
  attackMs.l : releaseMs.l
  threshold.f
EndStructure

Structure ZFORK_Config
  structSize.l
  waveOutDeviceId.l
  bufferMs.l
  masterVolumeLeft.l : masterVolumeRight.l
  playerVolumeLeft.l : playerVolumeRight.l
  monoMix.l
  outputBackend.l
EndStructure

""")
    lines.append("; Prototypes (__stdcall)")
    lines.extend(protos)
    lines.append("")
    for n in names:
        lines.append(f"Global {n}.{n}")
    lines.append("\nGlobal _libZFork.i = 0\n")
    lines.append("Procedure.i ZForkLoad(dllPath.s = \"libZFork.dll\")")
    lines.append("  _libZFork = OpenLibrary(#PB_Any, dllPath)")
    lines.append("  If Not _libZFork : ProcedureReturn #False : EndIf")
    for n in names:
        lines.append(f'  {n} = GetFunction(_libZFork, "{n}")')
    lines.append("  ProcedureReturn #True")
    lines.append("EndProcedure\n")
    lines.append("Procedure ZForkUnload()")
    lines.append("  If _libZFork : CloseLibrary(_libZFork) : _libZFork = 0 : EndIf")
    lines.append("EndProcedure\n")
    lines.append("""Procedure.s ZForkGetError(handle.i)
  If zfork_GetError : ProcedureReturn PeekS(zfork_GetError(handle), -1, #PB_Ascii) : EndIf
  ProcedureReturn ""
EndProcedure

Procedure.s ZForkGetBroadcastError(handle.i)
  If zfork_GetBroadcastError : ProcedureReturn PeekS(zfork_GetBroadcastError(handle), -1, #PB_Ascii) : EndIf
  ProcedureReturn ""
EndProcedure

Procedure.i ZForkOpenFile(handle.i, path.s, format.i = #ZFORK_FMT_AUTODETECT)
  ProcedureReturn Bool(zfork_OpenFile(handle, @path, format))
EndProcedure

Procedure.f ZForkDetectBPM(handle.i, algo.i = #ZFORK_BPM_ADVANCED)
  Define bpm.f = 0.0
  If zfork_DetectBPM : zfork_DetectBPM(handle, algo, @bpm) : EndIf
  ProcedureReturn bpm
EndProcedure

Procedure.i ZForkGetFFT(handle.i, Array leftBuf.f(1), Array rightBuf.f(1), points.i)
  If zfork_GetFFTData : ProcedureReturn zfork_GetFFTData(handle, @leftBuf(0), @rightBuf(0), points) : EndIf
  ProcedureReturn 0
EndProcedure

Procedure ZForkInitConfig(*cfg.ZFORK_Config)
  FillMemory(*cfg, SizeOf(ZFORK_Config), 0)
  *cfg\\structSize = SizeOf(ZFORK_Config)
  *cfg\\waveOutDeviceId = -1
  *cfg\\masterVolumeLeft = -1 : *cfg\\masterVolumeRight = -1
  *cfg\\playerVolumeLeft = -1 : *cfg\\playerVolumeRight = -1
  *cfg\\monoMix = -1
  *cfg\\outputBackend = #ZFORK_OUTPUT_AUTO
EndProcedure
""")
    out = ROOT / "bindings/purebasic/libzfork.pb"
    out.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"OK purebasic {out.relative_to(ROOT)}  ({len(names)} exports)")


ZFORK2_EXTRA_PB = r"""
; --- synced extras (cache / bank / effects / probe) ---
Prototype.i  zfork2_CreateContextWithBackend(backend.i)
Prototype.i  zfork2_ProbeFile(*pathW, *outInfo)
Prototype.i  zfork2_DetectFileBPM(*pathW, algo.i, *outBpm)
Prototype.i  zfork2_GetOutputBackendInfo(ctx.i, *outName, nameCapacity.l, *outPeriodFrames)
Prototype    zfork2_SetSoundCacheBudget(budgetBytes.q)
Prototype.q  zfork2_GetSoundCacheBudget()
Prototype    zfork2_GetSoundCacheStats(*outUsedBytes, *outEntryCount)
Prototype    zfork2_ClearSoundCache()
Prototype.i  zfork2_CompileBankV1(*textPathW, *binaryPathW)
Prototype.i  zfork2_CompileBankV1Embedded(*textPathW, *binaryPathW)
Prototype.i  zfork2_GetBankEventName(bank.i, index.l, *outName, nameCapacity.l)
Prototype    zfork2_SetVoiceTempo(ctx.i, voice.l, tempo.f)
Prototype    zfork2_GetGroupLevels(ctx.i, group.l, *outLeft, *outRight)
Prototype    zfork2_SetGroupEcho(ctx.i, group.l, *params)
Prototype    zfork2_SetGroupReverb(ctx.i, group.l, *params)
Prototype    zfork2_ClearGroupEffect(ctx.i, group.l, slot.i)

#ZF2_OUTPUT_AUTO    = 0
#ZF2_OUTPUT_WASAPI  = 1
#ZF2_OUTPUT_WAVEOUT = 2
#ZF2_MAX_EFFECT_SLOTS = 4
#ZF2_EFFECT_ECHO      = 0
#ZF2_EFFECT_REVERB    = 1
#ZF2_BPM_PEAKS           = 0
#ZF2_BPM_AUTOCORRELATION = 1
#ZF2_BPM_ADVANCED        = 2

Structure ZF2_FileProbe
  sampleRate.l : channels.l : bitrate.l : vbr.l
  lengthMs.l
  bpmTag.f
  title.a[256] : artist.a[256] : album.a[256] : year.a[16]
EndStructure

Structure ZF2_EchoParams
  leftDelayMs.l : leftSourceVolume.l : leftEchoVolume.l
  rightDelayMs.l : rightSourceVolume.l : rightEchoVolume.l
EndStructure

Structure ZF2_ReverbParams
  roomSize.f : damping.f : wet.f : dry.f
EndStructure
"""


def patch_purebasic_v2() -> None:
    path = ROOT / "bindings/purebasic/libzfork2.pb"
    text = path.read_text(encoding="utf-8")
    extras = [
        "zfork2_CreateContextWithBackend",
        "zfork2_ProbeFile",
        "zfork2_DetectFileBPM",
        "zfork2_GetOutputBackendInfo",
        "zfork2_SetSoundCacheBudget",
        "zfork2_GetSoundCacheBudget",
        "zfork2_GetSoundCacheStats",
        "zfork2_ClearSoundCache",
        "zfork2_CompileBankV1",
        "zfork2_CompileBankV1Embedded",
        "zfork2_GetBankEventName",
        "zfork2_SetVoiceTempo",
        "zfork2_GetGroupLevels",
        "zfork2_SetGroupEcho",
        "zfork2_SetGroupReverb",
        "zfork2_ClearGroupEffect",
    ]
    if "zfork2_CreateContextWithBackend" in text and "zfork2_CompileBankV1Embedded" in text:
        # still ensure all present
        missing = [e for e in extras if e not in text]
        if not missing:
            print("OK purebasic libzfork2.pb already complete")
            return

    # Insert prototypes after existing CreateContext prototype block
    marker = "Prototype.i  zfork2_CreateContext()"
    if marker in text and "zfork2_CreateContextWithBackend" not in text:
        text = text.replace(
            marker,
            marker + "\nPrototype.i  zfork2_CreateContextWithBackend(backend.i)",
            1,
        )

    inserts = []
    for name, proto in [
        ("zfork2_ProbeFile", "Prototype.i  zfork2_ProbeFile(*pathW, *outInfo)"),
        ("zfork2_DetectFileBPM", "Prototype.i  zfork2_DetectFileBPM(*pathW, algo.i, *outBpm)"),
        ("zfork2_GetOutputBackendInfo", "Prototype.i  zfork2_GetOutputBackendInfo(ctx.i, *outName, nameCapacity.l, *outPeriodFrames)"),
        ("zfork2_SetSoundCacheBudget", "Prototype    zfork2_SetSoundCacheBudget(budgetBytes.q)"),
        ("zfork2_GetSoundCacheBudget", "Prototype.q  zfork2_GetSoundCacheBudget()"),
        ("zfork2_GetSoundCacheStats", "Prototype    zfork2_GetSoundCacheStats(*outUsedBytes, *outEntryCount)"),
        ("zfork2_ClearSoundCache", "Prototype    zfork2_ClearSoundCache()"),
        ("zfork2_CompileBankV1", "Prototype.i  zfork2_CompileBankV1(*textPathW, *binaryPathW)"),
        ("zfork2_CompileBankV1Embedded", "Prototype.i  zfork2_CompileBankV1Embedded(*textPathW, *binaryPathW)"),
        ("zfork2_GetBankEventName", "Prototype.i  zfork2_GetBankEventName(bank.i, index.l, *outName, nameCapacity.l)"),
        ("zfork2_SetVoiceTempo", "Prototype    zfork2_SetVoiceTempo(ctx.i, voice.l, tempo.f)"),
        ("zfork2_GetGroupLevels", "Prototype    zfork2_GetGroupLevels(ctx.i, group.l, *outLeft, *outRight)"),
        ("zfork2_SetGroupEcho", "Prototype    zfork2_SetGroupEcho(ctx.i, group.l, *params)"),
        ("zfork2_SetGroupReverb", "Prototype    zfork2_SetGroupReverb(ctx.i, group.l, *params)"),
        ("zfork2_ClearGroupEffect", "Prototype    zfork2_ClearGroupEffect(ctx.i, group.l, slot.i)"),
    ]:
        if name not in text:
            inserts.append(proto)

    if inserts:
        # append before Global section
        gmark = "\nGlobal zfork2_"
        idx = text.find(gmark)
        block = "\n; --- API sync additions ---\n" + "\n".join(inserts) + "\n"
        if idx >= 0:
            text = text[:idx] + block + text[idx:]
        else:
            text += block

    # Globals + GetFunction bindings
    for name in extras:
        gline = f"Global {name}.{name}"
        if gline not in text:
            # after first Global zfork2_CreateContext line
            m = re.search(r"Global zfork2_CreateContext\.zfork2_CreateContext\n", text)
            if m:
                text = text[: m.end()] + gline + "\n" + text[m.end() :]
            else:
                text += "\n" + gline + "\n"

        bind = f'  {name} = GetFunction(_libZFork2, "{name}")'
        if bind not in text and f'GetFunction(_libZFork2, "{name}")' not in text:
            # find CreateContext GetFunction
            m = re.search(r'  zfork2_CreateContext\s*=\s*GetFunction\(_libZFork2,\s*"zfork2_CreateContext"\)\n', text)
            if m:
                text = text[: m.end()] + bind + "\n" + text[m.end() :]

    # Constants / structures if missing
    if "#ZF2_OUTPUT_AUTO" not in text:
        text = text.replace(
            "#ZF2_SOUND_PREDECODED",
            "#ZF2_OUTPUT_AUTO    = 0\n#ZF2_OUTPUT_WASAPI  = 1\n#ZF2_OUTPUT_WAVEOUT = 2\n"
            "#ZF2_MAX_EFFECT_SLOTS = 4\n#ZF2_EFFECT_ECHO = 0\n#ZF2_EFFECT_REVERB = 1\n"
            "#ZF2_BPM_PEAKS = 0\n#ZF2_BPM_AUTOCORRELATION = 1\n#ZF2_BPM_ADVANCED = 2\n\n"
            "#ZF2_SOUND_PREDECODED",
            1,
        )
    if "Structure ZF2_FileProbe" not in text:
        text += """
Structure ZF2_FileProbe
  sampleRate.l : channels.l : bitrate.l : vbr.l
  lengthMs.l
  bpmTag.f
  title.a[256] : artist.a[256] : album.a[256] : year.a[16]
EndStructure

Structure ZF2_EchoParams
  leftDelayMs.l : leftSourceVolume.l : leftEchoVolume.l
  rightDelayMs.l : rightSourceVolume.l : rightEchoVolume.l
EndStructure

Structure ZF2_ReverbParams
  roomSize.f : damping.f : wet.f : dry.f
EndStructure
"""
    path.write_text(text, encoding="utf-8", newline="\n")
    print(f"OK purebasic bindings/purebasic/libzfork2.pb patched")


def patch_csharp_v2() -> None:
    path = ROOT / "bindings/csharp/LibZFork2.cs"
    text = path.read_text(encoding="utf-8")
    additions = []

    def need(sym: str, block: str) -> None:
        if sym not in text:
            additions.append(block)

    need(
        "zfork2_CreateContextWithBackend",
        """
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr zfork2_CreateContextWithBackend(int backend);
""",
    )
    need(
        "zfork2_ProbeFile",
        """
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zfork2_ProbeFile([MarshalAs(UnmanagedType.LPWStr)] string path, out ZF2FileProbe outInfo);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zfork2_DetectFileBPM([MarshalAs(UnmanagedType.LPWStr)] string path, int algo, out float outBpm);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zfork2_GetOutputBackendInfo(IntPtr ctx, byte[] outName, uint nameCapacity, out uint outPeriodFrames);
""",
    )
    need(
        "zfork2_SetSoundCacheBudget",
        """
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetSoundCacheBudget(ulong budgetBytes);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong zfork2_GetSoundCacheBudget();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_GetSoundCacheStats(out ulong outUsedBytes, out uint outEntryCount);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_ClearSoundCache();
""",
    )
    need(
        "zfork2_CompileBankV1",
        """
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zfork2_CompileBankV1([MarshalAs(UnmanagedType.LPWStr)] string textPath,
            [MarshalAs(UnmanagedType.LPWStr)] string binaryPath);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zfork2_CompileBankV1Embedded([MarshalAs(UnmanagedType.LPWStr)] string textPath,
            [MarshalAs(UnmanagedType.LPWStr)] string binaryPath);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zfork2_GetBankEventName(IntPtr bank, uint index, byte[] outName, uint nameCapacity);
""",
    )
    need(
        "zfork2_SetVoiceTempo",
        """
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetVoiceTempo(IntPtr ctx, uint voice, float tempo);
""",
    )
    need(
        "zfork2_GetGroupLevels",
        """
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_GetGroupLevels(IntPtr ctx, uint group, out float outLeft, out float outRight);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetGroupEcho(IntPtr ctx, uint group, ref ZF2EchoParams params);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetGroupReverb(IntPtr ctx, uint group, ref ZF2ReverbParams params);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_ClearGroupEffect(IntPtr ctx, uint group, int slot);
""",
    )

    struct_block = """
    public static class ZF2Output
    {
        public const int Auto = 0, Wasapi = 1, WaveOut = 2;
    }
    public static class ZF2Effect
    {
        public const int MaxSlots = 4, Echo = 0, Reverb = 1;
    }
    public static class ZF2Bpm
    {
        public const int Peaks = 0, AutoCorrelation = 1, Advanced = 2;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct ZF2FileProbe
    {
        public int sampleRate, channels, bitrate, vbr;
        public uint lengthMs;
        public float bpmTag;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string title;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string artist;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string album;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 16)]  public string year;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct ZF2EchoParams
    {
        public int leftDelayMs, leftSourceVolume, leftEchoVolume;
        public int rightDelayMs, rightSourceVolume, rightEchoVolume;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct ZF2ReverbParams
    {
        public float roomSize, damping, wet, dry;
    }
"""

    if "struct ZF2FileProbe" not in text and "ZF2FileProbe" not in text:
        # insert after ZF2Event class
        text = text.replace(
            "public static class ZF2Event\n    {\n        public const int VoiceStart      = 0;\n        public const int VoiceFinished   = 1;\n        public const int VoiceLoop       = 2;\n        public const int PositionCrossed = 3;\n    }",
            "public static class ZF2Event\n    {\n        public const int VoiceStart      = 0;\n        public const int VoiceFinished   = 1;\n        public const int VoiceLoop       = 2;\n        public const int PositionCrossed = 3;\n    }\n"
            + struct_block,
            1,
        )

    if additions:
        # insert into ZF2Native after CreateContext
        anchor = "public static extern IntPtr zfork2_CreateContext();"
        if anchor in text:
            text = text.replace(anchor, anchor + "\n" + "".join(additions), 1)
        else:
            text += "\n" + "".join(additions)

    # Managed helpers on ZForkContext if class exists
    if "class ZForkContext" in text and "CreateContextWithBackend" not in text:
        text = text.replace(
            "public ZForkContext()\n        {\n            _ctx = ZF2Native.zfork2_CreateContext();",
            """public ZForkContext(int backend = ZF2Output.Auto)
        {
            _ctx = backend == ZF2Output.Auto
                ? ZF2Native.zfork2_CreateContext()
                : ZF2Native.zfork2_CreateContextWithBackend(backend);""",
            1,
        )

    if "SetVoiceTempo" not in text and "SetVoicePitch" in text:
        text = text.replace(
            "public void SetVoicePitch(uint voice, float semitones)\n        {\n            ThrowIfDisposed();\n            ZF2Native.zfork2_SetVoicePitch(_ctx, voice, semitones);\n        }",
            "public void SetVoicePitch(uint voice, float semitones)\n        {\n            ThrowIfDisposed();\n            ZF2Native.zfork2_SetVoicePitch(_ctx, voice, semitones);\n        }\n\n"
            "        public void SetVoiceTempo(uint voice, float tempo)\n        {\n            ThrowIfDisposed();\n            ZF2Native.zfork2_SetVoiceTempo(_ctx, voice, tempo);\n        }",
            1,
        )

    if "GetGroupLevels" not in text and "SetGroupMute" in text:
        # append methods before Dispose if possible — soft insert
        insert = """
        public (float Left, float Right) GetGroupLevels(uint group)
        {
            ThrowIfDisposed();
            ZF2Native.zfork2_GetGroupLevels(_ctx, group, out float l, out float r);
            return (l, r);
        }
        public void SetGroupEcho(uint group, ZF2EchoParams p)
        { ThrowIfDisposed(); ZF2Native.zfork2_SetGroupEcho(_ctx, group, ref p); }
        public void SetGroupReverb(uint group, ZF2ReverbParams p)
        { ThrowIfDisposed(); ZF2Native.zfork2_SetGroupReverb(_ctx, group, ref p); }
        public void ClearGroupEffect(uint group, int slot)
        { ThrowIfDisposed(); ZF2Native.zfork2_ClearGroupEffect(_ctx, group, slot); }

        public static void SetSoundCacheBudget(ulong budgetBytes) => ZF2Native.zfork2_SetSoundCacheBudget(budgetBytes);
        public static ulong GetSoundCacheBudget() => ZF2Native.zfork2_GetSoundCacheBudget();
        public static (ulong UsedBytes, uint EntryCount) GetSoundCacheStats()
        {
            ZF2Native.zfork2_GetSoundCacheStats(out ulong u, out uint n);
            return (u, n);
        }
        public static void ClearSoundCache() => ZF2Native.zfork2_ClearSoundCache();
        public static bool CompileBankV1(string textPath, string binaryPath)
            => ZF2Native.zfork2_CompileBankV1(textPath, binaryPath) != 0;
        public static bool CompileBankV1Embedded(string textPath, string binaryPath)
            => ZF2Native.zfork2_CompileBankV1Embedded(textPath, binaryPath) != 0;
        public static bool ProbeFile(string path, out ZF2FileProbe info)
            => ZF2Native.zfork2_ProbeFile(path, out info) != 0;
        public static float DetectFileBpm(string path, int algo = ZF2Bpm.Advanced)
        {
            ZF2Native.zfork2_DetectFileBPM(path, algo, out float bpm);
            return bpm;
        }
"""
        # before Dispose()
        text = text.replace("        public void Dispose()", insert + "\n        public void Dispose()", 1)

    path.write_text(text, encoding="utf-8", newline="\n")
    print(f"OK csharp   bindings/csharp/LibZFork2.cs patched")


def verify() -> None:
    h1 = (ROOT / "include/libzfork/libzfork.h").read_text(encoding="utf-8")
    h2 = (ROOT / "include/libzfork/libzfork2.h").read_text(encoding="utf-8")
    f1 = re.findall(r"ZFORK_API\s+[\w\s\*:]+\s+ZFORK_CALL\s+(\w+)\s*\(", h1)
    f2 = re.findall(r"ZF2_API\s+[\w\s\*:]+\s+(\w+)\s*\(", h2)
    files = {
        "cs1": ROOT / "bindings/csharp/LibZFork.cs",
        "cs2": ROOT / "bindings/csharp/LibZFork2.cs",
        "pa1": ROOT / "bindings/pascal/libzfork.pas",
        "pa2": ROOT / "bindings/pascal/libzfork2.pas",
        "pb1": ROOT / "bindings/purebasic/libzfork.pb",
        "pb2": ROOT / "bindings/purebasic/libzfork2.pb",
    }
    print("\n=== verify ===")
    for key, path in files.items():
        names = f1 if key.endswith("1") else f2
        t = path.read_text(encoding="utf-8")
        miss = [n for n in names if n not in t]
        print(f"{key}: missing {len(miss)}/{len(names)}" + ("" if not miss else " → " + ", ".join(miss)))


def main() -> None:
    sync_pascal()
    gen_csharp_v1()
    gen_purebasic_v1()
    patch_csharp_v2()
    patch_purebasic_v2()
    # also refresh dist/bindings if present
    dist = ROOT / "dist/bindings"
    if dist.is_dir():
        for sub in ("csharp", "pascal", "purebasic"):
            src = ROOT / "bindings" / sub
            dst = dist / sub
            if src.is_dir():
                if dst.exists():
                    shutil.rmtree(dst)
                shutil.copytree(src, dst)
        print("OK synced dist/bindings")
    verify()


if __name__ == "__main__":
    main()
