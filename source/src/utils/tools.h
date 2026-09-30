#pragma once
#include <string>
#include <string_view>
#include <span>
#include <cstdint>

namespace libZFork {

// Narrow/wide string conversion
std::wstring Utf8ToWide(std::string_view utf8);
std::string  WideToUtf8(std::wstring_view wide);

// PCM conversion utilities
void PCM16ToFloat(std::span<const int16_t> src, std::span<float> dst);
void FloatToPCM16(std::span<const float>  src, std::span<int16_t> dst);
void PCM24ToFloat(std::span<const uint8_t> src, std::span<float> dst, int channels);
void FloatToDBFS(std::span<const float> pcm, float& peakDB);

} // namespace libZFork
