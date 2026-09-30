/**
 * libZFork2 — SoundBank V1 Compiler (text -> binary "ZFBK", nothing else)
 *
 * Minimal wrapper around zfork2_CompileBankV1 / zfork2_CompileBankV1Embedded
 * for when you already have a hand-written or hand-edited bank .txt and
 * just want the compiled .bank — no folder scanning (see
 * zfork2_bank_builder for that), no demo playback.
 *
 * Usage:
 *   zfork2_bank_compile <input.txt> <output.bank> [--embed]
 *
 *   --embed   Pack every referenced sound file's raw bytes into the .bank
 *             (zfork2_CompileBankV1Embedded) instead of just storing paths
 *             (zfork2_CompileBankV1, the default) — produces a single
 *             self-contained file with no external sounds/ folder needed.
 *
 * Paths inside <input.txt> are resolved relative to <input.txt>'s own
 * directory (same rule as loading a bank) — this only matters for --embed,
 * which actually reads those files; without --embed the paths are just
 * copied through as-is.
 */

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <libzfork/libzfork2.h>

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: zfork2_bank_compile <input.txt> <output.bank> [--embed]\n");
        return 1;
    }

    fs::path inTxt  = argv[1];
    fs::path outBank = argv[2];
    bool     embed  = (argc >= 4) && (std::string(argv[3]) == "--embed");

    if (!fs::exists(inTxt)) {
        std::fprintf(stderr, "FEHLER: '%s' existiert nicht.\n", inTxt.string().c_str());
        return 1;
    }

    int ok = embed
        ? zfork2_CompileBankV1Embedded(inTxt.wstring().c_str(), outBank.wstring().c_str())
        : zfork2_CompileBankV1(inTxt.wstring().c_str(), outBank.wstring().c_str());

    if (!ok) {
        std::fprintf(stderr, "FEHLER: Kompilieren fehlgeschlagen (%s -> %s).\n",
                     inTxt.string().c_str(), outBank.string().c_str());
        return 1;
    }

    std::error_code ec;
    uintmax_t size = fs::file_size(outBank, ec);
    std::printf("OK: %s -> %s (%s, %llu bytes)\n",
                inTxt.string().c_str(), outBank.string().c_str(),
                embed ? "eingebettet" : "extern referenziert",
                ec ? 0ull : static_cast<unsigned long long>(size));
    return 0;
}
