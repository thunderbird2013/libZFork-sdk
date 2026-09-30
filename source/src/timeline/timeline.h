#pragma once
/**
 * Timeline — Clip-basiertes Arrangement mit Fade/Envelope
 * ========================================================
 *
 * Datenmodell:
 *
 *   Timeline
 *   └── TimelineTrack[]          (Spuren / Lanes)
 *       └── Clip[]               (Audio-Segmente auf der Zeitachse)
 *           ├── sourceOffsetSec  (wo im Quell-File starten)
 *           ├── durationSec      (Länge des Clips)
 *           ├── timelinePosSec   (Position auf der Timeline)
 *           ├── fadeInSec        (einfacher Fade-In)
 *           ├── fadeOutSec       (einfacher Fade-Out)
 *           └── envelope[]       (benutzerdefinierte Lautstärke-Kurve)
 *               └── EnvelopePoint { timeSec, gain }
 *
 * Wiedergabe übernimmt TimelinePlayer (timeline_player.h).
 */

#include <libzfork/libzfork.h>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace libZFork {

// ── Envelope ──────────────────────────────────────────────────────────────────

enum class FadeType {
    Linear,       // gerade Linie
    EqualPower,   // cos/sin — konstante Leistung (für Crossfades empfohlen)
    SCurve,       // kubische S-Kurve
};

/** Ein Keyframe-Punkt auf der Lautstärke-Kurve. */
struct EnvelopePoint {
    double timeSec = 0.0;   // Zeit innerhalb des Clips (0 = Clip-Start)
    float  gain    = 1.0f;  // Lautstärke (0.0–1.0, auch >1.0 möglich)
};

/**
 * Berechnet die Envelope-Lautstärke an einer bestimmten Clip-Zeit.
 *
 * @param points   Sortierte Keyframe-Liste (nach timeSec aufsteigend).
 * @param clipTime Sekunden ab Clip-Start (0 = erster Frame).
 * @returns interpolierter gain-Wert.
 */
inline float SampleEnvelope(const std::vector<EnvelopePoint>& points, double clipTime) {
    if (points.empty())  return 1.0f;
    if (points.size() == 1) return points[0].gain;

    // Vor dem ersten Punkt
    if (clipTime <= points.front().timeSec) return points.front().gain;
    // Nach dem letzten Punkt
    if (clipTime >= points.back().timeSec)  return points.back().gain;

    // Lineare Interpolation zwischen den umgebenden Keyframes
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (clipTime <= points[i].timeSec) {
            const auto& p0 = points[i - 1];
            const auto& p1 = points[i];
            double span = p1.timeSec - p0.timeSec;
            if (span <= 0.0) return p0.gain;
            double t = (clipTime - p0.timeSec) / span;
            return p0.gain + static_cast<float>(t) * (p1.gain - p0.gain);
        }
    }
    return points.back().gain;
}

// ── Clip ──────────────────────────────────────────────────────────────────────

struct Clip {
    uint32_t     id            = 0;
    std::wstring sourcePath;               // Quell-Datei
    StreamFormat format        = StreamFormat::Autodetect;

    double  timelinePosSec  = 0.0;  // Startposition auf der Timeline
    double  sourceOffsetSec = 0.0;  // Einstiegspunkt im Quell-File
    double  durationSec     = 0.0;  // 0 = bis Datei-Ende

    float   gain            = 1.0f; // Basis-Lautstärke des Clips

    // Einfache Fade-In / Fade-Out (werden in SampleGain berücksichtigt)
    double   fadeInSec      = 0.0;
    double   fadeOutSec     = 0.0;
    FadeType fadeType       = FadeType::Linear;

    // Optionale benutzerdefinierte Envelope (überschreibt die einfachen Fades)
    std::vector<EnvelopePoint> envelope;

    // ── Envelope-Hilfen ──────────────────────────────────────────────────────

    /** Fügt einen Keyframe hinzu und sortiert die Liste danach. */
    void AddPoint(double timeSec, float g) {
        envelope.push_back({ timeSec, g });
        std::sort(envelope.begin(), envelope.end(),
                  [](const EnvelopePoint& a, const EnvelopePoint& b){
                      return a.timeSec < b.timeSec; });
    }

    void ClearEnvelope() { envelope.clear(); }

    /**
     * Berechnet den kombinierten Gain-Wert für einen Frame auf der Timeline.
     *
     * @param timelineTime  aktuelle Wiedergabeposition (absolute Sekunden).
     * @returns             Gain 0.0–1.0 (oder mehr) oder 0 wenn außerhalb.
     */
    [[nodiscard]] float SampleGain(double timelineTime) const {
        // Ist der Frame überhaupt in diesem Clip?
        double clipStart = timelinePosSec;
        double clipEnd   = (durationSec > 0.0)
                           ? timelinePosSec + durationSec
                           : std::numeric_limits<double>::max();
        if (timelineTime < clipStart || timelineTime >= clipEnd) return 0.0f;

        double clipTime = timelineTime - clipStart;  // Zeit ab Clip-Start

        float g = gain;

        if (!envelope.empty()) {
            // Benutzerdefinierte Kurve übernimmt alles
            g *= SampleEnvelope(envelope, clipTime);
        } else {
            // Einfaches Fade-In
            if (fadeInSec > 0.0 && clipTime < fadeInSec) {
                float t = static_cast<float>(clipTime / fadeInSec);
                g *= ApplyFade(t, fadeType, /*fadeIn=*/true);
            }
            // Einfaches Fade-Out (Restzeit am Ende)
            if (fadeOutSec > 0.0 && durationSec > 0.0) {
                double timeLeft = clipEnd - timelineTime;
                if (timeLeft < fadeOutSec) {
                    float t = static_cast<float>(timeLeft / fadeOutSec);
                    g *= ApplyFade(t, fadeType, /*fadeIn=*/false);
                }
            }
        }
        return g;
    }

    // ── Setze Fade-In / Fade-Out schnell ─────────────────────────────────────
    void SetFadeIn (double sec, FadeType type = FadeType::EqualPower) {
        fadeInSec  = sec; fadeType = type;
    }
    void SetFadeOut(double sec, FadeType type = FadeType::EqualPower) {
        fadeOutSec = sec; fadeType = type;
    }

private:
    static float ApplyFade(float t, FadeType type, bool fadeIn) {
        // t: 0=stille Seite, 1=volle Seite
        float v = 0.0f;
        switch (type) {
        case FadeType::Linear:
            v = t;
            break;
        case FadeType::EqualPower:
            v = std::sin(t * std::numbers::pi_v<float> * 0.5f);
            break;
        case FadeType::SCurve:
            // kubische Glättung: 3t²-2t³
            v = t * t * (3.0f - 2.0f * t);
            break;
        }
        return fadeIn ? v : v;  // beide Richtungen gleich — t geht von 0→1
    }
};

// ── TimelineTrack ─────────────────────────────────────────────────────────────

struct TimelineTrack {
    uint32_t     id    = 0;
    std::string  name;
    float        gain  = 1.0f;   // Track-Level Lautstärke
    bool         muted = false;

    std::vector<Clip> clips;

    // ── Clip-Verwaltung ───────────────────────────────────────────────────────

    Clip& AddClip(std::wstring path, double timelinePosSec,
                  double sourceOffsetSec = 0.0, double durationSec = 0.0) {
        static uint32_t s_nextClipId = 1;
        Clip c;
        c.id               = s_nextClipId++;
        c.sourcePath       = std::move(path);
        c.timelinePosSec   = timelinePosSec;
        c.sourceOffsetSec  = sourceOffsetSec;
        c.durationSec      = durationSec;
        clips.push_back(std::move(c));
        return clips.back();
    }

    bool RemoveClip(uint32_t clipId) {
        auto it = std::find_if(clips.begin(), clips.end(),
                               [clipId](const Clip& c){ return c.id == clipId; });
        if (it == clips.end()) return false;
        clips.erase(it);
        return true;
    }

    Clip* FindClip(uint32_t clipId) {
        for (auto& c : clips) if (c.id == clipId) return &c;
        return nullptr;
    }
};

// ── Timeline ──────────────────────────────────────────────────────────────────

class Timeline {
public:
    // Wiedergabe-Parameter (müssen vor dem Start gesetzt sein)
    int sampleRate = 44100;
    int channels   = 2;

    // ── Tracks ────────────────────────────────────────────────────────────────

    TimelineTrack& AddTrack(std::string name = "") {
        static uint32_t s_nextTrackId = 1;
        TimelineTrack t;
        t.id   = s_nextTrackId++;
        t.name = name.empty() ? "Track " + std::to_string(t.id) : std::move(name);
        tracks_.push_back(std::move(t));
        return tracks_.back();
    }

    bool RemoveTrack(uint32_t trackId) {
        auto it = std::find_if(tracks_.begin(), tracks_.end(),
                               [trackId](const TimelineTrack& t){ return t.id == trackId; });
        if (it == tracks_.end()) return false;
        tracks_.erase(it);
        return true;
    }

    TimelineTrack* FindTrack(uint32_t trackId) {
        for (auto& t : tracks_) if (t.id == trackId) return &t;
        return nullptr;
    }

    std::vector<TimelineTrack>& Tracks() { return tracks_; }
    const std::vector<TimelineTrack>& Tracks() const { return tracks_; }

    // ── Gesamtlänge ───────────────────────────────────────────────────────────

    /** Gibt die Gesamtlänge der Timeline zurück (längster Clip-Endpunkt). */
    [[nodiscard]] double GetDuration() const {
        double maxEnd = 0.0;
        for (const auto& track : tracks_) {
            for (const auto& clip : track.clips) {
                double end = clip.timelinePosSec + clip.durationSec;
                if (clip.durationSec <= 0.0) end = clip.timelinePosSec + 3600.0; // unbekannt → 1h
                maxEnd = std::max(maxEnd, end);
            }
        }
        return maxEnd;
    }

    // ── Alle aktiven Clips zu einem Zeitpunkt ─────────────────────────────────

    struct ActiveClip {
        const TimelineTrack* track;
        const Clip*          clip;
    };

    [[nodiscard]] std::vector<ActiveClip> GetActiveClips(double timelineSec) const {
        std::vector<ActiveClip> result;
        for (const auto& track : tracks_) {
            if (track.muted) continue;
            for (const auto& clip : track.clips) {
                double clipEnd = (clip.durationSec > 0.0)
                                  ? clip.timelinePosSec + clip.durationSec
                                  : std::numeric_limits<double>::max();
                if (timelineSec >= clip.timelinePosSec && timelineSec < clipEnd)
                    result.push_back({ &track, &clip });
            }
        }
        return result;
    }

private:
    std::vector<TimelineTrack> tracks_;
};

} // namespace libZFork
