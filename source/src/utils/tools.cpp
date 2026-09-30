#include "tools.h"
#include <cmath>
#include <algorithm>
#include <codecvt>
#include <locale>

namespace libZFork {

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
    return conv.from_bytes(utf8.data(), utf8.data() + utf8.size());
}

std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
    return conv.to_bytes(wide.data(), wide.data() + wide.size());
}

void PCM16ToFloat(std::span<const int16_t> src, std::span<float> dst) {
    size_t n = std::min(src.size(), dst.size());
    for (size_t i = 0; i < n; ++i) dst[i] = src[i] / 32768.f;
}

void FloatToPCM16(std::span<const float> src, std::span<int16_t> dst) {
    size_t n = std::min(src.size(), dst.size());
    for (size_t i = 0; i < n; ++i) {
        float s = std::clamp(src[i], -1.f, 1.f);
        dst[i]  = static_cast<int16_t>(s * 32767.f);
    }
}

void PCM24ToFloat(std::span<const uint8_t> src, std::span<float> dst, int /*channels*/) {
    size_t samples = src.size() / 3;
    size_t n = std::min(samples, dst.size());
    for (size_t i = 0; i < n; ++i) {
        int32_t v = src[i*3] | (src[i*3+1] << 8) | (src[i*3+2] << 16);
        if (v & 0x800000) v |= ~0xFFFFFF;
        dst[i] = v / 8388608.f;
    }
}

void FloatToDBFS(std::span<const float> pcm, float& peakDB) {
    float peak = 0.f;
    for (auto s : pcm) peak = std::max(peak, std::abs(s));
    peakDB = (peak > 0.f) ? 20.f * std::log10(peak) : -144.f;
}

} // namespace libZFork
