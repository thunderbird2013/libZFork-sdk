#ifdef _WIN32
#include "waveout_output.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <format>

#pragma comment(lib, "winmm.lib")

namespace libZFork {

WaveOutOutput::WaveOutOutput() {
    hEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

WaveOutOutput::~WaveOutOutput() {
    Stop();
    Close();
    if (hEvent_) {
        CloseHandle(hEvent_);
        hEvent_ = nullptr;
    }
}

bool WaveOutOutput::Init(int sampleRate, int channels, int bitsPerSample, uint32_t /*bufferMs*/) {
    Close();

    format_.wFormatTag      = WAVE_FORMAT_PCM;
    format_.nChannels       = static_cast<WORD>(channels);
    format_.nSamplesPerSec  = static_cast<DWORD>(sampleRate);
    format_.wBitsPerSample  = static_cast<WORD>(bitsPerSample);
    format_.nBlockAlign     = static_cast<WORD>(channels * bitsPerSample / 8);
    format_.nAvgBytesPerSec = format_.nSamplesPerSec * format_.nBlockAlign;
    format_.cbSize          = 0;

    freeBlocks_.store(kBufferCount, std::memory_order_relaxed);
    stopped_.store(false, std::memory_order_release);
    for (auto& blk : blocks_) {
        blk.hdr = {};
        blk.available.store(true, std::memory_order_relaxed);
        blk.prepared = false;
    }

    MMRESULT res = waveOutOpen(&hWaveOut_,
                               static_cast<UINT>(deviceId_),
                               &format_,
                               reinterpret_cast<DWORD_PTR>(StaticCallback),
                               reinterpret_cast<DWORD_PTR>(this),
                               CALLBACK_FUNCTION);
    if (res != MMSYSERR_NOERROR) {
        hWaveOut_ = nullptr;
        return false;
    }

    // Prepare all headers once. Reusing already-prepared headers is cheaper
    // and avoids per-block prepare/unprepare churn that can cause glitches.
    for (auto& blk : blocks_) {
        blk.hdr.lpData         = reinterpret_cast<LPSTR>(blk.buf.data());
        blk.hdr.dwBufferLength = kBlockBytes;
        blk.hdr.dwFlags        = 0;
        if (waveOutPrepareHeader(hWaveOut_, &blk.hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            // Roll back already prepared headers and fail init.
            for (auto& prev : blocks_) {
                if (prev.prepared) {
                    waveOutUnprepareHeader(hWaveOut_, &prev.hdr, sizeof(WAVEHDR));
                    prev.prepared = false;
                }
            }
            waveOutClose(hWaveOut_);
            hWaveOut_ = nullptr;
            return false;
        }
        blk.prepared = true;
    }

    return true;
}

bool WaveOutOutput::Start() {
    // waveOut plays automatically once blocks are submitted via waveOutWrite.
    return hWaveOut_ != nullptr;
}

void WaveOutOutput::Pause() {
    if (hWaveOut_) waveOutPause(hWaveOut_);
}

void WaveOutOutput::Resume() {
    if (hWaveOut_) waveOutRestart(hWaveOut_);
}

void WaveOutOutput::Stop() {
    stopped_.store(true, std::memory_order_release);
    if (hEvent_) SetEvent(hEvent_); // unblock WriteBlock / Drain
    if (hWaveOut_) waveOutReset(hWaveOut_); // all pending blocks complete immediately via WOM_DONE
}

void WaveOutOutput::Close() {
    if (!hWaveOut_) return;
    waveOutReset(hWaveOut_);
    for (auto& blk : blocks_) {
        if (blk.prepared || (blk.hdr.dwFlags & WHDR_PREPARED))
            waveOutUnprepareHeader(hWaveOut_, &blk.hdr, sizeof(WAVEHDR));
        blk.hdr = {};
        blk.available.store(true, std::memory_order_relaxed);
        blk.prepared = false;
    }
    waveOutClose(hWaveOut_);
    hWaveOut_ = nullptr;
    freeBlocks_.store(kBufferCount, std::memory_order_release);
}

void WaveOutOutput::Drain() {
    while (freeBlocks_.load(std::memory_order_acquire) < kBufferCount
           && hWaveOut_
           && !stopped_.load(std::memory_order_acquire))
    {
        WaitForSingleObject(hEvent_, 50);
    }
}

bool WaveOutOutput::WriteBlock(const int16_t* pcm, uint32_t frames) {
    const auto* bytes   = reinterpret_cast<const uint8_t*>(pcm);
    uint32_t    total   = frames * format_.nBlockAlign;
    uint32_t    offset  = 0;

    while (offset < total) {
        if (stopped_.load(std::memory_order_acquire)) return false;

        if (freeBlocks_.load(std::memory_order_acquire) == 0) {
            WaitForSingleObject(hEvent_, 5);
            continue;
        }

        bool wrote = false;
        for (auto& blk : blocks_) {
            bool expected = true;
            if (blk.available.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
                uint32_t toWrite = std::min<uint32_t>(kBlockBytes, total - offset);
                std::memcpy(blk.buf.data(), bytes + offset, toWrite);
                blk.hdr.dwBufferLength = toWrite;
                blk.hdr.dwFlags        &= ~WHDR_DONE;

                if (waveOutWrite(hWaveOut_, &blk.hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
                    blk.available.store(true, std::memory_order_release);
                    SetEvent(hEvent_);
                    return false;
                }

                freeBlocks_.fetch_sub(1, std::memory_order_acq_rel);
                offset += toWrite;
                wrote = true;
                break;
            }
        }
        if (!wrote) WaitForSingleObject(hEvent_, 10);
    }
    return true;
}

void WaveOutOutput::SetMasterVolume(int left, int right) {
    if (!hWaveOut_) return;
    auto toWVol = [](int p) -> WORD {
        return static_cast<WORD>(std::clamp(p, 0, 100) * 0xFFFF / 100);
    };
    DWORD vol = (static_cast<DWORD>(toWVol(right)) << 16) | toWVol(left);
    waveOutSetVolume(hWaveOut_, vol);
}

int WaveOutOutput::EnumerateDevices() const {
    return static_cast<int>(waveOutGetNumDevs());
}

bool WaveOutOutput::GetDeviceInfo(int id, WaveDeviceInfo& out) const {
    WAVEOUTCAPSW caps{};
    if (waveOutGetDevCapsW(static_cast<UINT>(id), &caps, sizeof(caps)) != MMSYSERR_NOERROR)
        return false;
    out.id       = id;
    out.channels = caps.wChannels;
    out.formats  = caps.dwFormats;
    WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, out.name, 64, nullptr, nullptr);
    return true;
}

bool WaveOutOutput::SetDevice(int id) {
    deviceId_ = id;
    return true;
}

bool WaveOutOutput::SetOutputFile(const wchar_t* path) {
    outputFile_ = path ? path : L"";
    return true;
}

void CALLBACK WaveOutOutput::StaticCallback(HWAVEOUT hwo, UINT msg,
                                            DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR) {
    if (msg != WOM_DONE) return;
    auto* self = reinterpret_cast<WaveOutOutput*>(inst);
    auto* hdr  = reinterpret_cast<WAVEHDR*>(p1);
    (void)hwo;

    // Never call waveOut* APIs from this callback.
    // We only mark the completed block as available and wake the writer thread.
    for (auto& blk : self->blocks_) {
        if (&blk.hdr == hdr) {
            bool expected = false;
            if (blk.available.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                self->freeBlocks_.fetch_add(1, std::memory_order_acq_rel);
            }
            break;
        }
    }
    SetEvent(self->hEvent_);
}

} // namespace libZFork

#endif // _WIN32
