#include "broadcast_transport.h"
#include <array>
#include <cstring>
#include <string>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace libZFork {

namespace {
constexpr unsigned kShoutProtocolHttp = 0u;
constexpr unsigned kShoutProtocolIcy  = 2u;
constexpr unsigned kShoutFormatMp3    = 1u;

struct shout_t;
struct shout_metadata_t;

struct LibShoutApi {
#ifdef _WIN32
    HMODULE module = nullptr;
#else
    void* module = nullptr;
#endif
    int  (*shout_init)() = nullptr;
    void (*shout_shutdown)() = nullptr;
    shout_t* (*shout_new)() = nullptr;
    void (*shout_free)(shout_t*) = nullptr;
    int (*shout_set_host)(shout_t*, const char*) = nullptr;
    int (*shout_set_protocol)(shout_t*, unsigned) = nullptr;
    int (*shout_set_port)(shout_t*, unsigned short) = nullptr;
    int (*shout_set_password)(shout_t*, const char*) = nullptr;
    int (*shout_set_mount)(shout_t*, const char*) = nullptr;
    int (*shout_set_user)(shout_t*, const char*) = nullptr;
    int (*shout_set_format)(shout_t*, unsigned) = nullptr;
    int (*shout_set_name)(shout_t*, const char*) = nullptr;
    int (*shout_set_agent)(shout_t*, const char*) = nullptr;
    int (*shout_open)(shout_t*) = nullptr;
    int (*shout_close)(shout_t*) = nullptr;
    int (*shout_send)(shout_t*, const unsigned char*, size_t) = nullptr;
    void (*shout_sync)(shout_t*) = nullptr;
    const char* (*shout_get_error)(shout_t*) = nullptr;
    shout_metadata_t* (*shout_metadata_new)() = nullptr;
    int (*shout_metadata_add)(shout_metadata_t*, const char*, const char*) = nullptr;
    void (*shout_metadata_free)(shout_metadata_t*) = nullptr;
    int (*shout_set_metadata)(shout_t*, shout_metadata_t*) = nullptr;
};

template<typename T>
bool ResolveSymbol(LibShoutApi& api, const char* name, T& outFn) {
#ifdef _WIN32
    outFn = reinterpret_cast<T>(GetProcAddress(api.module, name));
#else
    outFn = reinterpret_cast<T>(dlsym(api.module, name));
#endif
    return outFn != nullptr;
}

bool LoadLibShout(LibShoutApi& api) {
#ifdef _WIN32
    constexpr std::array<const char*, 2> names = {"libshout.dll", "libshout-3.dll"};
    for (const char* n : names) {
        api.module = LoadLibraryA(n);
        if (api.module) break;
    }
#else
    constexpr std::array<const char*, 3> names = {"libshout.so", "libshout.so.3", "libshout.so.3.0.0"};
    for (const char* n : names) {
        api.module = dlopen(n, RTLD_LAZY | RTLD_LOCAL);
        if (api.module) break;
    }
#endif
    if (!api.module) return false;

    const bool requiredOk = ResolveSymbol(api, "shout_init", api.shout_init)
        && ResolveSymbol(api, "shout_shutdown", api.shout_shutdown)
        && ResolveSymbol(api, "shout_new", api.shout_new)
        && ResolveSymbol(api, "shout_free", api.shout_free)
        && ResolveSymbol(api, "shout_set_host", api.shout_set_host)
        && ResolveSymbol(api, "shout_set_protocol", api.shout_set_protocol)
        && ResolveSymbol(api, "shout_set_port", api.shout_set_port)
        && ResolveSymbol(api, "shout_set_password", api.shout_set_password)
        && ResolveSymbol(api, "shout_set_mount", api.shout_set_mount)
        && ResolveSymbol(api, "shout_set_user", api.shout_set_user)
        && ResolveSymbol(api, "shout_set_format", api.shout_set_format)
        && ResolveSymbol(api, "shout_set_name", api.shout_set_name)
        && ResolveSymbol(api, "shout_set_agent", api.shout_set_agent)
        && ResolveSymbol(api, "shout_open", api.shout_open)
        && ResolveSymbol(api, "shout_close", api.shout_close)
        && ResolveSymbol(api, "shout_send", api.shout_send)
        && ResolveSymbol(api, "shout_sync", api.shout_sync)
        && ResolveSymbol(api, "shout_get_error", api.shout_get_error);
    if (!requiredOk) return false;
    if (!api.shout_metadata_new) {
        (void)ResolveSymbol(api, "shout_metadata_new", api.shout_metadata_new);
        (void)ResolveSymbol(api, "shout_metadata_add", api.shout_metadata_add);
        (void)ResolveSymbol(api, "shout_metadata_free", api.shout_metadata_free);
        (void)ResolveSymbol(api, "shout_set_metadata", api.shout_set_metadata);
    }
    return true;
}

void UnloadLibShout(LibShoutApi& api) {
    if (!api.module) return;
#ifdef _WIN32
    FreeLibrary(api.module);
#else
    dlclose(api.module);
#endif
    api = {};
}

class LibShoutTransport final : public BroadcastTransport {
public:
    ~LibShoutTransport() override {
        Disconnect();
        if (api_.shout_shutdown) api_.shout_shutdown();
        if (stream_) api_.shout_free(stream_);
        UnloadLibShout(api_);
    }

    bool Connect(const BroadcastConfig& cfg) override {
        Disconnect();
        lastError_.clear();

        if (!api_.module && !LoadLibShout(api_)) {
            lastError_ = "libshout not found (copy libshout DLL/SO next to app or into system path)";
            return false;
        }
        if (!initialized_) {
            if (api_.shout_init() != 0) {
                lastError_ = "shout_init failed";
                return false;
            }
            initialized_ = true;
        }
        if (!stream_) stream_ = api_.shout_new();
        if (!stream_) {
            lastError_ = "shout_new failed";
            return false;
        }

        if (api_.shout_set_host(stream_, cfg.host) != 0) return Fail("shout_set_host");
        if (api_.shout_set_port(stream_, static_cast<unsigned short>(cfg.port)) != 0) return Fail("shout_set_port");
        if (api_.shout_set_user(stream_, cfg.user[0] ? cfg.user : "source") != 0) return Fail("shout_set_user");
        if (api_.shout_set_password(stream_, cfg.password) != 0) return Fail("shout_set_password");
        if (api_.shout_set_mount(stream_, cfg.mount[0] ? cfg.mount : "/stream") != 0) return Fail("shout_set_mount");
        const unsigned proto = (cfg.serverType == BroadcastServerType::Shoutcast) ? kShoutProtocolIcy : kShoutProtocolHttp;
        if (api_.shout_set_protocol(stream_, proto) != 0) return Fail("shout_set_protocol");
        if (api_.shout_set_format(stream_, kShoutFormatMp3) != 0) return Fail("shout_set_format");
        if (cfg.streamName[0] && api_.shout_set_name(stream_, cfg.streamName) != 0) return Fail("shout_set_name");
        (void)api_.shout_set_agent(stream_, "libZFork/1.0");

        if (api_.shout_open(stream_) != 0) return Fail("shout_open");
        connected_ = true;
        return true;
    }

    bool Send(std::span<const uint8_t> payload) override {
        if (!connected_ || !stream_) {
            lastError_ = "send while disconnected";
            return false;
        }
        if (payload.empty()) return true;
        if (api_.shout_send(stream_, payload.data(), payload.size()) != 0) return Fail("shout_send");
        api_.shout_sync(stream_);
        return true;
    }

    bool SetMetadata(const char* title, const char* artist, const char* album) override {
        if (!connected_ || !stream_) {
            lastError_ = "set metadata while disconnected";
            return false;
        }
        if (!api_.shout_metadata_new || !api_.shout_metadata_add || !api_.shout_metadata_free || !api_.shout_set_metadata) {
            lastError_ = "libshout metadata API unavailable";
            return false;
        }

        std::string nowPlaying;
        if (artist && artist[0] && title && title[0]) {
            nowPlaying = std::string(artist) + " - " + title;
        } else if (title && title[0]) {
            nowPlaying = title;
        } else if (album && album[0]) {
            nowPlaying = album;
        } else {
            nowPlaying = "Unknown";
        }

        shout_metadata_t* meta = api_.shout_metadata_new();
        if (!meta) {
            lastError_ = "shout_metadata_new failed";
            return false;
        }
        const int addRes = api_.shout_metadata_add(meta, "song", nowPlaying.c_str());
        if (addRes != 0) {
            api_.shout_metadata_free(meta);
            return Fail("shout_metadata_add");
        }
        const int setRes = api_.shout_set_metadata(stream_, meta);
        api_.shout_metadata_free(meta);
        if (setRes != 0) return Fail("shout_set_metadata");
        return true;
    }

    void Disconnect() override {
        if (connected_ && stream_ && api_.shout_close) api_.shout_close(stream_);
        connected_ = false;
    }

    bool IsConnected() const override { return connected_; }
    const char* GetLastError() const override { return lastError_.c_str(); }

private:
    bool Fail(const char* op) {
        connected_ = false;
        const char* s = (stream_ && api_.shout_get_error) ? api_.shout_get_error(stream_) : nullptr;
        lastError_ = s ? (std::string(op) + ": " + s) : std::string(op) + " failed";
        return false;
    }

    LibShoutApi api_{};
    shout_t* stream_ = nullptr;
    bool initialized_ = false;
    bool connected_ = false;
    std::string lastError_;
};
} // namespace

std::unique_ptr<BroadcastTransport> CreateBroadcastTransport() {
    return std::make_unique<LibShoutTransport>();
}

} // namespace libZFork

