#pragma once
#include "engine/sound.h"
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace libZFork::engine {

// Process-wide LRU cache for PreDecoded sounds, keyed by file path.
//
// Repeated SoundBankV1 loads (e.g. reloading a level, or two banks sharing
// a UI-click sound) previously re-read and re-decoded the same file from
// disk every single time. GetOrLoad() decodes each unique path only once
// and hands out the same shared_ptr<Sound> to every caller after that.
//
// Streaming sounds are never cached: they hold no PCM data (CreateDecoder()
// just reopens the file per voice), so there is nothing worth keeping.
//
// Budget: SetBudgetBytes(0) — the default — is "dynamic": the cache never
// evicts on its own and simply grows to whatever has actually been loaded,
// so a typical small bank needs no manual sizing at all. Pass a nonzero
// byte budget to cap memory use for large sound libraries; entries are then
// evicted least-recently-used first once the total exceeds it. Evicting an
// entry only drops the cache's own reference — a sound still held by a
// playing voice (or any other shared_ptr) stays alive until that reference
// goes away.
class SoundCache final {
public:
    static SoundCache& Instance();

    // Returns the cached Sound for `path`, loading and inserting it into the
    // cache on a miss. Streaming sounds bypass the cache and are loaded
    // fresh every call (matches the pre-existing Sound::LoadFile behaviour).
    [[nodiscard]] std::shared_ptr<Sound> GetOrLoad(const std::wstring& path, SoundType type);

    // 0 = dynamic/unbounded (default). Applies immediately: lowering the
    // budget below the current usage evicts LRU entries right away.
    void SetBudgetBytes(uint64_t bytes) noexcept;
    [[nodiscard]] uint64_t BudgetBytes() const noexcept;

    void Clear();

    [[nodiscard]] uint64_t    UsedBytes()  const noexcept;
    [[nodiscard]] std::size_t EntryCount() const noexcept;

private:
    struct Entry {
        std::wstring           key;
        std::shared_ptr<Sound> sound;
        uint64_t                bytes = 0;
    };
    using ListIt = std::list<Entry>::iterator;

    void EvictLocked();

    mutable std::mutex                        mutex_;
    std::list<Entry>                          lru_;   // front = most recently used
    std::unordered_map<std::wstring, ListIt>  index_;
    uint64_t                                  usedBytes_   = 0;
    uint64_t                                  budgetBytes_ = 0; // 0 = dynamic
};

} // namespace libZFork::engine
