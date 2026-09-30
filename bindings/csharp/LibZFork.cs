/*
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
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string key;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct MusicalKeyInfo
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string raw;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 16)] public string musical;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 8)]  public string camelot;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 8)]  public string openKey;
        public int wheel;
        public int isMinor; // 1=minor, 0=major, -1=unknown
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


    internal static class Native
    {
        internal const string DllName = "libZFork.dll";
        private const CallingConvention CC = CallingConvention.StdCall;


        [DllImport(DllName, CallingConvention = CC)]
        internal static extern IntPtr zfork_Create();

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern IntPtr zfork_CreateWithConfig(ref ZForkConfig config);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern void zfork_Destroy(IntPtr h);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_OpenFile(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] string path, int format);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_OpenFileA(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string path, int format);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_LoadPlaylistFile(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] string path, int fallbackFormat);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_LoadPlaylistFileA(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string path, int fallbackFormat);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_AddFile(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] string path, int format);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_AddFileA(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string path, int format);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_RemoveQueueItem(IntPtr h, uint index);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_MoveQueueItem(IntPtr h, uint fromIndex, uint toIndex);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern uint zfork_GetQueueCount(IntPtr h);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_GetQueueItemPathA(IntPtr h, uint index, byte[] outBuf, uint outBytes);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_OpenStream(IntPtr h, IntPtr data, uint size, int format);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern void zfork_Close(IntPtr h);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_Play(IntPtr h);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_PlayLoop(IntPtr h, uint count);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_Pause(IntPtr h);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_Resume(IntPtr h);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_Stop(IntPtr h);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_SeekMs(IntPtr h, int origin, uint positionMs);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_GetPositionMs(IntPtr h, out uint positionMs);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_GetStreamInfo(IntPtr h, int songIndex, out StreamInfo outInfo);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_GetStatus(IntPtr h, out StreamStatus outStatus);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_SetMasterVolume(IntPtr h, int left, int right);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_GetMasterVolume(IntPtr h, out int left, out int right);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_SetPlayerVolume(IntPtr h, int left, int right);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_GetPlayerVolume(IntPtr h, out int left, out int right);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_GetVUData(IntPtr h, out float left, out float right);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_MixChannels(IntPtr h, int mono);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_SetRate(IntPtr h, float rate);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_SetPitch(IntPtr h, float semitones);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_SetTempo(IntPtr h, float tempo);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_EnableEqualizer(IntPtr h, int enable);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_SetEQBandGain(IntPtr h, int band, float gainDb);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_GetEQBandGain(IntPtr h, int band, out float gainDb);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_ResetEqualizer(IntPtr h);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_EnableEcho(IntPtr h, int enable);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_SetEchoParams(IntPtr h, ref EchoParams params);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_GetEchoParams(IntPtr h, out EchoParams outParams);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_EnableVocalCut(IntPtr h, int enable);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_EnableSideCut(IntPtr h, int enable);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_EnableStereoEnhancer(IntPtr h, int enable);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_SetStereoWidth(IntPtr h, float width);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_GetStereoWidth(IntPtr h, out float outWidth);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_SetBufferSize(IntPtr h, uint milliseconds);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_GetFFTData(IntPtr h, [In, Out] float[] left, [In, Out] float[] right, uint points);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_DetectBPM(IntPtr h, int algo, out float outBPM);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_DetectFileBPMA(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string path, int algo, out float outBPM);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_AnalyzeFile(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] string path, uint flags, out IntPtr outHandle);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_AnalyzeFileA(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string path, uint flags, out IntPtr outHandle);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern void zfork_FreeAnalysis(IntPtr h, IntPtr analysis);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_GetAnalysisSummary(IntPtr h, IntPtr analysis, out float outBpm, out uint outLengthMs, out uint outFlags);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern uint zfork_CopyAnalysisWaveform(IntPtr h, IntPtr analysis, byte[] outBins, uint maxBins);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern uint zfork_CopyAnalysisPeaks(IntPtr h, IntPtr analysis, [In, Out] float[] outTimesMs, [In, Out] float[] outValues, uint maxPeaks);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_SetPlaylistAllowDuplicates(IntPtr h, int allow);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_GetPlaylistAllowDuplicates(IntPtr h);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_StartBroadcast(IntPtr h, ref BroadcastConfig config);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_StopBroadcast(IntPtr h);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_PushBroadcastPCM(IntPtr h, [In] float[] interleaved, uint frames, int channels, int sampleRate);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_StartBroadcastFile(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] string path, int format);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_StartBroadcastFileA(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string path, int format);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_AttachBroadcastToCurrentPlayback(IntPtr h, int enable);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_SetBroadcastMetadata(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string title, [MarshalAs(UnmanagedType.LPStr)] string artist, [MarshalAs(UnmanagedType.LPStr)] string album);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_PushBroadcastMicPCM(IntPtr h, [In] float[] interleaved, uint frames, int channels, int sampleRate);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_SetBroadcastMicActive(IntPtr h, int active);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_SetBroadcastDucking(IntPtr h, ref BroadcastDuckingConfig config);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_GetBroadcastDucking(IntPtr h, out BroadcastDuckingConfig outConfig);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_StartBroadcastMicCapture(IntPtr h, int deviceId, int sampleRate, int channels);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_StopBroadcastMicCapture(IntPtr h);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_GetBroadcastStats(IntPtr h, out BroadcastStats outStats);

        [DllImport(DllName, CallingConvention = CC)]
        [return: MarshalAs(UnmanagedType.LPStr)]
        internal static extern string? zfork_GetBroadcastError(IntPtr h);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_SetPCMOutputCallback(IntPtr h, PCMOutputCallback? callback, IntPtr userData, int disableBuiltInOutput);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_SetCallback(IntPtr h, PlayerCallback? callback, IntPtr userData);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_EnumerateOutputDevices(IntPtr h);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_GetOutputDeviceInfo(IntPtr h, int deviceId, out WaveDeviceInfo outInfo);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_SetOutputDevice(IntPtr h, int deviceId);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_LoadID3Ex(IntPtr h, int songIndex, out ID3InfoEx outInfo);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_LoadID3Picture(IntPtr h, int songIndex, int picIndex, out IntPtr outData, out uint outSize, out int outType);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_LoadFileID3ExA(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string path, out ID3InfoEx outInfo);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_LoadFileID3PictureA(IntPtr h, [MarshalAs(UnmanagedType.LPStr)] string path, int picIndex, out IntPtr outData, out uint outSize, out int outType);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern void zfork_FreeMemory(IntPtr ptr);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_ParseMusicalKey([MarshalAs(UnmanagedType.LPStr)] string text, out MusicalKeyInfo outInfo);

        [DllImport(DllName, CallingConvention = CC)]
        internal static extern int zfork_KeysCompatible(ref MusicalKeyInfo a, ref MusicalKeyInfo b);

        [DllImport(DllName, CallingConvention = CC)]
        [return: MarshalAs(UnmanagedType.LPStr)]
        internal static extern string? zfork_GetError(IntPtr h);
    }

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

    public static class MusicalKey
    {
        public static bool TryParse(string text, out MusicalKeyInfo info)
            => Native.zfork_ParseMusicalKey(text ?? "", out info) != 0;

        public static bool Compatible(MusicalKeyInfo a, MusicalKeyInfo b)
            => Native.zfork_KeysCompatible(ref a, ref b) != 0;

        public static bool Compatible(string keyA, string keyB)
        {
            if (!TryParse(keyA, out var a) || !TryParse(keyB, out var b)) return false;
            return Compatible(a, b);
        }
    }
}
