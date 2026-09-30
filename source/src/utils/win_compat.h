#pragma once

#ifdef _WIN32
#  include <windows.h>
#else
#  include <cstdio>
#  include <cstdint>
#  include <filesystem>
#  include <string>

using BOOL = int;
using DWORD = uint32_t;
using HANDLE = std::FILE*;
using LPVOID = void*;
using LPCVOID = const void*;
using LPDWORD = DWORD*;

struct LARGE_INTEGER {
    long long QuadPart = 0;
};

constexpr BOOL TRUE = 1;
constexpr BOOL FALSE = 0;

constexpr DWORD GENERIC_READ = 0x80000000u;
constexpr DWORD GENERIC_WRITE = 0x40000000u;
constexpr DWORD FILE_SHARE_READ = 0x00000001u;
constexpr DWORD OPEN_EXISTING = 3u;
constexpr DWORD CREATE_ALWAYS = 2u;
constexpr DWORD FILE_ATTRIBUTE_NORMAL = 0x00000080u;
constexpr DWORD FILE_FLAG_SEQUENTIAL_SCAN = 0x08000000u;

constexpr int FILE_BEGIN = SEEK_SET;
constexpr int FILE_CURRENT = SEEK_CUR;
constexpr int FILE_END = SEEK_END;

constexpr HANDLE INVALID_HANDLE_VALUE = nullptr;

inline HANDLE CreateFileW(const wchar_t* path,
                          DWORD desiredAccess,
                          DWORD /*shareMode*/,
                          void* /*securityAttributes*/,
                          DWORD creationDisposition,
                          DWORD /*flagsAndAttributes*/,
                          HANDLE /*templateFile*/) {
    if (!path) return INVALID_HANDLE_VALUE;

    const bool canWrite = (desiredAccess & GENERIC_WRITE) != 0;
    const bool canRead = (desiredAccess & GENERIC_READ) != 0;

    const char* mode = "rb";
    if (creationDisposition == CREATE_ALWAYS) {
        mode = canRead ? "wb+" : "wb";
    } else if (canWrite) {
        mode = canRead ? "rb+" : "ab";
    }

    const std::string native = std::filesystem::path(path).string();
    return std::fopen(native.c_str(), mode);
}

inline BOOL ReadFile(HANDLE hFile, LPVOID buffer, DWORD count, LPDWORD outRead, void* /*ov*/) {
    if (!hFile || !buffer) return FALSE;
    const size_t n = std::fread(buffer, 1, count, hFile);
    if (outRead) *outRead = static_cast<DWORD>(n);
    return TRUE;
}

inline BOOL WriteFile(HANDLE hFile, LPCVOID buffer, DWORD count, LPDWORD outWritten, void* /*ov*/) {
    if (!hFile || !buffer) return FALSE;
    const size_t n = std::fwrite(buffer, 1, count, hFile);
    if (outWritten) *outWritten = static_cast<DWORD>(n);
    return TRUE;
}

inline BOOL GetFileSizeEx(HANDLE hFile, LARGE_INTEGER* outSize) {
    if (!hFile || !outSize) return FALSE;
    const long cur = std::ftell(hFile);
    if (cur < 0) return FALSE;
    if (std::fseek(hFile, 0, SEEK_END) != 0) return FALSE;
    const long end = std::ftell(hFile);
    if (end < 0) return FALSE;
    outSize->QuadPart = static_cast<long long>(end);
    return std::fseek(hFile, cur, SEEK_SET) == 0 ? TRUE : FALSE;
}

inline BOOL SetFilePointerEx(HANDLE hFile,
                             LARGE_INTEGER distance,
                             LARGE_INTEGER* outNewPos,
                             DWORD moveMethod) {
    if (!hFile) return FALSE;
    if (std::fseek(hFile, static_cast<long>(distance.QuadPart), static_cast<int>(moveMethod)) != 0) {
        return FALSE;
    }
    if (outNewPos) {
        const long pos = std::ftell(hFile);
        if (pos < 0) return FALSE;
        outNewPos->QuadPart = static_cast<long long>(pos);
    }
    return TRUE;
}

inline BOOL CloseHandle(HANDLE hFile) {
    if (!hFile) return FALSE;
    return std::fclose(hFile) == 0 ? TRUE : FALSE;
}
#endif

