#include "musical_key.h"
#include <cctype>
#include <cstring>
#include <string>

namespace libZFork {
namespace {

struct WheelEntry {
    const char* camelot;  // "8A"
    const char* openKey;  // "1m"
    const char* musical;  // primary spelling "Am"
    const char* alt;      // optional enharmonic / synonym
};

// Camelot wheel (Mixed In Key). Index = (wheel-1)*2 + (minor?0:1)
static const WheelEntry kWheel[24] = {
    {"1A",  "1m",  "Abm", "G#m"},
    {"1B",  "1d",  "B",   nullptr},
    {"2A",  "2m",  "Ebm", "D#m"},
    {"2B",  "2d",  "F#",  "Gb"},
    {"3A",  "3m",  "Bbm", "A#m"},
    {"3B",  "3d",  "Db",  "C#"},
    {"4A",  "4m",  "Fm",  nullptr},
    {"4B",  "4d",  "Ab",  "G#"},
    {"5A",  "5m",  "Cm",  nullptr},
    {"5B",  "5d",  "Eb",  "D#"},
    {"6A",  "6m",  "Gm",  nullptr},
    {"6B",  "6d",  "Bb",  "A#"},
    {"7A",  "7m",  "Dm",  nullptr},
    {"7B",  "7d",  "F",   nullptr},
    {"8A",  "8m",  "Am",  nullptr},
    {"8B",  "8d",  "C",   nullptr},
    {"9A",  "9m",  "Em",  nullptr},
    {"9B",  "9d",  "G",   nullptr},
    {"10A", "10m", "Bm",  nullptr},
    {"10B", "10d", "D",   nullptr},
    {"11A", "11m", "F#m", "Gbm"},
    {"11B", "11d", "A",   nullptr},
    {"12A", "12m", "C#m", "Dbm"},
    {"12B", "12d", "E",   nullptr},
};

static void CopyTrunc(char* dst, std::size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    std::size_t n = 0;
    while (src[n] && n + 1 < cap) {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = '\0';
}

static std::string TrimCopy(const char* text) {
    if (!text) return {};
    while (*text && std::isspace(static_cast<unsigned char>(*text))) ++text;
    std::string s(text);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

static bool EqIgnoreCase(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (std::tolower(static_cast<unsigned char>(*a)) !=
            std::tolower(static_cast<unsigned char>(*b)))
            return false;
        ++a; ++b;
    }
    return *a == *b;
}

static void FillFromWheel(MusicalKeyInfo& out, int idx) {
    const WheelEntry& e = kWheel[idx];
    CopyTrunc(out.camelot, sizeof(out.camelot), e.camelot);
    CopyTrunc(out.openKey, sizeof(out.openKey), e.openKey);
    CopyTrunc(out.musical, sizeof(out.musical), e.musical);
    out.wheel = (idx / 2) + 1;
    out.isMinor = (idx % 2 == 0) ? 1 : 0;
}

static int FindByCamelot(int wheel, bool minor) {
    if (wheel < 1 || wheel > 12) return -1;
    return (wheel - 1) * 2 + (minor ? 0 : 1);
}

static int FindByMusical(const std::string& token) {
    // token already uppercased, may end with M for minor (Am -> AM after upper — careful!)
    // We pass musical forms with lowercase 'm' for minor: normalize separately.
    for (int i = 0; i < 24; ++i) {
        if (EqIgnoreCase(kWheel[i].musical, token.c_str())) return i;
        if (kWheel[i].alt && EqIgnoreCase(kWheel[i].alt, token.c_str())) return i;
    }
    return -1;
}

static bool ParseCamelotToken(const std::string& t, int& wheel, bool& minor) {
    // 1A..12B
    if (t.size() < 2 || t.size() > 3) return false;
    char letter = t.back();
    if (letter != 'A' && letter != 'B') return false;
    minor = (letter == 'A');
    int w = std::atoi(t.substr(0, t.size() - 1).c_str());
    if (w < 1 || w > 12) return false;
    wheel = w;
    return true;
}

static bool ParseOpenKeyToken(const std::string& t, int& wheel, bool& minor) {
    // 1m..12d (m=minor, d=major/dur)
    if (t.size() < 2 || t.size() > 3) return false;
    char letter = static_cast<char>(std::tolower(static_cast<unsigned char>(t.back())));
    if (letter != 'm' && letter != 'd') return false;
    minor = (letter == 'm');
    int w = std::atoi(t.substr(0, t.size() - 1).c_str());
    if (w < 1 || w > 12) return false;
    wheel = w;
    return true;
}

static bool ParseClassicalToken(std::string t, int& idx) {
    // Accept: C, C#, Db, Am, A#m, F#min, Gmajor, etc.
    for (char& c : t) {
        if (c == '♯') c = '#';
        if (c == '♭') c = 'b';
    }
    // remove spaces already done
    std::string lower;
    lower.reserve(t.size());
    for (char c : t) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    bool minor = false;
    if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, "minor") == 0) {
        minor = true;
        lower.resize(lower.size() - 5);
    } else if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, "major") == 0) {
        minor = false;
        lower.resize(lower.size() - 5);
    } else if (lower.size() >= 3 && lower.compare(lower.size() - 3, 3, "min") == 0) {
        minor = true;
        lower.resize(lower.size() - 3);
    } else if (lower.size() >= 3 && lower.compare(lower.size() - 3, 3, "maj") == 0) {
        minor = false;
        lower.resize(lower.size() - 3);
    } else if (!lower.empty() && lower.back() == 'm') {
        // Am, F#m — but not plain "m"
        if (lower.size() >= 2) {
            minor = true;
            lower.pop_back();
        }
    }

    while (!lower.empty() && (lower.back() == '-' || lower.back() == '_'))
        lower.pop_back();
    if (lower.empty()) return false;

    // Build candidate with proper case for table match: Root + optional #/b + optional m
    std::string cand;
    cand.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(lower[0]))));
    for (std::size_t i = 1; i < lower.size(); ++i) {
        char c = lower[i];
        if (c == '#' || c == 'b') cand.push_back(c);
        else return false; // unexpected
    }
    if (minor) cand.push_back('m');

    idx = FindByMusical(cand);
    if (idx >= 0) return true;

    // Try enharmonic: if we have # try b spellings already in table via alt
    return false;
}

} // namespace

bool ZFORK_CALL ParseMusicalKey(const char* text, MusicalKeyInfo& out) {
    std::memset(&out, 0, sizeof(out));
    out.isMinor = -1;
    std::string trimmed = TrimCopy(text);
    if (trimmed.empty()) return false;
    CopyTrunc(out.raw, sizeof(out.raw), trimmed.c_str());

    // Compact upper for Camelot; keep classical parse on original-ish token
    std::string compact;
    for (char c : trimmed) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        compact.push_back(c);
    }
    if (compact.empty()) return false;

    std::string upper = compact;
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    int wheel = 0;
    bool minor = false;
    if (ParseCamelotToken(upper, wheel, minor)) {
        int idx = FindByCamelot(wheel, minor);
        FillFromWheel(out, idx);
        return true;
    }
    // Open Key: 1m..12d (case-insensitive m/d)
    {
        std::string ot = upper;
        if (!ot.empty()) {
            char last = static_cast<char>(std::tolower(static_cast<unsigned char>(ot.back())));
            if (last == 'm' || last == 'd') {
                ot.back() = last;
                if (ParseOpenKeyToken(ot, wheel, minor)) {
                    int idx = FindByCamelot(wheel, minor);
                    FillFromWheel(out, idx);
                    return true;
                }
            }
        }
    }

    int idx = -1;
    if (ParseClassicalToken(compact, idx) && idx >= 0) {
        FillFromWheel(out, idx);
        return true;
    }
    return false;
}

bool ZFORK_CALL KeysHarmonicallyCompatible(const MusicalKeyInfo& a, const MusicalKeyInfo& b) {
    if (a.wheel < 1 || a.wheel > 12 || b.wheel < 1 || b.wheel > 12) return false;
    if (a.isMinor < 0 || b.isMinor < 0) return false;

    // Same Camelot number: perfect match or relative major/minor
    if (a.wheel == b.wheel) return true;

    // Adjacent on wheel, same mode (energy change ±1)
    auto nextW = [](int w) { return w == 12 ? 1 : w + 1; };
    auto prevW = [](int w) { return w == 1 ? 12 : w - 1; };
    if (a.isMinor == b.isMinor) {
        if (nextW(a.wheel) == b.wheel || prevW(a.wheel) == b.wheel)
            return true;
    }
    return false;
}

bool ExtractKeyHint(const char* text, char* outKey, std::size_t outCap) {
    if (!outKey || outCap == 0) return false;
    outKey[0] = '\0';
    if (!text || !*text) return false;

    // Prefer explicit Camelot tokens in free text (Mixed In Key comments, etc.)
    const char* p = text;
    while (*p) {
        while (*p && !std::isalnum(static_cast<unsigned char>(*p))) ++p;
        if (!*p) break;
        const char* start = p;
        while (*p && (std::isalnum(static_cast<unsigned char>(*p)) || *p == '#' || *p == 'b' || *p == 'B'))
            ++p;
        std::string tok(start, p);
        MusicalKeyInfo info{};
        if (ParseMusicalKey(tok.c_str(), info) && info.wheel > 0) {
            // Prefer Camelot notation for DJ mixing UIs
            CopyTrunc(outKey, outCap, info.camelot);
            return true;
        }
    }
    return false;
}

} // namespace libZFork
