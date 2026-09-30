;
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
  key.a[32]
EndStructure

Structure ZFORK_MusicalKeyInfo
  raw.a[32]
  musical.a[16]
  camelot.a[8]
  openKey.a[8]
  wheel.l
  isMinor.l
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


; Prototypes (__stdcall)
Prototype.i  zfork_Create()
Prototype.i  zfork_CreateWithConfig(*config)
Prototype    zfork_Destroy(handle.i)
Prototype.i  zfork_OpenFile(handle.i, *pathW, format.i)
Prototype.i  zfork_OpenFileA(handle.i, *pathA, format.i)
Prototype.i  zfork_LoadPlaylistFile(handle.i, *pathW, fallbackFormat.i)
Prototype.i  zfork_LoadPlaylistFileA(handle.i, *pathA, fallbackFormat.i)
Prototype.i  zfork_AddFile(handle.i, *pathW, format.i)
Prototype.i  zfork_AddFileA(handle.i, *pathA, format.i)
Prototype.i  zfork_RemoveQueueItem(handle.i, index.l)
Prototype.i  zfork_MoveQueueItem(handle.i, fromIndex.l, toIndex.l)
Prototype.l  zfork_GetQueueCount(handle.i)
Prototype.i  zfork_GetQueueItemPathA(handle.i, index.l, *outPath, outBytes.l)
Prototype.i  zfork_OpenStream(handle.i, *data, size.l, format.i)
Prototype    zfork_Close(handle.i)
Prototype.i  zfork_Play(handle.i)
Prototype.i  zfork_PlayLoop(handle.i, count.l)
Prototype.i  zfork_Pause(handle.i)
Prototype.i  zfork_Resume(handle.i)
Prototype.i  zfork_Stop(handle.i)
Prototype.i  zfork_SeekMs(handle.i, origin.i, positionMs.l)
Prototype.i  zfork_GetPositionMs(handle.i, *positionMs)
Prototype.i  zfork_GetStreamInfo(handle.i, songIndex.i, *outInfo)
Prototype.i  zfork_GetStatus(handle.i, *outStatus)
Prototype.i  zfork_SetMasterVolume(handle.i, left.i, right.i)
Prototype.i  zfork_GetMasterVolume(handle.i, *outLeft, *outRight)
Prototype.i  zfork_SetPlayerVolume(handle.i, left.i, right.i)
Prototype.i  zfork_GetPlayerVolume(handle.i, *outLeft, *outRight)
Prototype.i  zfork_GetVUData(handle.i, *outLeft, *outRight)
Prototype.i  zfork_MixChannels(handle.i, mono.i)
Prototype.i  zfork_SetRate(handle.i, rate.f)
Prototype.i  zfork_SetPitch(handle.i, semitones.f)
Prototype.i  zfork_SetTempo(handle.i, tempo.f)
Prototype.i  zfork_EnableEqualizer(handle.i, enable.i)
Prototype.i  zfork_SetEQBandGain(handle.i, band.i, gainDb.f)
Prototype.i  zfork_GetEQBandGain(handle.i, band.i, *gainDb)
Prototype.i  zfork_ResetEqualizer(handle.i)
Prototype.i  zfork_EnableEcho(handle.i, enable.i)
Prototype.i  zfork_SetEchoParams(handle.i, *params)
Prototype.i  zfork_GetEchoParams(handle.i, *outParams)
Prototype.i  zfork_EnableVocalCut(handle.i, enable.i)
Prototype.i  zfork_EnableSideCut(handle.i, enable.i)
Prototype.i  zfork_EnableStereoEnhancer(handle.i, enable.i)
Prototype.i  zfork_SetStereoWidth(handle.i, width.f)
Prototype.i  zfork_GetStereoWidth(handle.i, *outWidth)
Prototype.i  zfork_SetBufferSize(handle.i, milliseconds.l)
Prototype.i  zfork_GetFFTData(handle.i, *leftBuf, *rightBuf, points.l)
Prototype.i  zfork_DetectBPM(handle.i, algo.i, *outBPM)
Prototype.i  zfork_DetectFileBPMA(handle.i, *pathA, algo.i, *outBPM)
Prototype.i  zfork_AnalyzeFile(handle.i, *pathW, flags.l, *outHandle)
Prototype.i  zfork_AnalyzeFileA(handle.i, *pathA, flags.l, *outHandle)
Prototype    zfork_FreeAnalysis(handle.i, analysis.i)
Prototype.i  zfork_GetAnalysisSummary(handle.i, analysis.i, *outBpm, *outLengthMs, *outFlags)
Prototype.l  zfork_CopyAnalysisWaveform(handle.i, analysis.i, *outBins, maxBins.l)
Prototype.l  zfork_CopyAnalysisPeaks(handle.i, analysis.i, *outTimesMs, *outValues, maxPeaks.l)
Prototype.i  zfork_SetPlaylistAllowDuplicates(handle.i, allow.i)
Prototype.i  zfork_GetPlaylistAllowDuplicates(handle.i)
Prototype.i  zfork_StartBroadcast(handle.i, *config)
Prototype.i  zfork_StopBroadcast(handle.i)
Prototype.i  zfork_PushBroadcastPCM(handle.i, *interleaved, frames.l, channels.i, sampleRate.i)
Prototype.i  zfork_StartBroadcastFile(handle.i, *pathW, format.i)
Prototype.i  zfork_StartBroadcastFileA(handle.i, *pathA, format.i)
Prototype.i  zfork_AttachBroadcastToCurrentPlayback(handle.i, enable.i)
Prototype.i  zfork_SetBroadcastMetadata(handle.i, *titleA, *artistA, *albumA)
Prototype.i  zfork_PushBroadcastMicPCM(handle.i, *interleaved, frames.l, channels.i, sampleRate.i)
Prototype.i  zfork_SetBroadcastMicActive(handle.i, active.i)
Prototype.i  zfork_SetBroadcastDucking(handle.i, *config)
Prototype.i  zfork_GetBroadcastDucking(handle.i, *outConfig)
Prototype.i  zfork_StartBroadcastMicCapture(handle.i, deviceId.i, sampleRate.i, channels.i)
Prototype.i  zfork_StopBroadcastMicCapture(handle.i)
Prototype.i  zfork_GetBroadcastStats(handle.i, *outStats)
Prototype.i  zfork_GetBroadcastError(handle.i)
Prototype.i  zfork_SetPCMOutputCallback(handle.i, *callback, userData.i, disableBuiltInOutput.i)
Prototype.i  zfork_SetCallback(handle.i, *callback, userData.i)
Prototype.i  zfork_EnumerateOutputDevices(handle.i)
Prototype.i  zfork_GetOutputDeviceInfo(handle.i, deviceId.i, *outInfo)
Prototype.i  zfork_SetOutputDevice(handle.i, deviceId.i)
Prototype.i  zfork_LoadID3Ex(handle.i, songIndex.i, *outInfo)
Prototype.i  zfork_LoadID3Picture(handle.i, songIndex.i, picIndex.i, *outData, *outSize, *outType)
Prototype.i  zfork_LoadFileID3ExA(handle.i, *pathA, *outInfo)
Prototype.i  zfork_LoadFileID3PictureA(handle.i, *pathA, picIndex.i, *outData, *outSize, *outType)
Prototype    zfork_FreeMemory(*ptr)
Prototype.i  zfork_ParseMusicalKey(*textA, *outInfo)
Prototype.i  zfork_KeysCompatible(*a, *b)
Prototype.i  zfork_GetError(handle.i)

Global zfork_Create.zfork_Create
Global zfork_CreateWithConfig.zfork_CreateWithConfig
Global zfork_Destroy.zfork_Destroy
Global zfork_OpenFile.zfork_OpenFile
Global zfork_OpenFileA.zfork_OpenFileA
Global zfork_LoadPlaylistFile.zfork_LoadPlaylistFile
Global zfork_LoadPlaylistFileA.zfork_LoadPlaylistFileA
Global zfork_AddFile.zfork_AddFile
Global zfork_AddFileA.zfork_AddFileA
Global zfork_RemoveQueueItem.zfork_RemoveQueueItem
Global zfork_MoveQueueItem.zfork_MoveQueueItem
Global zfork_GetQueueCount.zfork_GetQueueCount
Global zfork_GetQueueItemPathA.zfork_GetQueueItemPathA
Global zfork_OpenStream.zfork_OpenStream
Global zfork_Close.zfork_Close
Global zfork_Play.zfork_Play
Global zfork_PlayLoop.zfork_PlayLoop
Global zfork_Pause.zfork_Pause
Global zfork_Resume.zfork_Resume
Global zfork_Stop.zfork_Stop
Global zfork_SeekMs.zfork_SeekMs
Global zfork_GetPositionMs.zfork_GetPositionMs
Global zfork_GetStreamInfo.zfork_GetStreamInfo
Global zfork_GetStatus.zfork_GetStatus
Global zfork_SetMasterVolume.zfork_SetMasterVolume
Global zfork_GetMasterVolume.zfork_GetMasterVolume
Global zfork_SetPlayerVolume.zfork_SetPlayerVolume
Global zfork_GetPlayerVolume.zfork_GetPlayerVolume
Global zfork_GetVUData.zfork_GetVUData
Global zfork_MixChannels.zfork_MixChannels
Global zfork_SetRate.zfork_SetRate
Global zfork_SetPitch.zfork_SetPitch
Global zfork_SetTempo.zfork_SetTempo
Global zfork_EnableEqualizer.zfork_EnableEqualizer
Global zfork_SetEQBandGain.zfork_SetEQBandGain
Global zfork_GetEQBandGain.zfork_GetEQBandGain
Global zfork_ResetEqualizer.zfork_ResetEqualizer
Global zfork_EnableEcho.zfork_EnableEcho
Global zfork_SetEchoParams.zfork_SetEchoParams
Global zfork_GetEchoParams.zfork_GetEchoParams
Global zfork_EnableVocalCut.zfork_EnableVocalCut
Global zfork_EnableSideCut.zfork_EnableSideCut
Global zfork_EnableStereoEnhancer.zfork_EnableStereoEnhancer
Global zfork_SetStereoWidth.zfork_SetStereoWidth
Global zfork_GetStereoWidth.zfork_GetStereoWidth
Global zfork_SetBufferSize.zfork_SetBufferSize
Global zfork_GetFFTData.zfork_GetFFTData
Global zfork_DetectBPM.zfork_DetectBPM
Global zfork_DetectFileBPMA.zfork_DetectFileBPMA
Global zfork_AnalyzeFile.zfork_AnalyzeFile
Global zfork_AnalyzeFileA.zfork_AnalyzeFileA
Global zfork_FreeAnalysis.zfork_FreeAnalysis
Global zfork_GetAnalysisSummary.zfork_GetAnalysisSummary
Global zfork_CopyAnalysisWaveform.zfork_CopyAnalysisWaveform
Global zfork_CopyAnalysisPeaks.zfork_CopyAnalysisPeaks
Global zfork_SetPlaylistAllowDuplicates.zfork_SetPlaylistAllowDuplicates
Global zfork_GetPlaylistAllowDuplicates.zfork_GetPlaylistAllowDuplicates
Global zfork_StartBroadcast.zfork_StartBroadcast
Global zfork_StopBroadcast.zfork_StopBroadcast
Global zfork_PushBroadcastPCM.zfork_PushBroadcastPCM
Global zfork_StartBroadcastFile.zfork_StartBroadcastFile
Global zfork_StartBroadcastFileA.zfork_StartBroadcastFileA
Global zfork_AttachBroadcastToCurrentPlayback.zfork_AttachBroadcastToCurrentPlayback
Global zfork_SetBroadcastMetadata.zfork_SetBroadcastMetadata
Global zfork_PushBroadcastMicPCM.zfork_PushBroadcastMicPCM
Global zfork_SetBroadcastMicActive.zfork_SetBroadcastMicActive
Global zfork_SetBroadcastDucking.zfork_SetBroadcastDucking
Global zfork_GetBroadcastDucking.zfork_GetBroadcastDucking
Global zfork_StartBroadcastMicCapture.zfork_StartBroadcastMicCapture
Global zfork_StopBroadcastMicCapture.zfork_StopBroadcastMicCapture
Global zfork_GetBroadcastStats.zfork_GetBroadcastStats
Global zfork_GetBroadcastError.zfork_GetBroadcastError
Global zfork_SetPCMOutputCallback.zfork_SetPCMOutputCallback
Global zfork_SetCallback.zfork_SetCallback
Global zfork_EnumerateOutputDevices.zfork_EnumerateOutputDevices
Global zfork_GetOutputDeviceInfo.zfork_GetOutputDeviceInfo
Global zfork_SetOutputDevice.zfork_SetOutputDevice
Global zfork_LoadID3Ex.zfork_LoadID3Ex
Global zfork_LoadID3Picture.zfork_LoadID3Picture
Global zfork_LoadFileID3ExA.zfork_LoadFileID3ExA
Global zfork_LoadFileID3PictureA.zfork_LoadFileID3PictureA
Global zfork_FreeMemory.zfork_FreeMemory
Global zfork_ParseMusicalKey.zfork_ParseMusicalKey
Global zfork_KeysCompatible.zfork_KeysCompatible
Global zfork_GetError.zfork_GetError

Global _libZFork.i = 0

Procedure.i ZForkLoad(dllPath.s = "libZFork.dll")
  _libZFork = OpenLibrary(#PB_Any, dllPath)
  If Not _libZFork : ProcedureReturn #False : EndIf
  zfork_Create = GetFunction(_libZFork, "zfork_Create")
  zfork_CreateWithConfig = GetFunction(_libZFork, "zfork_CreateWithConfig")
  zfork_Destroy = GetFunction(_libZFork, "zfork_Destroy")
  zfork_OpenFile = GetFunction(_libZFork, "zfork_OpenFile")
  zfork_OpenFileA = GetFunction(_libZFork, "zfork_OpenFileA")
  zfork_LoadPlaylistFile = GetFunction(_libZFork, "zfork_LoadPlaylistFile")
  zfork_LoadPlaylistFileA = GetFunction(_libZFork, "zfork_LoadPlaylistFileA")
  zfork_AddFile = GetFunction(_libZFork, "zfork_AddFile")
  zfork_AddFileA = GetFunction(_libZFork, "zfork_AddFileA")
  zfork_RemoveQueueItem = GetFunction(_libZFork, "zfork_RemoveQueueItem")
  zfork_MoveQueueItem = GetFunction(_libZFork, "zfork_MoveQueueItem")
  zfork_GetQueueCount = GetFunction(_libZFork, "zfork_GetQueueCount")
  zfork_GetQueueItemPathA = GetFunction(_libZFork, "zfork_GetQueueItemPathA")
  zfork_OpenStream = GetFunction(_libZFork, "zfork_OpenStream")
  zfork_Close = GetFunction(_libZFork, "zfork_Close")
  zfork_Play = GetFunction(_libZFork, "zfork_Play")
  zfork_PlayLoop = GetFunction(_libZFork, "zfork_PlayLoop")
  zfork_Pause = GetFunction(_libZFork, "zfork_Pause")
  zfork_Resume = GetFunction(_libZFork, "zfork_Resume")
  zfork_Stop = GetFunction(_libZFork, "zfork_Stop")
  zfork_SeekMs = GetFunction(_libZFork, "zfork_SeekMs")
  zfork_GetPositionMs = GetFunction(_libZFork, "zfork_GetPositionMs")
  zfork_GetStreamInfo = GetFunction(_libZFork, "zfork_GetStreamInfo")
  zfork_GetStatus = GetFunction(_libZFork, "zfork_GetStatus")
  zfork_SetMasterVolume = GetFunction(_libZFork, "zfork_SetMasterVolume")
  zfork_GetMasterVolume = GetFunction(_libZFork, "zfork_GetMasterVolume")
  zfork_SetPlayerVolume = GetFunction(_libZFork, "zfork_SetPlayerVolume")
  zfork_GetPlayerVolume = GetFunction(_libZFork, "zfork_GetPlayerVolume")
  zfork_GetVUData = GetFunction(_libZFork, "zfork_GetVUData")
  zfork_MixChannels = GetFunction(_libZFork, "zfork_MixChannels")
  zfork_SetRate = GetFunction(_libZFork, "zfork_SetRate")
  zfork_SetPitch = GetFunction(_libZFork, "zfork_SetPitch")
  zfork_SetTempo = GetFunction(_libZFork, "zfork_SetTempo")
  zfork_EnableEqualizer = GetFunction(_libZFork, "zfork_EnableEqualizer")
  zfork_SetEQBandGain = GetFunction(_libZFork, "zfork_SetEQBandGain")
  zfork_GetEQBandGain = GetFunction(_libZFork, "zfork_GetEQBandGain")
  zfork_ResetEqualizer = GetFunction(_libZFork, "zfork_ResetEqualizer")
  zfork_EnableEcho = GetFunction(_libZFork, "zfork_EnableEcho")
  zfork_SetEchoParams = GetFunction(_libZFork, "zfork_SetEchoParams")
  zfork_GetEchoParams = GetFunction(_libZFork, "zfork_GetEchoParams")
  zfork_EnableVocalCut = GetFunction(_libZFork, "zfork_EnableVocalCut")
  zfork_EnableSideCut = GetFunction(_libZFork, "zfork_EnableSideCut")
  zfork_EnableStereoEnhancer = GetFunction(_libZFork, "zfork_EnableStereoEnhancer")
  zfork_SetStereoWidth = GetFunction(_libZFork, "zfork_SetStereoWidth")
  zfork_GetStereoWidth = GetFunction(_libZFork, "zfork_GetStereoWidth")
  zfork_SetBufferSize = GetFunction(_libZFork, "zfork_SetBufferSize")
  zfork_GetFFTData = GetFunction(_libZFork, "zfork_GetFFTData")
  zfork_DetectBPM = GetFunction(_libZFork, "zfork_DetectBPM")
  zfork_DetectFileBPMA = GetFunction(_libZFork, "zfork_DetectFileBPMA")
  zfork_AnalyzeFile = GetFunction(_libZFork, "zfork_AnalyzeFile")
  zfork_AnalyzeFileA = GetFunction(_libZFork, "zfork_AnalyzeFileA")
  zfork_FreeAnalysis = GetFunction(_libZFork, "zfork_FreeAnalysis")
  zfork_GetAnalysisSummary = GetFunction(_libZFork, "zfork_GetAnalysisSummary")
  zfork_CopyAnalysisWaveform = GetFunction(_libZFork, "zfork_CopyAnalysisWaveform")
  zfork_CopyAnalysisPeaks = GetFunction(_libZFork, "zfork_CopyAnalysisPeaks")
  zfork_SetPlaylistAllowDuplicates = GetFunction(_libZFork, "zfork_SetPlaylistAllowDuplicates")
  zfork_GetPlaylistAllowDuplicates = GetFunction(_libZFork, "zfork_GetPlaylistAllowDuplicates")
  zfork_StartBroadcast = GetFunction(_libZFork, "zfork_StartBroadcast")
  zfork_StopBroadcast = GetFunction(_libZFork, "zfork_StopBroadcast")
  zfork_PushBroadcastPCM = GetFunction(_libZFork, "zfork_PushBroadcastPCM")
  zfork_StartBroadcastFile = GetFunction(_libZFork, "zfork_StartBroadcastFile")
  zfork_StartBroadcastFileA = GetFunction(_libZFork, "zfork_StartBroadcastFileA")
  zfork_AttachBroadcastToCurrentPlayback = GetFunction(_libZFork, "zfork_AttachBroadcastToCurrentPlayback")
  zfork_SetBroadcastMetadata = GetFunction(_libZFork, "zfork_SetBroadcastMetadata")
  zfork_PushBroadcastMicPCM = GetFunction(_libZFork, "zfork_PushBroadcastMicPCM")
  zfork_SetBroadcastMicActive = GetFunction(_libZFork, "zfork_SetBroadcastMicActive")
  zfork_SetBroadcastDucking = GetFunction(_libZFork, "zfork_SetBroadcastDucking")
  zfork_GetBroadcastDucking = GetFunction(_libZFork, "zfork_GetBroadcastDucking")
  zfork_StartBroadcastMicCapture = GetFunction(_libZFork, "zfork_StartBroadcastMicCapture")
  zfork_StopBroadcastMicCapture = GetFunction(_libZFork, "zfork_StopBroadcastMicCapture")
  zfork_GetBroadcastStats = GetFunction(_libZFork, "zfork_GetBroadcastStats")
  zfork_GetBroadcastError = GetFunction(_libZFork, "zfork_GetBroadcastError")
  zfork_SetPCMOutputCallback = GetFunction(_libZFork, "zfork_SetPCMOutputCallback")
  zfork_SetCallback = GetFunction(_libZFork, "zfork_SetCallback")
  zfork_EnumerateOutputDevices = GetFunction(_libZFork, "zfork_EnumerateOutputDevices")
  zfork_GetOutputDeviceInfo = GetFunction(_libZFork, "zfork_GetOutputDeviceInfo")
  zfork_SetOutputDevice = GetFunction(_libZFork, "zfork_SetOutputDevice")
  zfork_LoadID3Ex = GetFunction(_libZFork, "zfork_LoadID3Ex")
  zfork_LoadID3Picture = GetFunction(_libZFork, "zfork_LoadID3Picture")
  zfork_LoadFileID3ExA = GetFunction(_libZFork, "zfork_LoadFileID3ExA")
  zfork_LoadFileID3PictureA = GetFunction(_libZFork, "zfork_LoadFileID3PictureA")
  zfork_FreeMemory = GetFunction(_libZFork, "zfork_FreeMemory")
  zfork_ParseMusicalKey = GetFunction(_libZFork, "zfork_ParseMusicalKey")
  zfork_KeysCompatible = GetFunction(_libZFork, "zfork_KeysCompatible")
  zfork_GetError = GetFunction(_libZFork, "zfork_GetError")
  ProcedureReturn #True
EndProcedure

Procedure ZForkUnload()
  If _libZFork : CloseLibrary(_libZFork) : _libZFork = 0 : EndIf
EndProcedure

Procedure.s ZForkGetError(handle.i)
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
  *cfg\structSize = SizeOf(ZFORK_Config)
  *cfg\waveOutDeviceId = -1
  *cfg\masterVolumeLeft = -1 : *cfg\masterVolumeRight = -1
  *cfg\playerVolumeLeft = -1 : *cfg\playerVolumeRight = -1
  *cfg\monoMix = -1
  *cfg\outputBackend = #ZFORK_OUTPUT_AUTO
EndProcedure
