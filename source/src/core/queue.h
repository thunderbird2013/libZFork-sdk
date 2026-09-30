#pragma once
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <span>
#include <optional>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace libZFork {

// Thread-safe circular PCM audio buffer
class AudioQueue {
public:
    explicit AudioQueue(uint32_t capacityBytes = 1024 * 1024 * 4)
        : buf_(capacityBytes), capacity_(capacityBytes) {}

    // Returns bytes written (0 if full and !block)
    uint32_t Write(std::span<const std::byte> data, bool block = false) {
        std::unique_lock lock(mtx_);
        if (block) {
            notFull_.wait(lock, [&] { return Free() >= data.size() || closed_; });
        }
        if (closed_) return 0;
        uint32_t toWrite = static_cast<uint32_t>(std::min(data.size(), static_cast<size_t>(Free())));
        if (toWrite == 0) return 0;
        uint32_t part1 = std::min(toWrite, capacity_ - writePos_);
        std::memcpy(buf_.data() + writePos_, data.data(), part1);
        if (toWrite > part1)
            std::memcpy(buf_.data(), data.data() + part1, toWrite - part1);
        writePos_ = (writePos_ + toWrite) % capacity_;
        size_ += toWrite;
        notEmpty_.notify_one();
        return toWrite;
    }

    // Returns bytes read (0 if empty and !block)
    uint32_t Read(std::span<std::byte> dest, bool block = false) {
        std::unique_lock lock(mtx_);
        if (block) {
            notEmpty_.wait(lock, [&] { return size_ > 0 || closed_; });
        }
        if (size_ == 0) return 0;
        uint32_t toRead = static_cast<uint32_t>(std::min(dest.size(), static_cast<size_t>(size_)));
        uint32_t part1  = std::min(toRead, capacity_ - readPos_);
        std::memcpy(dest.data(), buf_.data() + readPos_, part1);
        if (toRead > part1)
            std::memcpy(dest.data() + part1, buf_.data(), toRead - part1);
        readPos_ = (readPos_ + toRead) % capacity_;
        size_ -= toRead;
        notFull_.notify_one();
        return toRead;
    }

    void Clear() {
        std::lock_guard lock(mtx_);
        readPos_ = writePos_ = size_ = 0;
    }

    void Close() {
        std::lock_guard lock(mtx_);
        closed_ = true;
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    void Reopen() {
        std::lock_guard lock(mtx_);
        closed_ = false;
    }

    [[nodiscard]] uint32_t Available() const {
        std::lock_guard lock(mtx_);
        return size_;
    }

    [[nodiscard]] uint32_t Free() const { return capacity_ - size_; }

    [[nodiscard]] bool IsClosed() const {
        std::lock_guard lock(mtx_);
        return closed_;
    }

private:
    std::vector<std::byte>     buf_;
    uint32_t                   capacity_;
    uint32_t                   readPos_  = 0;
    uint32_t                   writePos_ = 0;
    uint32_t                   size_     = 0;
    bool                       closed_   = false;
    mutable std::mutex         mtx_;
    std::condition_variable    notEmpty_;
    std::condition_variable    notFull_;
};

} // namespace libZFork
