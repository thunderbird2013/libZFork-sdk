/*
 * libZFork2 — C# / .NET Bindings  (Game Audio Engine)
 * =====================================================
 * Target  : .NET 6+ / .NET Framework 4.7.2+
 * Platform: Windows x64
 *
 * Setup:
 *   1. Copy libZFork.dll next to your executable.
 *   2. Add LibZFork2.cs to your project.
 *
 * Quick example (.NET 6+):
 *
 *   using LibZFork;
 *
 *   using var ctx = new ZForkContext();
 *   ctx.Start();
 *
 *   var sfxGroup = ctx.GetGroup("sfx");
 *   var sound    = ctx.LoadSound(@"C:\sounds\boom.wav");
 *   uint voice   = ctx.PlaySound(sound, sfxGroup);
 *
 *   // Game loop:
 *   while (running) {
 *       ctx.Update();     // dispatches callbacks on this thread
 *   }
 *
 *   ctx.UnloadSound(sound);
 */

using System;
using System.Runtime.InteropServices;

namespace LibZFork
{
    // ── Event callback delegate ───────────────────────────────────────────────

    /// <summary>Called by <see cref="ZForkContext.Update"/> for each queued engine event.</summary>
    /// <param name="eventType">One of the <see cref="ZF2Event"/> values.</param>
    /// <param name="voice">The voice that triggered the event.</param>
    /// <param name="param1">Loops remaining (VoiceLoop) or markerId (PositionCrossed).</param>
    /// <param name="param2">Position in seconds (PositionCrossed).</param>
    /// <param name="userData">The userData pointer passed to <see cref="ZForkContext.SetCallback"/>.</param>
    public delegate void ZF2EventCallback(int eventType, uint voice,
                                          uint param1, float param2, IntPtr userData);

    /// <summary>Event type values received in <see cref="ZF2EventCallback"/>.</summary>
    public static class ZF2Event
    {
        public const int VoiceStart      = 0;
        public const int VoiceFinished   = 1;
        public const int VoiceLoop       = 2;
        public const int PositionCrossed = 3;
    }

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


    // ── Low-level P/Invoke ────────────────────────────────────────────────────

    internal static class ZF2Native
    {
        private const string DllName = "libZFork";

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr zfork2_CreateContext();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr zfork2_CreateContextWithBackend(int backend);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zfork2_ProbeFile([MarshalAs(UnmanagedType.LPWStr)] string path, out ZF2FileProbe outInfo);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zfork2_DetectFileBPM([MarshalAs(UnmanagedType.LPWStr)] string path, int algo, out float outBpm);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zfork2_GetOutputBackendInfo(IntPtr ctx, byte[] outName, uint nameCapacity, out uint outPeriodFrames);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetSoundCacheBudget(ulong budgetBytes);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong zfork2_GetSoundCacheBudget();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_GetSoundCacheStats(out ulong outUsedBytes, out uint outEntryCount);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_ClearSoundCache();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zfork2_CompileBankV1([MarshalAs(UnmanagedType.LPWStr)] string textPath,
            [MarshalAs(UnmanagedType.LPWStr)] string binaryPath);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zfork2_CompileBankV1Embedded([MarshalAs(UnmanagedType.LPWStr)] string textPath,
            [MarshalAs(UnmanagedType.LPWStr)] string binaryPath);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zfork2_GetBankEventName(IntPtr bank, uint index, byte[] outName, uint nameCapacity);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetVoiceTempo(IntPtr ctx, uint voice, float tempo);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_GetGroupLevels(IntPtr ctx, uint group, out float outLeft, out float outRight);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetGroupEcho(IntPtr ctx, uint group, ref ZF2EchoParams params);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetGroupReverb(IntPtr ctx, uint group, ref ZF2ReverbParams params);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_ClearGroupEffect(IntPtr ctx, uint group, int slot);


        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_DestroyContext(IntPtr ctx);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zfork2_Start(IntPtr ctx);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_Stop(IntPtr ctx);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern uint zfork2_RegisterGroup(IntPtr ctx,
            [MarshalAs(UnmanagedType.LPStr)] string name);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern uint zfork2_GetGroup(IntPtr ctx,
            [MarshalAs(UnmanagedType.LPStr)] string name);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetCallback(IntPtr ctx,
            ZF2EventCallback? cb, IntPtr userData);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_Update(IntPtr ctx);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr zfork2_LoadSound(IntPtr ctx,
            [MarshalAs(UnmanagedType.LPWStr)] string path, int type);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_UnloadSound(IntPtr sound);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern uint zfork2_PlaySound(IntPtr ctx, IntPtr sound, uint group);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_StopVoice(IntPtr ctx, uint voice);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_PauseVoice(IntPtr ctx, uint voice);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_ResumeVoice(IntPtr ctx, uint voice);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zfork2_IsVoiceActive(IntPtr ctx, uint voice);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetVoiceVolume(IntPtr ctx, uint voice, float volume);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetVoicePan(IntPtr ctx, uint voice, float pan);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetVoicePitch(IntPtr ctx, uint voice, float semitones);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetVoiceLoop(IntPtr ctx, uint voice, int loopCount);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetVoicePriority(IntPtr ctx, uint voice, int priority);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetVoicePosition2D(IntPtr ctx, uint voice, float x, float y);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetVoicePosition3D(IntPtr ctx, uint voice, float x, float y, float z);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetVoiceMarker(IntPtr ctx, uint voice, uint markerId, float posSeconds);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_ClearVoiceMarkers(IntPtr ctx, uint voice);

        // ── SoundBank V1 ──────────────────────────────────────────────────────────

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr zfork2_LoadBankV1(IntPtr ctx,
            [MarshalAs(UnmanagedType.LPWStr)] string path);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_UnloadBankV1(IntPtr bank);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern uint zfork2_GetBankEntryCount(IntPtr bank);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern uint zfork2_GetBankSkippedCount(IntPtr bank);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern uint zfork2_PlayEventV1(IntPtr ctx, IntPtr bank,
            [MarshalAs(UnmanagedType.LPStr)] string eventId);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern uint zfork2_PlayEventOnGroupV1(IntPtr ctx, IntPtr bank,
            [MarshalAs(UnmanagedType.LPStr)] string eventId, uint group);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetGroupVolume(IntPtr ctx, uint group, float volume);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetGroupMute(IntPtr ctx, uint group, int muted);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetListenerPosition2D(IntPtr ctx, float x, float y);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetListenerPosition3D(IntPtr ctx, float x, float y, float z);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetListenerDirection(IntPtr ctx, float dx, float dy, float dz);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zfork2_SetListenerRange(IntPtr ctx,
            float minDist, float maxDist, float rolloff);
    }

    // ── Constants ─────────────────────────────────────────────────────────────

    /// <summary>Sound loading modes.</summary>
    public static class ZF2SoundType
    {
        public const int PreDecoded = 0;
        public const int Streaming  = 1;
    }

    // ── High-level managed wrapper ────────────────────────────────────────────

    /// <summary>
    /// High-level wrapper around a ZF2_CONTEXT. Dispose to release resources.
    /// </summary>
    public sealed class ZForkContext : IDisposable
    {
        private IntPtr _ctx;
        private bool   _disposed;

        // Keep a reference so the delegate is not collected while the context lives.
        private ZF2EventCallback? _callbackRef;

        public static readonly uint InvalidVoice = 0u;
        public static readonly uint InvalidGroup = 0xFFFFFFFFu;

        /// <exception cref="InvalidOperationException">Thrown if the native context cannot be created.</exception>
        public ZForkContext()
        {
            _ctx = ZF2Native.zfork2_CreateContext();
            if (_ctx == IntPtr.Zero)
                throw new InvalidOperationException("zfork2_CreateContext failed.");
        }

        // ── Lifecycle ─────────────────────────────────────────────────────────

        /// <returns>true on success.</returns>
        public bool Start() => ZF2Native.zfork2_Start(_ctx) != 0;

        public void Stop() => ZF2Native.zfork2_Stop(_ctx);

        // ── Groups ────────────────────────────────────────────────────────────

        public uint RegisterGroup(string name) => ZF2Native.zfork2_RegisterGroup(_ctx, name);
        public uint GetGroup(string name)      => ZF2Native.zfork2_GetGroup(_ctx, name);

        // ── Events ────────────────────────────────────────────────────────────

        public void SetCallback(ZF2EventCallback cb, IntPtr userData = default)
        {
            _callbackRef = cb;
            ZF2Native.zfork2_SetCallback(_ctx, cb, userData);
        }

        public void ClearCallback()
        {
            _callbackRef = null;
            ZF2Native.zfork2_SetCallback(_ctx, null, IntPtr.Zero);
        }

        public void Update() => ZF2Native.zfork2_Update(_ctx);

        // ── Sounds ────────────────────────────────────────────────────────────

        public IntPtr LoadSound(string path, int type = ZF2SoundType.PreDecoded)
            => ZF2Native.zfork2_LoadSound(_ctx, path, type);

        public void UnloadSound(IntPtr sound)
            => ZF2Native.zfork2_UnloadSound(sound);

        // ── Voices ────────────────────────────────────────────────────────────

        public uint PlaySound(IntPtr sound, uint group)
            => ZF2Native.zfork2_PlaySound(_ctx, sound, group);

        public void StopVoice(uint voice)   => ZF2Native.zfork2_StopVoice(_ctx, voice);
        public void PauseVoice(uint voice)  => ZF2Native.zfork2_PauseVoice(_ctx, voice);
        public void ResumeVoice(uint voice) => ZF2Native.zfork2_ResumeVoice(_ctx, voice);

        public bool IsVoiceActive(uint voice)
            => ZF2Native.zfork2_IsVoiceActive(_ctx, voice) != 0;

        public void SetVoiceVolume(uint voice, float volume)
            => ZF2Native.zfork2_SetVoiceVolume(_ctx, voice, volume);

        public void SetVoicePan(uint voice, float pan)
            => ZF2Native.zfork2_SetVoicePan(_ctx, voice, pan);

        public void SetVoicePitch(uint voice, float semitones)
            => ZF2Native.zfork2_SetVoicePitch(_ctx, voice, semitones);

        public void SetVoiceLoop(uint voice, int loopCount)
            => ZF2Native.zfork2_SetVoiceLoop(_ctx, voice, loopCount);

        public void SetVoicePriority(uint voice, int priority)
            => ZF2Native.zfork2_SetVoicePriority(_ctx, voice, priority);

        public void SetVoicePosition2D(uint voice, float x, float y)
            => ZF2Native.zfork2_SetVoicePosition2D(_ctx, voice, x, y);

        public void SetVoicePosition3D(uint voice, float x, float y, float z)
            => ZF2Native.zfork2_SetVoicePosition3D(_ctx, voice, x, y, z);

        public void SetVoiceMarker(uint voice, uint markerId, float posSeconds)
            => ZF2Native.zfork2_SetVoiceMarker(_ctx, voice, markerId, posSeconds);

        public void ClearVoiceMarkers(uint voice)
            => ZF2Native.zfork2_ClearVoiceMarkers(_ctx, voice);

        // ── SoundBank V1 ──────────────────────────────────────────────────────────

        /// <summary>
        /// Load a text soundbank. Returns <see cref="IntPtr.Zero"/> if the file
        /// cannot be opened. Call <see cref="GetBankEntryCount"/> to verify how
        /// many sounds actually loaded after a successful call.
        /// </summary>
        public IntPtr LoadBankV1(string path)
            => ZF2Native.zfork2_LoadBankV1(_ctx, path);

        /// <summary>Release a bank handle and all its loaded sounds.</summary>
        public void UnloadBankV1(IntPtr bank)
            => ZF2Native.zfork2_UnloadBankV1(bank);

        /// <summary>
        /// Number of events successfully loaded from the bank.
        /// 0 means all entries failed — check <see cref="GetBankSkippedCount"/>.
        /// </summary>
        public uint GetBankEntryCount(IntPtr bank)
            => ZF2Native.zfork2_GetBankEntryCount(bank);

        /// <summary>
        /// Number of entries in the bank file whose sound files could not be
        /// loaded (missing file, unsupported format, wrong path, ...).
        /// </summary>
        public uint GetBankSkippedCount(IntPtr bank)
            => ZF2Native.zfork2_GetBankSkippedCount(bank);

        /// <summary>Play an event using the defaults from the bank file.</summary>
        /// <returns>A stamped voice id, or <see cref="InvalidVoice"/> on failure.</returns>
        public uint PlayEventV1(IntPtr bank, string eventId)
            => ZF2Native.zfork2_PlayEventV1(_ctx, bank, eventId);

        /// <summary>
        /// Play an event but route it through <paramref name="group"/> instead
        /// of the group specified in the bank file.
        /// </summary>
        public uint PlayEventOnGroupV1(IntPtr bank, string eventId, uint group)
            => ZF2Native.zfork2_PlayEventOnGroupV1(_ctx, bank, eventId, group);

        // ── Groups ────────────────────────────────────────────────────────────

        public void SetGroupVolume(uint group, float volume)
            => ZF2Native.zfork2_SetGroupVolume(_ctx, group, volume);

        public void SetGroupMute(uint group, bool muted)
            => ZF2Native.zfork2_SetGroupMute(_ctx, group, muted ? 1 : 0);

        // ── Listener ──────────────────────────────────────────────────────────

        public void SetListenerPosition2D(float x, float y)
            => ZF2Native.zfork2_SetListenerPosition2D(_ctx, x, y);

        public void SetListenerPosition3D(float x, float y, float z)
            => ZF2Native.zfork2_SetListenerPosition3D(_ctx, x, y, z);

        public void SetListenerDirection(float dx, float dy, float dz)
            => ZF2Native.zfork2_SetListenerDirection(_ctx, dx, dy, dz);

        public void SetListenerRange(float minDist, float maxDist, float rolloff)
            => ZF2Native.zfork2_SetListenerRange(_ctx, minDist, maxDist, rolloff);

        // ── IDisposable ───────────────────────────────────────────────────────

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            ZF2Native.zfork2_DestroyContext(_ctx);
            _ctx = IntPtr.Zero;
        }
    }
}
