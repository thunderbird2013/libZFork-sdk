/**
 * libZFork — ID3 Tag Reader
 *
 * Reads and displays all ID3 tags from audio files.
 * Optionally exports the embedded album art to a file.
 *
 * Usage:
 *   id3_reader <file> [options]
 *
 * Options:
 *   --art <out.jpg>   Save embedded album art to file
 *   --json            Output tags as JSON
 */

#define NOMINMAX
#include <libzfork/libzfork.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

// ── Helpers ────────────────────────────────────────────────────────────────────

static bool HasFlag(int argc, char* argv[], const char* flag) {
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], flag)) return true;
    return false;
}

static const char* GetArg(int argc, char* argv[], const char* key) {
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], key)) return argv[i + 1];
    return nullptr;
}

static std::wstring Utf8ToWide(const char* s) {
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), n);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

static std::string JsonEscape(const char* s) {
    std::string out;
    if (!s) return out;
    for (const char* p = s; *p; ++p) {
        if (*p == '"')       out += "\\\"";
        else if (*p == '\\') out += "\\\\";
        else if (*p == '\n') out += "\\n";
        else if (*p == '\r') out += "\\r";
        else if (*p == '\t') out += "\\t";
        else                 out += *p;
    }
    return out;
}

static void PrintField(const char* label, const char* value) {
    if (!value || !value[0]) return;
    std::printf("  %-18s %s\n", label, value);
}

static void PrintFieldInt(const char* label, int value) {
    if (value == 0) return;
    std::printf("  %-18s %d\n", label, value);
}

static void PrintSep(const char* title) {
    std::printf("\n── %s ", title);
    int len = 4 + static_cast<int>(std::strlen(title));
    for (int i = len; i < 60; ++i) std::printf("─");
    std::printf("\n");
}

// ── Album art ──────────────────────────────────────────────────────────────────

static const char* PicTypeName(int type) {
    switch (type) {
        case 0: return "JPEG";
        case 1: return "PNG";
        case 2: return "BMP";
        default: return "unknown";
    }
}

static bool SaveAlbumArt(libZFork::ZFork* player, int songIdx, const char* outPath) {
    libZFork::ID3Picture pic{};
    if (!player->GetID3Picture(songIdx, 0, pic) || !pic.data || pic.size == 0) {
        std::printf("No embedded album art found.\n");
        return false;
    }

    auto wOut = Utf8ToWide(outPath);
    HANDLE hf = CreateFileW(wOut.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) {
        std::printf("Cannot create: %s\n", outPath);
        return false;
    }
    DWORD written = 0;
    ::WriteFile(hf, static_cast<LPCVOID>(pic.data), pic.size, &written, nullptr);
    CloseHandle(hf);

    std::printf("Album art saved: %s (%u bytes, %s)\n",
                outPath, pic.size, PicTypeName(pic.type));
    return true;
}

// ── JSON output ────────────────────────────────────────────────────────────────

static void PrintJSON(libZFork::ZFork* player, int idx,
                      const libZFork::ID3InfoEx& tag,
                      const libZFork::StreamInfo& info) {
    libZFork::ID3Picture pic{};
    bool hasArt = player->GetID3Picture(idx, 0, pic) && pic.data && pic.size > 0;

    std::printf("{\n");
    std::printf("  \"title\":        \"%s\",\n",  JsonEscape(tag.title).c_str());
    std::printf("  \"artist\":       \"%s\",\n",  JsonEscape(tag.artist).c_str());
    std::printf("  \"album\":        \"%s\",\n",  JsonEscape(tag.album).c_str());
    std::printf("  \"albumArtist\":  \"%s\",\n",  JsonEscape(tag.albumArtist).c_str());
    std::printf("  \"year\":         \"%s\",\n",  JsonEscape(tag.year).c_str());
    std::printf("  \"comment\":      \"%s\",\n",  JsonEscape(tag.comment).c_str());
    std::printf("  \"composer\":     \"%s\",\n",  JsonEscape(tag.composer).c_str());
    std::printf("  \"copyright\":    \"%s\",\n",  JsonEscape(tag.copyright).c_str());
    std::printf("  \"publisher\":    \"%s\",\n",  JsonEscape(tag.publisher).c_str());
    std::printf("  \"url\":          \"%s\",\n",  JsonEscape(tag.url).c_str());
    std::printf("  \"track\":        %d,\n",       tag.trackNumber);
    std::printf("  \"bpm\":          %d,\n",       tag.bpm);
    std::printf("  \"sampleRate\":   %d,\n",       info.samplingRate);
    std::printf("  \"channels\":     %d,\n",       info.channelNumber);
    std::printf("  \"bitrate\":      %d,\n",       info.bitrate);
    std::printf("  \"vbr\":          %s,\n",       info.vbr ? "true" : "false");
    std::printf("  \"durationSec\":  %u,\n",
                info.length.hms.hour * 3600u +
                info.length.hms.minute * 60u +
                info.length.hms.second);
    std::printf("  \"albumArt\":     %s\n", hasArt ? "true" : "false");
    std::printf("}\n");
}

// ── Main ───────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::printf("Usage: id3_reader <audio_file> [--art out.jpg] [--json]\n");
        return 1;
    }

    bool       doJson = HasFlag(argc, argv, "--json");
    const char* artOut = GetArg(argc, argv, "--art");

    auto* player = libZFork::CreateZFork();
    if (!player) { std::printf("CreateZFork() failed\n"); return 1; }

    auto wPath = Utf8ToWide(argv[1]);
    if (!player->OpenFile(wPath.c_str(), libZFork::StreamFormat::Autodetect)) {
        std::printf("OpenFile failed: %s\n", player->GetError());
        libZFork::DestroyZFork(player);
        return 1;
    }

    libZFork::ID3InfoEx  tag{};
    libZFork::StreamInfo info{};
    player->LoadID3Ex(0, tag);
    player->GetStreamInfo(0, info);

    if (doJson) {
        PrintJSON(player, 0, tag, info);
        if (artOut) SaveAlbumArt(player, 0, artOut);
        libZFork::DestroyZFork(player);
        return 0;
    }

    // ── Human-readable ────────────────────────────────────────────────────────
    std::printf("=== libZFork ID3 Reader ===\n");
    std::printf("File: %s\n", argv[1]);

    PrintSep("ID3 Tags");
    PrintField   ("Title:",        tag.title);
    PrintField   ("Artist:",       tag.artist);
    PrintField   ("Album:",        tag.album);
    PrintField   ("Album Artist:", tag.albumArtist);
    PrintField   ("Year:",         tag.year);
    PrintFieldInt("Track:",        tag.trackNumber);
    PrintFieldInt("BPM:",          tag.bpm);
    PrintField   ("Composer:",     tag.composer);
    PrintField   ("Publisher:",    tag.publisher);
    PrintField   ("Copyright:",    tag.copyright);
    PrintField   ("URL:",          tag.url);
    PrintField   ("Comment:",      tag.comment);

    // Album art
    {
        libZFork::ID3Picture pic{};
        bool hasArt = player->GetID3Picture(0, 0, pic) && pic.data && pic.size > 0;
        std::printf("  %-18s %s", "Album Art:", hasArt ? "Yes" : "No");
        if (hasArt)
            std::printf("  (%u bytes, %s, \"%s\")",
                        pic.size, PicTypeName(pic.type),
                        pic.description[0] ? pic.description : "Front Cover");
        std::printf("\n");
    }

    PrintSep("Stream Info");
    std::printf("  %-18s %d Hz\n",    "Sample rate:", info.samplingRate);
    std::printf("  %-18s %d\n",       "Channels:",    info.channelNumber);
    std::printf("  %-18s %d kbps%s\n","Bitrate:",     info.bitrate,
                info.vbr ? " (VBR)" : "");
    std::printf("  %-18s %02u:%02u:%02u.%03u\n", "Duration:",
                info.length.hms.hour, info.length.hms.minute,
                info.length.hms.second, info.length.hms.millisecond);

    if (artOut) {
        std::printf("\n");
        SaveAlbumArt(player, 0, artOut);
    }

    libZFork::DestroyZFork(player);
    return 0;
}
