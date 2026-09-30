/**
 * libZFork2 — SoundBank V1 Bulk-Import Tool
 *
 * Scans a folder tree for audio files and generates a text-format
 * SoundBankV1 bank (see engine/soundbank_v1.h for the format) covering all
 * of them, using simple zero-config conventions instead of a sidecar config
 * file:
 *
 *   - eventId   = filename without extension, sanitised to [a-z0-9_]
 *                 (lowercased, spaces/dashes -> '_', other chars dropped).
 *                 A collision gets a numeric suffix (_2, _3, ...).
 *   - group     = immediate parent folder name (relative to the scanned
 *                 root); files directly in the root go to "sfx".
 *   - type      = "streaming" if the file is larger than
 *                 kStreamingThresholdBytes OR its group is "music" or
 *                 "streaming"; "predecoded" otherwise.
 *   - loop      = -1 (infinite) for the "music" group, 0 otherwise.
 *   - volume/pan/priority = sensible defaults (1.0 / 0.0 / 5).
 *
 * The output is a completely ordinary bank .txt — meant to be hand-edited
 * afterwards (volumes, loop points, exceptions), not a black box. This is
 * the CLI precursor to a future GUI bank builder: same scan+defaults logic,
 * just without a visual editor in front of it yet.
 *
 * Usage:
 *   zfork2_bank_builder <folder> [output.txt] [--compile out.bank] [--embed]
 *
 *   <folder>          Directory to scan recursively for audio files.
 *   output.txt        Where to write the generated bank (default:
 *                      <folder>/soundbank_generated.txt).
 *   --compile out.bank  Also compile the generated .txt to binary (v3 "ZFBK").
 *   --embed             With --compile: embed the audio bytes into the
 *                        .bank instead of referencing them externally
 *                        (zfork2_CompileBankV1Embedded instead of
 *                        zfork2_CompileBankV1).
 */

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <libzfork/libzfork2.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr uintmax_t kStreamingThresholdBytes = 500 * 1024; // 500 KiB

const std::set<std::string> kAudioExtensions = {
    ".ogg", ".oga", ".wav", ".wave", ".mp3", ".flac",
    ".opus", ".m4a", ".aac", ".aif", ".aiff",
};

struct ScannedFile {
    fs::path    absPath;
    std::string relPathUtf8; // relative to scan root, forward slashes, for the bank's path field
    std::string group;
};

std::string SanitizeEventId(const std::string& stem) {
    std::string out;
    out.reserve(stem.size());
    for (unsigned char c : stem) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
        } else if (c == ' ' || c == '-' || c == '_') {
            if (!out.empty() && out.back() != '_') out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.empty()) out = "sound";
    return out;
}

std::string ToForwardSlashes(const fs::path& p) {
    std::string s = p.generic_string();
    return s;
}

std::string LowerExt(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::string PadRight(const std::string& s, size_t width) {
    return s.size() >= width ? s : s + std::string(width - s.size(), ' ');
}

std::string PadLeft(const std::string& s, size_t width) {
    return s.size() >= width ? s : std::string(width - s.size(), ' ') + s;
}

// One fully-formatted bank row, ready to print in the aligned table.
struct BankRow {
    std::string eventId, path, group, type;
    std::string volStr, panStr, prioStr, loopStr;
};

std::vector<ScannedFile> ScanFolder(const fs::path& root) {
    std::vector<ScannedFile> out;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const fs::path& p = it->path();
        if (kAudioExtensions.find(LowerExt(p)) == kAudioExtensions.end()) continue;

        ScannedFile f;
        f.absPath = p;
        fs::path rel = fs::relative(p, root, ec);
        if (ec) rel = p.filename();
        f.relPathUtf8 = ToForwardSlashes(rel);

        fs::path parent = rel.parent_path();
        f.group = parent.empty() ? "sfx" : ToForwardSlashes(parent.filename());
        out.push_back(std::move(f));
    }
    return out;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr,
            "Usage: zfork2_bank_builder <folder> [output.txt] [--compile out.bank] [--embed]\n");
        return 1;
    }

    fs::path root = argv[1];
    if (!fs::exists(root) || !fs::is_directory(root)) {
        std::fprintf(stderr, "FEHLER: '%s' ist kein Ordner.\n", root.string().c_str());
        return 1;
    }

    fs::path outTxt = root / "soundbank_generated.txt";
    fs::path compileTo;
    bool     embed = false;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--compile" && i + 1 < argc) {
            compileTo = argv[++i];
        } else if (arg == "--embed") {
            embed = true;
        } else {
            outTxt = arg;
        }
    }

    auto files = ScanFolder(root);
    if (files.empty()) {
        std::fprintf(stderr, "Keine Audiodateien in '%s' gefunden.\n", root.string().c_str());
        return 1;
    }
    std::sort(files.begin(), files.end(), [](const ScannedFile& a, const ScannedFile& b) {
        return a.relPathUtf8 < b.relPathUtf8;
    });

    // ── Build rows ─────────────────────────────────────────────────────────
    std::map<std::string, int>          eventIdCounts;
    std::map<std::string, std::vector<BankRow>> byGroup; // std::map -> groups come out alphabetically
    uint32_t written = 0;

    for (const auto& f : files) {
        std::string base = SanitizeEventId(f.absPath.stem().string());
        std::string eventId = base;
        int& count = eventIdCounts[base];
        if (count > 0) eventId = base + "_" + std::to_string(count + 1);
        ++count;

        std::error_code ec;
        uintmax_t size = fs::file_size(f.absPath, ec);
        bool streaming = (!ec && size > kStreamingThresholdBytes) ||
                         f.group == "music" || f.group == "streaming";
        int32_t loopCount = (f.group == "music") ? -1 : 0;

        BankRow row;
        row.eventId = eventId;
        row.path    = f.relPathUtf8;
        row.group   = f.group;
        row.type    = streaming ? "streaming" : "predecoded";
        row.volStr  = "1.00";
        row.panStr  = "0.00";
        row.prioStr = "5";
        row.loopStr = std::to_string(loopCount);

        byGroup[f.group].push_back(std::move(row));
        ++written;
    }

    // ── Column widths (across all groups, so columns line up everywhere) ───
    size_t wEvent = 7, wPath = 4, wGroup = 5, wType = 9; // header label lengths as floor
    for (const auto& [groupName, rows] : byGroup) {
        for (const auto& r : rows) {
            wEvent = std::max(wEvent, r.eventId.size());
            wPath  = std::max(wPath,  r.path.size());
            wGroup = std::max(wGroup, r.group.size());
            wType  = std::max(wType,  r.type.size());
        }
    }
    constexpr size_t wVol = 4, wPan = 4, wPrio = 4, wLoop = 4;

    auto writeHeaderRow = [&](std::ostream& s, const char* prefix) {
        s << prefix
          << PadRight("eventId", wEvent) << " ; " << PadRight("path", wPath) << " ; "
          << PadRight("group", wGroup) << " ; " << PadRight("type", wType) << " ; "
          << PadLeft("vol", wVol) << " ; " << PadLeft("pan", wPan) << " ; "
          << PadLeft("prio", wPrio) << " ; " << PadLeft("loop", wLoop) << "\n";
    };

    const size_t ruleWidth = wEvent + wPath + wGroup + wType + wVol + wPan + wPrio + wLoop + 3 * 7 + 2;

    std::ofstream out(outTxt);
    if (!out.is_open()) {
        std::fprintf(stderr, "FEHLER: kann '%s' nicht schreiben.\n", outTxt.string().c_str());
        return 1;
    }

    const std::string rule(ruleWidth, '=');
    out << "# " << rule << "\n";
    out << "# libZFork2 SoundBank V1 -- auto-generiert von zfork2_bank_builder\n";
    out << "# Quelle: " << ToForwardSlashes(root) << "\n";
    out << "#\n";
    out << "# Konventionen: Gruppe = Unterordnername (Root-Dateien -> \"sfx\").\n";
    out << "#   Typ  = streaming ab " << (kStreamingThresholdBytes / 1024)
        << " KiB oder Gruppe 'music'/'streaming', sonst predecoded.\n";
    out << "#   vol  = Lautstaerke 0.0-4.0 | pan = -1.0(L)..+1.0(R)\n";
    out << "#   prio = Prioritaet, hoeher gewinnt bei Voice-Knappheit\n";
    out << "#   loop = -1 endlos, 0 kein Loop, N = N-mal wiederholen\n";
    out << "# Diese Datei ist ein normaler Ausgangspunkt zum Nachbearbeiten.\n";
    out << "# " << rule << "\n\n";

    for (const auto& [groupName, rows] : byGroup) {
        std::string groupHeader = "# --- Gruppe: " + groupName + " (" + std::to_string(rows.size()) +
                                   (rows.size() == 1 ? " Sound) " : " Sounds) ");
        out << groupHeader
            << std::string(groupHeader.size() < ruleWidth + 2 ? ruleWidth + 2 - groupHeader.size() : 0, '-')
            << "\n";
        writeHeaderRow(out, "# ");

        for (const auto& r : rows) {
            out << PadRight(r.eventId, wEvent) << " ; " << PadRight(r.path, wPath) << " ; "
                << PadRight(r.group, wGroup) << " ; " << PadRight(r.type, wType) << " ; "
                << PadLeft(r.volStr, wVol) << " ; " << PadLeft(r.panStr, wPan) << " ; "
                << PadLeft(r.prioStr, wPrio) << " ; " << PadLeft(r.loopStr, wLoop) << "\n";
        }
        out << "\n";
    }
    out.close();

    std::printf("Bank-Quelle geschrieben: %s (%u Eintraege, %zu Gruppe(n))\n",
                outTxt.string().c_str(), written, byGroup.size());

    if (!compileTo.empty()) {
        int ok = embed
            ? zfork2_CompileBankV1Embedded(outTxt.wstring().c_str(), compileTo.wstring().c_str())
            : zfork2_CompileBankV1(outTxt.wstring().c_str(), compileTo.wstring().c_str());
        if (ok) {
            std::error_code ec;
            uintmax_t sz = fs::file_size(compileTo, ec);
            std::printf("Kompiliert (%s): %s (%llu bytes)\n",
                        embed ? "eingebettet" : "extern referenziert",
                        compileTo.string().c_str(), ec ? 0ull : static_cast<unsigned long long>(sz));
        } else {
            std::fprintf(stderr, "FEHLER: Kompilieren nach '%s' fehlgeschlagen.\n", compileTo.string().c_str());
            return 1;
        }
    }

    return 0;
}
