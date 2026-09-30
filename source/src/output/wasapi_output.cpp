#ifdef _WIN32
#include "wasapi_output.h"

#include <initguid.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmreg.h>
#include <avrt.h>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

using Microsoft::WRL::ComPtr;

namespace libZFork {

WasapiOutput::WasapiOutput() {
    hEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

WasapiOutput::~WasapiOutput() {
    Stop();
    Close();
    if (hEvent_) {
        CloseHandle(hEvent_);
        hEvent_ = nullptr;
    }
}

bool WasapiOutput::InitializeClient(int sampleRate, int channels, int bitsPerSample) {
    WAVEFORMATEX wfx{};
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = static_cast<WORD>(channels);
    wfx.nSamplesPerSec  = static_cast<DWORD>(sampleRate);
    wfx.wBitsPerSample  = static_cast<WORD>(bitsPerSample);
    wfx.nBlockAlign     = static_cast<WORD>(channels * bitsPerSample / 8);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize          = 0;

    // Exclusive mode requires an exact format match — no "closest match"
    // negotiation like shared mode allows.
    if (client_->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &wfx, nullptr) != S_OK)
        return false;

    REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
    if (FAILED(client_->GetDevicePeriod(&defaultPeriod, &minPeriod)))
        return false;

    HRESULT hr = client_->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,
                                     AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                     minPeriod, minPeriod, &wfx, nullptr);

    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
        // Recommended MS retry pattern: query the aligned buffer size, then
        // recreate the client with a period computed from it.
        UINT32 alignedFrames = 0;
        if (FAILED(client_->GetBufferSize(&alignedFrames))) return false;

        REFERENCE_TIME alignedPeriod = static_cast<REFERENCE_TIME>(
            10000.0 * 1000 * alignedFrames / wfx.nSamplesPerSec + 0.5);

        client_.Reset();
        if (FAILED(device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                     nullptr, &client_)))
            return false;

        hr = client_->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,
                                 AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 alignedPeriod, alignedPeriod, &wfx, nullptr);
    }
    if (FAILED(hr)) return false;

    UINT32 bufferFrames = 0;
    if (FAILED(client_->GetBufferSize(&bufferFrames))) return false;

    if (FAILED(client_->SetEventHandle(hEvent_))) return false;
    if (FAILED(client_->GetService(__uuidof(IAudioRenderClient), &renderClient_)))
        return false;

    blockAlign_   = wfx.nBlockAlign;
    bufferFrames_ = bufferFrames;
    return true;
}

bool WasapiOutput::Init(int sampleRate, int channels, int bitsPerSample, uint32_t /*bufferMs*/) {
    Close();

    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    comInitialized_ = SUCCEEDED(hrCo); // S_OK or S_FALSE both mean "usable"
    if (FAILED(hrCo) && hrCo != RPC_E_CHANGED_MODE) return false;

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), &enumerator_)))
        return false;

    HRESULT hrDev;
    if (selectedDeviceId_ < 0) {
        hrDev = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
    } else {
        ComPtr<IMMDeviceCollection> collection;
        hrDev = enumerator_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
        if (SUCCEEDED(hrDev)) {
            UINT count = 0;
            collection->GetCount(&count);
            if (static_cast<UINT>(selectedDeviceId_) < count)
                hrDev = collection->Item(static_cast<UINT>(selectedDeviceId_), &device_);
            else
                hrDev = E_INVALIDARG;
        }
    }
    if (FAILED(hrDev)) return false;

    if (FAILED(device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client_)))
        return false;

    if (!InitializeClient(sampleRate, channels, bitsPerSample)) {
        client_.Reset();
        renderClient_.Reset();
        return false;
    }

    // Ring buffer: several device periods of headroom so WriteBlock() (the
    // engine thread, pushing kMixFrames at a time) and the WASAPI render
    // thread (pulling bufferFrames_ at a time) can run at different,
    // independent cadences without stalling each other.
    const size_t ringFrames = static_cast<size_t>(bufferFrames_) * 16;
    ring_.assign(std::max<size_t>(ringFrames, 1) * blockAlign_, 0);
    ringHead_ = ringTail_ = ringUsed_ = 0;

    stopped_.store(true, std::memory_order_release);
    return true;
}

bool WasapiOutput::Start() {
    if (!client_ || !renderClient_) return false;

    // Pre-fill one full period of silence — recommended before Start() in
    // exclusive event-driven mode so the very first event has data ready.
    BYTE* data = nullptr;
    if (SUCCEEDED(renderClient_->GetBuffer(bufferFrames_, &data))) {
        std::memset(data, 0, static_cast<size_t>(bufferFrames_) * blockAlign_);
        renderClient_->ReleaseBuffer(bufferFrames_, 0);
    }

    if (FAILED(client_->Start())) return false;

    stopped_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    renderThread_ = std::thread([this] { RenderThreadLoop(); });
    return true;
}

void WasapiOutput::Pause() {
    if (client_) client_->Stop();
}

void WasapiOutput::Resume() {
    if (client_) client_->Start();
}

void WasapiOutput::Stop() {
    stopped_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(ringMutex_);
        ringUsed_ = 0; // discard queued audio so Drain()/WriteBlock() waiters unblock
    }
    ringNotFull_.notify_all();
    ringNotEmpty_.notify_all();
    if (hEvent_) SetEvent(hEvent_); // wake the render thread's wait

    running_.store(false, std::memory_order_release);
    if (renderThread_.joinable()) renderThread_.join();

    if (client_) client_->Stop();
}

void WasapiOutput::Close() {
    renderClient_.Reset();
    client_.Reset();
    device_.Reset();
    enumerator_.Reset();

    if (comInitialized_) {
        CoUninitialize();
        comInitialized_ = false;
    }
}

void WasapiOutput::Drain() {
    std::unique_lock<std::mutex> lock(ringMutex_);
    ringNotEmpty_.wait(lock, [this] {
        return ringUsed_ == 0 || stopped_.load(std::memory_order_acquire);
    });
}

bool WasapiOutput::WriteBlock(const int16_t* pcm, uint32_t frames) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(pcm);
    size_t total  = static_cast<size_t>(frames) * blockAlign_;
    size_t offset = 0;

    std::unique_lock<std::mutex> lock(ringMutex_);
    while (offset < total) {
        if (stopped_.load(std::memory_order_acquire)) return false;

        ringNotFull_.wait(lock, [this] {
            return ringUsed_ < ring_.size() || stopped_.load(std::memory_order_acquire);
        });
        if (stopped_.load(std::memory_order_acquire)) return false;

        const size_t freeBytes  = ring_.size() - ringUsed_;
        const size_t chunk      = std::min(total - offset, freeBytes);
        const size_t firstPart  = std::min(chunk, ring_.size() - ringTail_);

        std::memcpy(ring_.data() + ringTail_, bytes + offset, firstPart);
        if (chunk > firstPart)
            std::memcpy(ring_.data(), bytes + offset + firstPart, chunk - firstPart);

        ringTail_  = (ringTail_ + chunk) % ring_.size();
        ringUsed_ += chunk;
        offset    += chunk;

        ringNotEmpty_.notify_all();
    }
    return true;
}

void WasapiOutput::RenderThreadLoop() {
    // Exclusive mode runs at the device's minimum period (often just a few
    // ms). At normal thread priority this render thread competes with the
    // GUI/decode threads for CPU time, and any scheduling delay longer than
    // one period causes an audible underrun (heard as a brief stutter).
    // Register with MMCSS ("Pro Audio") so the OS scheduler gives it
    // near-real-time priority, the same mechanism DAWs/pro audio apps use.
    DWORD mmcssTaskIndex = 0;
    HANDLE mmcssHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmcssTaskIndex);
    if (!mmcssHandle) {
        // Fallback if MMCSS is unavailable: at least ask for a higher OS
        // thread priority so this loop isn't starved by normal-priority work.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    }

    while (running_.load(std::memory_order_acquire)) {
        DWORD wait = WaitForSingleObject(hEvent_, 2000);
        if (wait != WAIT_OBJECT_0) continue;
        if (!running_.load(std::memory_order_acquire)) break;

        BYTE* data = nullptr;
        if (FAILED(renderClient_->GetBuffer(bufferFrames_, &data))) continue;

        const size_t wanted = static_cast<size_t>(bufferFrames_) * blockAlign_;
        size_t       got    = 0;
        {
            std::lock_guard<std::mutex> lock(ringMutex_);
            got = std::min(wanted, ringUsed_);
            const size_t firstPart = std::min(got, ring_.size() - ringHead_);
            std::memcpy(data, ring_.data() + ringHead_, firstPart);
            if (got > firstPart)
                std::memcpy(data + firstPart, ring_.data(), got - firstPart);

            ringHead_  = (ringHead_ + got) % ring_.size();
            ringUsed_ -= got;
        }
        ringNotFull_.notify_all();

        if (got < wanted) {
            // Underrun: fill the remainder with silence rather than stale
            // data. audible as a brief dip, not a repeated-buffer glitch.
            std::memset(data + got, 0, wanted - got);
        }

        renderClient_->ReleaseBuffer(bufferFrames_, 0);
    }

    if (mmcssHandle) AvRevertMmThreadCharacteristics(mmcssHandle);
}

void WasapiOutput::SetMasterVolume(int, int) {
    // Exclusive-mode streams bypass the session mixer, so there's no simple
    // per-session volume control here (that's ISimpleAudioVolume, which only
    // applies to shared-mode sessions). Hardware volume would need
    // IAudioEndpointVolume on the device — not wired up; voice/group volume
    // in the mixer already covers the common use case.
}

int WasapiOutput::EnumerateDevices() const {
    if (!EnsureEnumerator()) return 0;
    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection)))
        return 0;
    UINT count = 0;
    collection->GetCount(&count);
    return static_cast<int>(count);
}

bool WasapiOutput::GetDeviceInfo(int id, WaveDeviceInfo& out) const {
    if (!EnsureEnumerator()) return false;
    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection)))
        return false;

    ComPtr<IMMDevice> dev;
    if (FAILED(collection->Item(static_cast<UINT>(id), &dev))) return false;

    out.id       = static_cast<uint32_t>(id);
    out.channels = 2;
    out.formats  = 0xFFFFFFFFu;
    std::snprintf(out.name, sizeof(out.name), "WASAPI device %d", id);

    ComPtr<IPropertyStore> store;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &store))) {
        PROPVARIANT nameVar;
        PropVariantInit(&nameVar);
        if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &nameVar)) && nameVar.pwszVal) {
            WideCharToMultiByte(CP_UTF8, 0, nameVar.pwszVal, -1,
                               out.name, sizeof(out.name), nullptr, nullptr);
        }
        PropVariantClear(&nameVar);
    }
    return true;
}

bool WasapiOutput::EnsureEnumerator() const {
    if (enumerator_) return true;
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)hrCo; // S_OK / S_FALSE / RPC_E_CHANGED_MODE — alle ok zum Enumerieren
    return SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator), &enumerator_));
}

bool WasapiOutput::SetDevice(int id) {
    selectedDeviceId_ = id;
    return true;
}

bool WasapiOutput::SetOutputFile(const wchar_t*) {
    return false; // not supported by this backend
}

} // namespace libZFork

#endif // _WIN32
