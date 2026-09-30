#include "wav_encoder.h"
#include <cstring>
#include <algorithm>
#include <vector>
#include <cstdint>

namespace libZFork {

static void WriteU16(HANDLE h, uint16_t v) { DWORD w; WriteFile(h, &v, 2, &w, nullptr); }
static void WriteU32(HANDLE h, uint32_t v) { DWORD w; WriteFile(h, &v, 4, &w, nullptr); }

bool WavEncoder::Open(const wchar_t* path, int sampleRate, int channels) {
    Close();
    sampleRate_ = sampleRate;
    channels_   = channels;
    hFile_ = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile_ == INVALID_HANDLE_VALUE) { SetError("Cannot create WAV file"); return false; }

    // Write placeholder header (44 bytes)
    DWORD w;
    WriteFile(hFile_, "RIFF", 4, &w, nullptr);
    WriteU32(hFile_, 0);                       // total size - placeholder
    WriteFile(hFile_, "WAVE", 4, &w, nullptr);
    WriteFile(hFile_, "fmt ", 4, &w, nullptr);
    WriteU32(hFile_, 16);                      // chunk size
    WriteU16(hFile_, 1);                       // PCM
    WriteU16(hFile_, static_cast<uint16_t>(channels));
    WriteU32(hFile_, static_cast<uint32_t>(sampleRate));
    WriteU32(hFile_, static_cast<uint32_t>(sampleRate * channels * 2)); // avgBytesPerSec
    WriteU16(hFile_, static_cast<uint16_t>(channels * 2));              // blockAlign
    WriteU16(hFile_, 16);                      // bits per sample
    WriteFile(hFile_, "data", 4, &w, nullptr);
    WriteU32(hFile_, 0);                       // data size - placeholder
    dataBytes_ = 0;
    return true;
}

bool WavEncoder::Write(std::span<const float> pcm) {
    if (hFile_ == INVALID_HANDLE_VALUE) return false;
    std::vector<int16_t> out(pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) {
        float s = std::clamp(pcm[i], -1.0f, 1.0f);
        out[i]  = static_cast<int16_t>(s * 32767.0f);
    }
    DWORD written = 0;
    ::WriteFile(hFile_, static_cast<LPCVOID>(out.data()), static_cast<DWORD>(out.size() * 2), &written, nullptr);
    dataBytes_ += written;
    return written > 0;
}

void WavEncoder::Close() {
    if (hFile_ == INVALID_HANDLE_VALUE) return;
    // Patch RIFF size and data size
    LARGE_INTEGER li{}; li.QuadPart = 4;
    SetFilePointerEx(hFile_, li, nullptr, FILE_BEGIN);
    uint32_t riffSize = 36 + dataBytes_;
    DWORD w;
    WriteFile(hFile_, &riffSize, 4, &w, nullptr);
    li.QuadPart = 40;
    SetFilePointerEx(hFile_, li, nullptr, FILE_BEGIN);
    WriteFile(hFile_, &dataBytes_, 4, &w, nullptr);
    CloseHandle(hFile_);
    hFile_     = INVALID_HANDLE_VALUE;
    dataBytes_ = 0;
}

} // namespace libZFork
