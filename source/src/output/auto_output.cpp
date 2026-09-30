#ifdef _WIN32
#include "auto_output.h"
#include <cstdio>

namespace libZFork {

bool AutoOutput::Init(int sampleRate, int channels, int bitsPerSample, uint32_t bufferMs) {
    active_ = nullptr;

    if (preference_ != AudioBackendPreference::WaveOut) {
        if (wasapi_->Init(sampleRate, channels, bitsPerSample, bufferMs)) {
            active_ = wasapi_.get();
            return true;
        }
        if (preference_ == AudioBackendPreference::Wasapi) {
            std::fprintf(stderr, "AutoOutput: WASAPI-Exclusive angefordert, aber Init "
                                 "fehlgeschlagen (kein Fallback, da explizit angefordert).\n");
            return false;
        }
    }

    if (preference_ != AudioBackendPreference::Wasapi) {
        if (waveOut_->Init(sampleRate, channels, bitsPerSample, bufferMs)) {
            active_ = waveOut_.get();
            return true;
        }
    }
    return false;
}

bool AutoOutput::Start()      { return active_ && active_->Start(); }
void AutoOutput::Pause()      { if (active_) active_->Pause(); }
void AutoOutput::Resume()     { if (active_) active_->Resume(); }
void AutoOutput::Stop()       { if (active_) active_->Stop(); }
void AutoOutput::Close()      { if (active_) active_->Close(); }
void AutoOutput::Drain()      { if (active_) active_->Drain(); }

bool AutoOutput::WriteBlock(const int16_t* pcm, uint32_t frames) {
    return active_ && active_->WriteBlock(pcm, frames);
}

void AutoOutput::SetMasterVolume(int left, int right) {
    if (active_) active_->SetMasterVolume(left, right);
}

int AutoOutput::EnumerateDevices() const {
    if (active_) return active_->EnumerateDevices();
    // Vor Init(): je nach Preference auflisten (WaveOut geht immer ohne Init).
    if (preference_ == AudioBackendPreference::Wasapi)
        return wasapi_->EnumerateDevices();
    if (preference_ == AudioBackendPreference::WaveOut)
        return waveOut_->EnumerateDevices();
    int n = wasapi_->EnumerateDevices();
    if (n > 0) return n;
    return waveOut_->EnumerateDevices();
}

bool AutoOutput::GetDeviceInfo(int id, WaveDeviceInfo& out) const {
    if (active_) return active_->GetDeviceInfo(id, out);
    if (preference_ == AudioBackendPreference::Wasapi)
        return wasapi_->GetDeviceInfo(id, out);
    if (preference_ == AudioBackendPreference::WaveOut)
        return waveOut_->GetDeviceInfo(id, out);
    if (wasapi_->GetDeviceInfo(id, out)) return true;
    return waveOut_->GetDeviceInfo(id, out);
}

bool AutoOutput::SetDevice(int id) {
    // Applies to whichever backend Init() picks next; also forward to the
    // active one in case it's called after Init() to switch devices live.
    const bool okWasapi  = wasapi_->SetDevice(id);
    const bool okWaveOut = waveOut_->SetDevice(id);
    return okWasapi || okWaveOut;
}

bool AutoOutput::SetOutputFile(const wchar_t* path) {
    return active_ && active_->SetOutputFile(path);
}

// ── Factory ───────────────────────────────────────────────────────────────────

std::unique_ptr<IAudioOutput> CreatePlatformAudioOutput(AudioBackendPreference pref) {
    auto out = std::make_unique<AutoOutput>();
    out->SetBackendPreference(pref);
    return out;
}

} // namespace libZFork

#endif // _WIN32
