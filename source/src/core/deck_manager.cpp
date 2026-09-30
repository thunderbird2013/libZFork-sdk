#include "deck_manager.h"
#include "player.h"

#include <cmath>
#include <algorithm>
#include <format>
#include <numbers>

namespace libZFork {

// ── Konstruktor / Destruktor ───────────────────────────────────────────────────

DeckManager::DeckManager()  = default;
DeckManager::~DeckManager() = default;

// ── Private Helpers ───────────────────────────────────────────────────────────

DeckManager::DeckEntry* DeckManager::FindDeck(int id) {
    for (auto& d : decks_)
        if (d.id == id) return &d;
    return nullptr;
}

const DeckManager::DeckEntry* DeckManager::FindDeck(int id) const {
    for (const auto& d : decks_)
        if (d.id == id) return &d;
    return nullptr;
}

void DeckManager::SetError(std::string_view msg) const {
    error_ = msg;
}

// ── Deck-Verwaltung ───────────────────────────────────────────────────────────

int DeckManager::CreateDeck(std::string_view name) {
    std::lock_guard lock(mtx_);

    DeckEntry entry;
    entry.id     = nextId_++;
    entry.name   = name.empty()
                       ? std::format("Deck {}", entry.id)
                       : std::string(name);
    entry.player = std::make_unique<Player>();

    int id = entry.id;
    decks_.push_back(std::move(entry));
    return id;
}

bool DeckManager::DestroyDeck(int deckId) {
    std::lock_guard lock(mtx_);
    auto it = std::find_if(decks_.begin(), decks_.end(),
                            [deckId](const DeckEntry& d){ return d.id == deckId; });
    if (it == decks_.end()) {
        SetError(std::format("DestroyDeck: Deck {} nicht gefunden", deckId));
        return false;
    }
    it->player->Stop();
    decks_.erase(it);
    return true;
}

int DeckManager::GetDeckCount() const {
    std::lock_guard lock(mtx_);
    return static_cast<int>(decks_.size());
}

std::vector<int> DeckManager::GetDeckIds() const {
    std::lock_guard lock(mtx_);
    std::vector<int> ids;
    ids.reserve(decks_.size());
    for (const auto& d : decks_) ids.push_back(d.id);
    return ids;
}

bool DeckManager::GetDeckInfo(int deckId, DeckInfo& out) const {
    std::lock_guard lock(mtx_);
    const auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("GetDeckInfo: Deck {} nicht gefunden", deckId)); return false; }

    out.id           = d->id;
    out.name         = std::wstring(d->name.begin(), d->name.end());
    out.trackPath    = d->trackPath;
    { auto ss = d->player->GetStatus();
      out.status = ss.playing ? PlayerStatus::Playing
                 : ss.paused  ? PlayerStatus::Paused
                              : PlayerStatus::Stopped; }
    out.bpm          = d->bpm;
    out.volumeLeft   = d->volumeLeft;
    out.volumeRight  = d->volumeRight;
    out.pitchSemi    = d->pitchSemi;
    out.tempoPercent = d->tempoPercent;

    StreamTime pos{};
    if (d->player->GetPosition(pos))
        out.positionSec = static_cast<float>(pos.ms) / 1000.0f;

    StreamInfo info{};
    if (d->player->GetStreamInfo(0, info))
        out.durationSec = static_cast<float>(info.length.ms) / 1000.0f;

    return true;
}

// ── Track laden ───────────────────────────────────────────────────────────────

bool DeckManager::LoadTrack(int deckId, const wchar_t* path, StreamFormat format) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("LoadTrack: Deck {} nicht gefunden", deckId)); return false; }

    d->player->Stop();
    d->player->Close();
    if (!d->player->OpenFile(path, format)) {
        SetError(std::format("LoadTrack: OpenFile fehlgeschlagen — {}", d->player->GetError()));
        return false;
    }
    d->trackPath    = path;
    d->bpm          = 0.0f;   // BPM neu ermitteln wenn gewünscht
    d->pitchSemi    = 0.0f;
    d->tempoPercent = 0.0f;
    return true;
}

bool DeckManager::LoadTrackA(int deckId, const char* path, StreamFormat format) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("LoadTrackA: Deck {} nicht gefunden", deckId)); return false; }

    d->player->Stop();
    d->player->Close();
    if (!d->player->OpenFileA(path, format)) {
        SetError(std::format("LoadTrackA: OpenFileA fehlgeschlagen — {}", d->player->GetError()));
        return false;
    }
    // ANSI → wstring für trackPath
    d->trackPath    = std::wstring(path, path + std::char_traits<char>::length(path));
    d->bpm          = 0.0f;
    d->pitchSemi    = 0.0f;
    d->tempoPercent = 0.0f;
    return true;
}

std::wstring DeckManager::GetTrackPath(int deckId) const {
    std::lock_guard lock(mtx_);
    const auto* d = FindDeck(deckId);
    return d ? d->trackPath : L"";
}

// ── Transport ─────────────────────────────────────────────────────────────────

bool DeckManager::Play(int deckId) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("Play: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->Play();
}

bool DeckManager::Pause(int deckId) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("Pause: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->Pause();
}

bool DeckManager::Resume(int deckId) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("Resume: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->Resume();
}

bool DeckManager::Stop(int deckId) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("Stop: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->Stop();
}

bool DeckManager::Seek(int deckId, float seconds) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("Seek: Deck {} nicht gefunden", deckId)); return false; }

    StreamTime t{};
    t.ms = static_cast<uint32_t>(seconds * 1000.0f);
    return d->player->Seek(SeekOrigin::Begin, t);
}

bool DeckManager::SetLoop(int deckId, uint32_t count) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("SetLoop: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->PlayLoop(count);
}

// ── Position / Status ─────────────────────────────────────────────────────────

float DeckManager::GetPosition(int deckId) const {
    std::lock_guard lock(mtx_);
    const auto* d = FindDeck(deckId);
    if (!d) return 0.0f;
    StreamTime t{};
    if (!d->player->GetPosition(t)) return 0.0f;
    return static_cast<float>(t.ms) / 1000.0f;
}

float DeckManager::GetDuration(int deckId) const {
    std::lock_guard lock(mtx_);
    const auto* d = FindDeck(deckId);
    if (!d) return 0.0f;
    StreamInfo info{};
    if (!d->player->GetStreamInfo(0, info)) return 0.0f;
    return static_cast<float>(info.length.ms) / 1000.0f;
}

PlayerStatus DeckManager::GetStatus(int deckId) const {
    std::lock_guard lock(mtx_);
    const auto* d = FindDeck(deckId);
    if (!d) return PlayerStatus::Stopped;
    auto ss = d->player->GetStatus();
    return ss.playing ? PlayerStatus::Playing
         : ss.paused  ? PlayerStatus::Paused
                      : PlayerStatus::Stopped;
}

// ── Lautstärke ────────────────────────────────────────────────────────────────

bool DeckManager::SetVolume(int deckId, int left, int right) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("SetVolume: Deck {} nicht gefunden", deckId)); return false; }
    left  = std::clamp(left,  0, 100);
    right = std::clamp(right, 0, 100);
    d->volumeLeft  = left;
    d->volumeRight = right;
    return d->player->SetPlayerVolume(left, right);
}

bool DeckManager::GetVolume(int deckId, int& outLeft, int& outRight) const {
    std::lock_guard lock(mtx_);
    const auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("GetVolume: Deck {} nicht gefunden", deckId)); return false; }
    outLeft  = d->volumeLeft;
    outRight = d->volumeRight;
    return true;
}

bool DeckManager::SetCrossfader(int deckA, int deckB, float position) {
    position = std::clamp(position, 0.0f, 1.0f);

    // Konstant-Leistungs-Kurve (gleiche Energie an jedem Punkt)
    const float angle = position * (std::numbers::pi_v<float> / 2.0f);
    const int   volA  = static_cast<int>(std::cos(angle) * 100.0f);
    const int   volB  = static_cast<int>(std::sin(angle) * 100.0f);

    bool ok = true;
    ok &= SetVolume(deckA, volA, volA);
    ok &= SetVolume(deckB, volB, volB);
    return ok;
}

bool DeckManager::Crossfade(int deckFrom, int deckTo, uint32_t durationMs) {
    std::lock_guard lock(mtx_);
    auto* from = FindDeck(deckFrom);
    auto* to   = FindDeck(deckTo);
    if (!from) { SetError(std::format("Crossfade: Quelldeck {} nicht gefunden", deckFrom)); return false; }
    if (!to)   { SetError(std::format("Crossfade: Zieldeck {} nicht gefunden", deckTo));   return false; }

    // Zieldeck starten falls es stoppt
    { auto ss = to->player->GetStatus();
      if (!ss.playing && !ss.paused) to->player->Play(); }

    // deckFrom: aktuelles Volume → 0
    const int fromL = from->volumeLeft,  fromR = from->volumeRight;
    // deckTo:  0 → 100/100
    const int toL   = to->volumeLeft,    toR   = to->volumeRight;

    // SlideVolume läuft asynchron im Player-Thread
    from->player->SlideVolume(fromL, fromR, 0,    0,    durationMs);
    to->player->SlideVolume  (0,     0,     toL,  toR,  durationMs);

    from->volumeLeft  = 0;
    from->volumeRight = 0;
    return true;
}

// ── Pitch / Tempo / Rate ──────────────────────────────────────────────────────

bool DeckManager::SetPitch(int deckId, float semitones) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("SetPitch: Deck {} nicht gefunden", deckId)); return false; }
    d->pitchSemi = semitones;
    return d->player->SetPitch(semitones);
}

bool DeckManager::SetTempo(int deckId, float percent) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("SetTempo: Deck {} nicht gefunden", deckId)); return false; }
    d->tempoPercent = percent;
    // SoundTouch::setTempo() erwartet einen Multiplikator (1.0 = normal, 1.1 = 10% schneller).
    // Die UI übergibt Prozentwerte (-50..+50) → in Ratio umrechnen.
    const float ratio = std::max(0.05f, 1.0f + percent / 100.0f);
    return d->player->SetTempo(ratio);
}

bool DeckManager::SetRate(int deckId, float rate) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("SetRate: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->SetRate(rate);
}

// ── BPM ───────────────────────────────────────────────────────────────────────

bool DeckManager::DetectBPM(int deckId, float& outBpm) {
    // BPM-Analyse kann dauern — Lock nur kurz halten für Zugriff, dann analysieren
    Player* player = nullptr;
    {
        std::lock_guard lock(mtx_);
        auto* d = FindDeck(deckId);
        if (!d) { SetError(std::format("DetectBPM: Deck {} nicht gefunden", deckId)); return false; }
        player = d->player.get();
    }

    float bpm = 0.0f;
    if (!player->DetectBPM(BPMAlgorithm::AutoCorrelation, bpm)) {
        SetError(std::format("DetectBPM: Analyse fehlgeschlagen — {}", player->GetError()));
        return false;
    }

    {
        std::lock_guard lock(mtx_);
        auto* d = FindDeck(deckId);
        if (d) d->bpm = bpm;
    }
    outBpm = bpm;
    return true;
}

bool DeckManager::SetBPM(int deckId, float bpm) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("SetBPM: Deck {} nicht gefunden", deckId)); return false; }
    d->bpm = bpm;
    return true;
}

float DeckManager::GetBPM(int deckId) const {
    std::lock_guard lock(mtx_);
    const auto* d = FindDeck(deckId);
    return d ? d->bpm : 0.0f;
}

bool DeckManager::SyncBPM(int deckMaster, int deckSlave) {
    std::lock_guard lock(mtx_);
    const auto* master = FindDeck(deckMaster);
    auto*       slave  = FindDeck(deckSlave);

    if (!master) { SetError(std::format("SyncBPM: Master-Deck {} nicht gefunden", deckMaster)); return false; }
    if (!slave)  { SetError(std::format("SyncBPM: Slave-Deck {}  nicht gefunden", deckSlave));  return false; }

    if (master->bpm <= 0.0f) {
        SetError(std::format("SyncBPM: Master BPM unbekannt (DetectBPM/SetBPM zuerst aufrufen)"));
        return false;
    }
    if (slave->bpm <= 0.0f) {
        SetError(std::format("SyncBPM: Slave BPM unbekannt (DetectBPM/SetBPM zuerst aufrufen)"));
        return false;
    }

    // Tempo-Faktor: Ratio direkt an SoundTouch übergeben (SoundTouch erwartet Multiplikator)
    const float ratio   = master->bpm / slave->bpm;  // z.B. 128/120 = 1.0667
    const float percent = (ratio - 1.0f) * 100.0f;  // +6.67%
    slave->tempoPercent = percent;
    return slave->player->SetTempo(std::max(0.05f, ratio));  // Ratio, nicht Prozent!
}

// ── Equalizer ─────────────────────────────────────────────────────────────────

bool DeckManager::EnableEQ(int deckId, bool enable) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("EnableEQ: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->EnableEqualizer(enable);
}

bool DeckManager::SetEQBand(int deckId, int band, float gainDb) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("SetEQBand: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->SetEqualizerBandGain(band, gainDb);
}

bool DeckManager::GetEQBand(int deckId, int band, float& outGainDb) const {
    std::lock_guard lock(mtx_);
    const auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("GetEQBand: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->GetEqualizerBandGain(band, outGainDb);
}

bool DeckManager::ResetEQ(int deckId) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("ResetEQ: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->ResetEqualizer();
}

// ── Echo ──────────────────────────────────────────────────────────────────────

bool DeckManager::EnableEcho(int deckId, bool enable) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("EnableEcho: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->EnableEcho(enable);
}

bool DeckManager::SetEchoParams(int deckId, const EchoParams& params) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("SetEchoParams: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->SetEchoParams(params);
}

bool DeckManager::GetEchoParams(int deckId, EchoParams& outParams) const {
    std::lock_guard lock(mtx_);
    const auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("GetEchoParams: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->GetEchoParams(outParams);
}

// ── Analyse ───────────────────────────────────────────────────────────────────

bool DeckManager::GetVU(int deckId, VUData& outVU) const {
    std::lock_guard lock(mtx_);
    const auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("GetVU: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->GetVUData(outVU);
}

bool DeckManager::GetFFT(int deckId, float* outLeft, float* outRight, uint32_t points) const {
    std::lock_guard lock(mtx_);
    const auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("GetFFT: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->GetFFTData(outLeft, outRight, points);
}

// ── Track-Info / ID3 ─────────────────────────────────────────────────────────

bool DeckManager::GetStreamInfo(int deckId, StreamInfo& outInfo) const {
    std::lock_guard lock(mtx_);
    const auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("GetStreamInfo: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->GetStreamInfo(0, outInfo);
}

bool DeckManager::GetID3(int deckId, ID3Info& outInfo) const {
    std::lock_guard lock(mtx_);
    const auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("GetID3: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->LoadID3(0, outInfo);
}

bool DeckManager::GetID3Ex(int deckId, ID3InfoEx& outInfo) const {
    std::lock_guard lock(mtx_);
    const auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("GetID3Ex: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->LoadID3Ex(0, outInfo);
}

// ── Callback ──────────────────────────────────────────────────────────────────

bool DeckManager::SetCallback(int deckId, PlayerCallback callback, void* userData) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("SetCallback: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->SetCallback(callback, userData);
}

// ── Soundkarte ───────────────────────────────────────────────────────────────

bool DeckManager::SetDevice(int deckId, int deviceId) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    if (!d) { SetError(std::format("SetDevice: Deck {} nicht gefunden", deckId)); return false; }
    return d->player->SetWaveOutDevice(deviceId);
}

int DeckManager::EnumerateDevices() const {
    std::lock_guard lock(mtx_);
    if (decks_.empty()) {
        // Temporären Player nur zum Enumerieren
        Player tmp;
        return tmp.EnumerateWaveOutDevices();
    }
    return decks_.front().player->EnumerateWaveOutDevices();
}

bool DeckManager::GetDeviceInfo(int deviceId, WaveDeviceInfo& outInfo) const {
    std::lock_guard lock(mtx_);
    if (decks_.empty()) {
        Player tmp;
        return tmp.GetWaveOutDeviceInfo(deviceId, outInfo);
    }
    return decks_.front().player->GetWaveOutDeviceInfo(deviceId, outInfo);
}

// ── Direktzugriff ─────────────────────────────────────────────────────────────

Player* DeckManager::GetPlayer(int deckId) {
    std::lock_guard lock(mtx_);
    auto* d = FindDeck(deckId);
    return d ? d->player.get() : nullptr;
}

const Player* DeckManager::GetPlayer(int deckId) const {
    std::lock_guard lock(mtx_);
    const auto* d = FindDeck(deckId);
    return d ? d->player.get() : nullptr;
}

// ── Fehler ────────────────────────────────────────────────────────────────────

const char* DeckManager::GetError() const {
    return error_.c_str();
}

} // namespace libZFork
