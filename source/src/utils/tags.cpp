#include "tags.h"
#include "musical_key.h"
#include "utils/win_compat.h"
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>
#include <codecvt>

namespace libZFork {

static std::string DecodeUtf16(const uint8_t* data, uint32_t size, bool bigEndian) {
    if (!data || size < 2) return {};
    std::u16string tmp;
    tmp.reserve(size / 2);
    for (uint32_t i = 0; i + 1 < size; i += 2) {
        uint16_t cu = bigEndian
            ? static_cast<uint16_t>((data[i] << 8) | data[i + 1])
            : static_cast<uint16_t>(data[i] | (data[i + 1] << 8));
        if (cu == 0) break;
        tmp.push_back(static_cast<char16_t>(cu));
    }
    if (tmp.empty()) return {};
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> conv;
    return conv.to_bytes(tmp);
}

static std::vector<uint8_t> LoadFileData(const wchar_t* path, uint32_t maxBytes = 1024*1024) {
    HANDLE hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, 0, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz{}; GetFileSizeEx(hf, &sz);
    uint32_t toRead = static_cast<uint32_t>(std::min<int64_t>(sz.QuadPart, maxBytes));
    std::vector<uint8_t> buf(toRead);
    DWORD read = 0;
    ::ReadFile(hf, static_cast<LPVOID>(buf.data()), toRead, &read, nullptr);
    CloseHandle(hf);
    buf.resize(read);
    return buf;
}

void ID3Tags::CopyTruncated(char* dst, size_t dstSize, std::string_view src) {
    size_t n = std::min(dstSize - 1, src.size());
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

std::string ID3Tags::DecodeFrameString(const uint8_t* data, uint32_t size) {
    if (size == 0) return {};
    uint8_t enc = data[0];
    const uint8_t* str = data + 1;
    uint32_t       len = size - 1;

    if (enc == 0) {
        // Latin-1
        return std::string(reinterpret_cast<const char*>(str), len);
    } else if (enc == 1 || enc == 2) {
        // UTF-16 with BOM (enc==1, mostly LE) or UTF-16BE (enc==2)
        if (enc == 1 && len >= 2) {
            if (str[0] == 0xFE && str[1] == 0xFF) return DecodeUtf16(str + 2, len - 2, true);
            if (str[0] == 0xFF && str[1] == 0xFE) return DecodeUtf16(str + 2, len - 2, false);
        }
        return DecodeUtf16(str, len, enc == 2);
    } else if (enc == 3) {
        // UTF-8
        return std::string(reinterpret_cast<const char*>(str), len);
    }
    return {};
}

bool ID3Tags::ParseID3v2(const uint8_t* data, uint32_t size, ID3InfoEx& out,
                         std::vector<ID3Picture>* pics) {
    if (size < 10) return false;
    if (std::memcmp(data, "ID3", 3) != 0) return false;

    // uint8_t ver = data[3];
    // uint8_t flags = data[5];
    uint32_t tagSize = ((data[6] & 0x7F) << 21) | ((data[7] & 0x7F) << 14)
                     | ((data[8] & 0x7F) << 7)  |  (data[9] & 0x7F);
    tagSize += 10;
    if (tagSize > size) tagSize = size;

    uint32_t pos = 10;
    while (pos + 10 <= tagSize) {
        char id[5]{}; std::memcpy(id, data + pos, 4);
        uint32_t frameSize = (data[pos+4] << 24) | (data[pos+5] << 16)
                           | (data[pos+6] << 8)  |  data[pos+7];
        pos += 10;
        if (frameSize == 0 || pos + frameSize > tagSize) break;

        auto val = [&]{ return DecodeFrameString(data + pos, frameSize); };

        if      (std::strcmp(id, "TIT2") == 0) CopyTruncated(out.title,    sizeof(out.title),    val());
        else if (std::strcmp(id, "TPE1") == 0) CopyTruncated(out.artist,   sizeof(out.artist),   val());
        else if (std::strcmp(id, "TALB") == 0) CopyTruncated(out.album,    sizeof(out.album),    val());
        else if (std::strcmp(id, "TDRC") == 0 || std::strcmp(id, "TYER") == 0)
            CopyTruncated(out.year, sizeof(out.year), val());
        else if (std::strcmp(id, "COMM") == 0) {
            // COMM: encoding(1) + language(3) + short content desc + NUL + text
            if (frameSize >= 5) {
                uint8_t enc = data[pos];
                const uint8_t* p = data + pos + 4;
                const uint8_t* end = data + pos + frameSize;
                if (enc == 1 || enc == 2) {
                    while (p + 1 < end && !(p[0] == 0 && p[1] == 0)) p += 2;
                    if (p + 1 < end) p += 2;
                } else {
                    while (p < end && *p) ++p;
                    if (p < end) ++p;
                }
                if (p < end) {
                    std::vector<uint8_t> tmp(1 + static_cast<size_t>(end - p));
                    tmp[0] = enc;
                    std::memcpy(tmp.data() + 1, p, static_cast<size_t>(end - p));
                    auto s = DecodeFrameString(tmp.data(), static_cast<uint32_t>(tmp.size()));
                    while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) s.pop_back();
                    if (!s.empty()) CopyTruncated(out.comment, sizeof(out.comment), s);
                }
            }
        }
        else if (std::strcmp(id, "TPE2") == 0) CopyTruncated(out.albumArtist, sizeof(out.albumArtist), val());
        else if (std::strcmp(id, "TCOM") == 0) CopyTruncated(out.composer,  sizeof(out.composer), val());
        else if (std::strcmp(id, "TCOP") == 0) CopyTruncated(out.copyright,sizeof(out.copyright),val());
        else if (std::strcmp(id, "WXXX") == 0) CopyTruncated(out.url,      sizeof(out.url),      val());
        else if (std::strcmp(id, "TPUB") == 0) CopyTruncated(out.publisher,sizeof(out.publisher),val());
        else if (std::strcmp(id, "TBPM") == 0) {
            auto s = val(); if (!s.empty()) out.bpm = std::atoi(s.c_str());
        }
        else if (std::strcmp(id, "TKEY") == 0) {
            auto s = val();
            // Trim trailing NULs often left by DecodeFrameString
            while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) s.pop_back();
            if (!s.empty()) CopyTruncated(out.key, sizeof(out.key), s);
        }
        else if (std::strcmp(id, "TRCK") == 0) {
            auto s = val(); if (!s.empty()) out.trackNumber = std::atoi(s.c_str());
        }
        else if (pics && std::strcmp(id, "APIC") == 0) {
            // APIC: enc(1) + mime(N) + 0 + pic_type(1) + desc(N) + 0 + data
            if (frameSize < 4) { pos += frameSize; continue; }
            const uint8_t* p = data + pos + 1;
            const uint8_t* end = data + pos + frameSize;
            while (p < end && *p) ++p; // skip mime
            ++p; // skip 0
            if (p >= end) { pos += frameSize; continue; }
            uint8_t picType = *p++;
            while (p < end && *p) ++p; // skip desc
            ++p; // skip 0
            if (p >= end) { pos += frameSize; continue; }
            uint32_t imgSize = static_cast<uint32_t>(end - p);
            ID3Picture pic{};
            pic.data = new uint8_t[imgSize];
            std::memcpy(pic.data, p, imgSize);
            pic.size = imgSize;
            pic.type = picType;
            pics->push_back(pic);
        }
        pos += frameSize;
    }

    // Fallback: Camelot / key often lives in COMM when TKEY is empty (some tools)
    if (out.key[0] == '\0' && out.comment[0] != '\0') {
        char hint[32]{};
        if (ExtractKeyHint(out.comment, hint, sizeof(hint)))
            CopyTruncated(out.key, sizeof(out.key), hint);
    }
    return true;
}

bool ID3Tags::ParseID3v1(const uint8_t* data, uint32_t size, ID3Info& out) {
    if (size < 128) return false;
    const uint8_t* tag = data + size - 128;
    if (std::memcmp(tag, "TAG", 3) != 0) return false;

    auto copyField = [](char* dst, size_t dstSize, const uint8_t* src, size_t srcLen) {
        size_t n = std::min(dstSize - 1, srcLen);
        std::memcpy(dst, src, n);
        dst[n] = '\0';
        // Trim trailing spaces
        while (n > 0 && dst[n-1] == ' ') { dst[--n] = '\0'; }
    };

    copyField(out.title,   sizeof(out.title),   tag + 3,  30);
    copyField(out.artist,  sizeof(out.artist),  tag + 33, 30);
    copyField(out.album,   sizeof(out.album),   tag + 63, 30);
    copyField(out.year,    sizeof(out.year),    tag + 93, 4);
    copyField(out.comment, sizeof(out.comment), tag + 97, 30);
    out.trackNumber = (tag[125] == 0 && tag[126] != 0) ? tag[126] : 0;
    out.genre       = tag[127];
    return true;
}

bool ID3Tags::LoadBasic(const wchar_t* path, ID3Info& out) {
    auto data = LoadFileData(path);
    if (data.empty()) return false;
    std::memset(&out, 0, sizeof(out));
    ID3InfoEx ex{};
    if (ParseID3v2(data.data(), static_cast<uint32_t>(data.size()), ex)) {
        std::memcpy(out.title,   ex.title,   sizeof(out.title));
        std::memcpy(out.artist,  ex.artist,  sizeof(out.artist));
        std::memcpy(out.album,   ex.album,   sizeof(out.album));
        std::memcpy(out.year,    ex.year,    sizeof(out.year));
        std::memcpy(out.comment, ex.comment, sizeof(out.comment));
        out.trackNumber = ex.trackNumber;
        out.genre       = ex.genre;
        return true;
    }
    return ParseID3v1(data.data(), static_cast<uint32_t>(data.size()), out);
}

bool ID3Tags::LoadExtended(const wchar_t* path, ID3InfoEx& out) {
    auto data = LoadFileData(path);
    if (data.empty()) return false;
    std::memset(&out, 0, sizeof(out));
    return ParseID3v2(data.data(), static_cast<uint32_t>(data.size()), out);
}

bool ID3Tags::GetPicture(const wchar_t* path, int index, ID3Picture& out) {
    auto data = LoadFileData(path, 10 * 1024 * 1024); // allow up to 10 MB for cover art
    if (data.empty()) return false;
    ID3InfoEx ex{};
    std::vector<ID3Picture> pics;
    if (!ParseID3v2(data.data(), static_cast<uint32_t>(data.size()), ex, &pics)) return false;
    if (index < 0 || index >= static_cast<int>(pics.size())) return false;
    out = pics[index];
    // Note: caller must delete[] out.data
    return true;
}

} // namespace libZFork
