#include "engine/sound_cache.h"

namespace libZFork::engine {

SoundCache& SoundCache::Instance() {
    static SoundCache instance;
    return instance;
}

std::shared_ptr<Sound> SoundCache::GetOrLoad(const std::wstring& path, SoundType type) {
    if (type != SoundType::PreDecoded) {
        // No PCM data to cache — reopening a stream is already cheap.
        return Sound::LoadFile(path.c_str(), type);
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = index_.find(path);
    if (it != index_.end()) {
        lru_.splice(lru_.begin(), lru_, it->second); // move to MRU; iterators stay valid
        return it->second->sound;
    }

    auto sound = Sound::LoadFile(path.c_str(), type);
    if (!sound) return nullptr;

    const uint64_t bytes = static_cast<uint64_t>(sound->PCMData().size()) * sizeof(float);

    lru_.push_front(Entry{ path, sound, bytes });
    index_[path] = lru_.begin();
    usedBytes_ += bytes;

    EvictLocked();
    return sound;
}

void SoundCache::SetBudgetBytes(uint64_t bytes) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    budgetBytes_ = bytes;
    EvictLocked();
}

uint64_t SoundCache::BudgetBytes() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return budgetBytes_;
}

void SoundCache::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    lru_.clear();
    index_.clear();
    usedBytes_ = 0;
}

uint64_t SoundCache::UsedBytes() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return usedBytes_;
}

std::size_t SoundCache::EntryCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return lru_.size();
}

void SoundCache::EvictLocked() {
    if (budgetBytes_ == 0) return; // dynamic: no cap, never evict
    while (usedBytes_ > budgetBytes_ && !lru_.empty()) {
        Entry& victim = lru_.back();
        usedBytes_ -= victim.bytes;
        index_.erase(victim.key);
        lru_.pop_back();
    }
}

} // namespace libZFork::engine
