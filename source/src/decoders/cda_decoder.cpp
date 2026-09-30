#include "cda_decoder.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cwctype>

#ifdef _WIN32
#  include <windows.h>
#  include <winioctl.h>
#  include <ntddcdrm.h>
#endif

namespace libZFork {

namespace {
constexpr uint32_t kCddaBytesPerSector = 2352u;
constexpr uint32_t kCddaFramesPerSector = 588u;
constexpr uint32_t kCookedSectorSize = 2048u;

#ifdef _WIN32
static uint32_t MsfToLba(UCHAR m, UCHAR s, UCHAR f) {
    const int lba = (static_cast<int>(m) * 60 + static_cast<int>(s)) * 75 + static_cast<int>(f) - 150;
    return lba > 0 ? static_cast<uint32_t>(lba) : 0u;
}
#endif
}

CdaDecoder::CdaDecoder() = default;
CdaDecoder::~CdaDecoder() { CloseStream(); }

bool CdaDecoder::OpenMemory(const uint8_t* data, uint32_t size) {
    (void)data;
    (void)size;
    SetError("CD audio decoder does not support memory input");
    return false;
}

void CdaDecoder::CloseStream() {
#ifdef _WIN32
    if (hCdrom_) {
        CloseHandle(reinterpret_cast<HANDLE>(hCdrom_));
        hCdrom_ = nullptr;
    }
#else
    hCdrom_ = nullptr;
#endif
    trackStartLba_ = 0;
    trackEndLba_ = 0;
    framePos_ = 0;
    totalFrames_ = 0;
}

#ifdef _WIN32
bool CdaDecoder::ParseTrackPath(const wchar_t* path, wchar_t& outDrive, int& outTrack) const {
    if (!path || !path[0]) return false;
    outTrack = -1;
    outDrive = 0;
    std::wstring p(path);

    // cdda://D:1
    if (p.rfind(L"cdda://", 0) == 0 && p.size() >= 9) {
        outDrive = static_cast<wchar_t>(std::towupper(p[7]));
        const size_t sep = p.find(L':', 8);
        if (sep != std::wstring::npos && (sep + 1) < p.size()) {
            try {
                outTrack = std::max(1, std::stoi(p.substr(sep + 1)));
            } catch (...) {
                outTrack = -1;
            }
        }
        return (outDrive >= L'A' && outDrive <= L'Z' && outTrack > 0);
    }

    // D:\Track01.cda
    if (p.size() < 3 || p[1] != L':') return false;
    outDrive = static_cast<wchar_t>(std::towupper(p[0]));
    const size_t slash = p.find_last_of(L"\\/");
    const std::wstring file = (slash == std::wstring::npos) ? p : p.substr(slash + 1);
    const size_t dot = file.find_last_of(L'.');
    const std::wstring stem = (dot == std::wstring::npos) ? file : file.substr(0, dot);
    for (size_t i = stem.size(); i-- > 0;) {
        if (!std::iswdigit(stem[i])) {
            if (i + 1 < stem.size()) {
                try {
                    outTrack = std::max(1, std::stoi(stem.substr(i + 1)));
                } catch (...) {
                    outTrack = -1;
                }
            }
            break;
        }
        if (i == 0) {
            try {
                outTrack = std::max(1, std::stoi(stem));
            } catch (...) {
                outTrack = -1;
            }
        }
    }
    return (outDrive >= L'A' && outDrive <= L'Z' && outTrack > 0);
}

bool CdaDecoder::OpenTrack(wchar_t driveLetter, int trackNumber) {
    const std::wstring devPath = L"\\\\.\\" + std::wstring(1, driveLetter) + L":";
    HANDLE h = CreateFileW(devPath.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        SetError("Cannot open CD device");
        return false;
    }

    CDROM_TOC toc{};
    DWORD got = 0;
    if (!DeviceIoControl(h, IOCTL_CDROM_READ_TOC, nullptr, 0, &toc, sizeof(toc), &got, nullptr)) {
        CloseHandle(h);
        SetError("Cannot read CD TOC");
        return false;
    }

    const int first = static_cast<int>(toc.FirstTrack);
    const int last = static_cast<int>(toc.LastTrack);
    if (trackNumber < first || trackNumber > last) {
        CloseHandle(h);
        SetError("Requested CD track out of range");
        return false;
    }

    const int idx = trackNumber - first;
    const auto& td = toc.TrackData[idx];
    if ((td.Control & 0x04) != 0) {
        CloseHandle(h);
        SetError("Requested track is data, not audio");
        return false;
    }

    const auto& nextTd = toc.TrackData[idx + 1];
    const uint32_t startLba = MsfToLba(td.Address[1], td.Address[2], td.Address[3]);
    const uint32_t endLba = MsfToLba(nextTd.Address[1], nextTd.Address[2], nextTd.Address[3]);
    if (endLba <= startLba) {
        CloseHandle(h);
        SetError("Invalid CD track range");
        return false;
    }

    hCdrom_ = reinterpret_cast<void*>(h);
    trackStartLba_ = startLba;
    trackEndLba_ = endLba;
    framePos_ = 0;
    totalFrames_ = static_cast<uint64_t>(endLba - startLba) * kCddaFramesPerSector;
    return true;
}

bool CdaDecoder::ReadSectors(uint32_t lba, uint32_t sectorCount, std::vector<uint8_t>& outRaw) const {
    if (!hCdrom_ || sectorCount == 0) return false;
    outRaw.resize(static_cast<size_t>(sectorCount) * kCddaBytesPerSector);

    RAW_READ_INFO ri{};
    ri.TrackMode = CDDA;
    ri.SectorCount = sectorCount;
    ri.DiskOffset.QuadPart = static_cast<LONGLONG>(lba) * static_cast<LONGLONG>(kCookedSectorSize);

    DWORD got = 0;
    const BOOL ok = DeviceIoControl(reinterpret_cast<HANDLE>(hCdrom_),
                                    IOCTL_CDROM_RAW_READ,
                                    &ri, sizeof(ri),
                                    outRaw.data(), static_cast<DWORD>(outRaw.size()),
                                    &got, nullptr);
    if (!ok || got < kCddaBytesPerSector) return false;
    outRaw.resize(got - (got % 4u));
    return true;
}
#endif

bool CdaDecoder::OpenFile(const wchar_t* path) {
    CloseStream();
#ifndef _WIN32
    (void)path;
    SetError("CD audio decoder is only supported on Windows");
    return false;
#else
    wchar_t drive = 0;
    int track = -1;
    if (!ParseTrackPath(path, drive, track)) {
        SetError("Invalid CD track path (expected D:\\Track01.cda or cdda://D:1)");
        return false;
    }
    return OpenTrack(drive, track);
#endif
}

uint32_t CdaDecoder::DecodeFloat(float* outPCM, uint32_t maxFrames) {
#ifndef _WIN32
    (void)outPCM;
    (void)maxFrames;
    return 0;
#else
    if (!hCdrom_ || !outPCM || maxFrames == 0 || framePos_ >= totalFrames_) return 0;

    const uint64_t remainingFrames = totalFrames_ - framePos_;
    const uint32_t wantFrames = static_cast<uint32_t>(std::min<uint64_t>(remainingFrames, maxFrames));
    const uint32_t wantSectors = (wantFrames + kCddaFramesPerSector - 1u) / kCddaFramesPerSector;
    const uint32_t currentLba = trackStartLba_ + static_cast<uint32_t>(framePos_ / kCddaFramesPerSector);

    std::vector<uint8_t> raw;
    if (!ReadSectors(currentLba, wantSectors, raw)) {
        SetError("CD raw read failed");
        return 0;
    }
    const uint32_t framesRead = std::min<uint32_t>(wantFrames,
        static_cast<uint32_t>(raw.size() / 4u)); // 2ch * int16
    const int16_t* in = reinterpret_cast<const int16_t*>(raw.data());
    for (uint32_t i = 0; i < framesRead * 2u; ++i) {
        outPCM[i] = static_cast<float>(in[i]) / 32768.0f;
    }
    framePos_ += framesRead;
    return framesRead;
#endif
}

bool CdaDecoder::Seek(SeekOrigin origin, const StreamTime& time) {
    int64_t target = 0;
    switch (origin) {
        case SeekOrigin::Begin:
            target = static_cast<int64_t>(TimeToSamplePos(time, SampleRate()));
            break;
        case SeekOrigin::Current:
            target = static_cast<int64_t>(framePos_) + static_cast<int64_t>(time.samples);
            break;
        case SeekOrigin::End:
            target = static_cast<int64_t>(totalFrames_);
            break;
    }
    target = std::clamp<int64_t>(target, 0, static_cast<int64_t>(totalFrames_));
    framePos_ = static_cast<uint64_t>(target);
    return true;
}

StreamTime CdaDecoder::GetPosition() const {
    return SamplesToTime(framePos_, SampleRate());
}

StreamTime CdaDecoder::GetLength() const {
    return SamplesToTime(totalFrames_, SampleRate());
}

} // namespace libZFork
