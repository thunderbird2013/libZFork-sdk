#pragma once
#include <libzfork/libzfork.h>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace libZFork {

class BroadcastTransport {
public:
    virtual ~BroadcastTransport() = default;
    [[nodiscard]] virtual bool Connect(const BroadcastConfig& cfg) = 0;
    [[nodiscard]] virtual bool Send(std::span<const uint8_t> payload) = 0;
    [[nodiscard]] virtual bool SetMetadata(const char* title, const char* artist, const char* album) = 0;
    virtual void Disconnect() = 0;
    [[nodiscard]] virtual bool IsConnected() const = 0;
    [[nodiscard]] virtual const char* GetLastError() const = 0;
};

std::unique_ptr<BroadcastTransport> CreateBroadcastTransport();

} // namespace libZFork

