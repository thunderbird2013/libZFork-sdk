#pragma once
#include <libzfork/libzfork.h>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace libZFork {

// ID3v1 / ID3v2 tag parser. Reads from file only.
class ID3Tags {
public:
    [[nodiscard]] static bool LoadBasic(const wchar_t* path, ID3Info& out);
    [[nodiscard]] static bool LoadExtended(const wchar_t* path, ID3InfoEx& out);
    [[nodiscard]] static bool GetPicture(const wchar_t* path, int index, ID3Picture& out);

private:
    // Returns true if path has an ID3v2 header and fills basic fields
    static bool ParseID3v2(const uint8_t* data, uint32_t size, ID3InfoEx& out,
                            std::vector<ID3Picture>* pics = nullptr);
    static bool ParseID3v1(const uint8_t* data, uint32_t size, ID3Info& out);

    static std::string DecodeFrameString(const uint8_t* data, uint32_t size);
    static void        CopyTruncated(char* dst, size_t dstSize, std::string_view src);
};

} // namespace libZFork
