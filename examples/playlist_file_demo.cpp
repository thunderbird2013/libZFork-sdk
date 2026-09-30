#include <libzfork/libzfork.h>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace libZFork;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("Usage: playlist_file_demo <playlist.m3u|m3u8|pls>\n");
        std::printf("Supports UTF-8 paths incl. emoji and long directories.\n");
        return 1;
    }

    ZFork* p = CreateZFork();
    if (!p) return 2;

    if (!p->LoadPlaylistFileA(argv[1], StreamFormat::Autodetect)) {
        std::printf("LoadPlaylistFileA failed: %s\n", p->GetError());
        DestroyZFork(p);
        return 3;
    }

    const uint32_t count = p->GetQueueCount();
    std::printf("Loaded playlist entries: %u\n", static_cast<unsigned>(count));
    if (count == 0) {
        DestroyZFork(p);
        return 4;
    }

    if (!p->Play()) {
        std::printf("Play failed: %s\n", p->GetError());
        DestroyZFork(p);
        return 5;
    }

    std::printf("Playing playlist... Ctrl+C to stop.\n");
    for (;;) {
        StreamStatus st = p->GetStatus();
        StreamTime pos{};
        p->GetPosition(pos);
        std::printf("\ridx=%d/%u playing=%d paused=%d pos=%u ms   ",
                    st.songIndex + 1,
                    static_cast<unsigned>(count),
                    st.playing ? 1 : 0,
                    st.paused ? 1 : 0,
                    pos.ms);
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    // Unreachable in this demo.
    p->Stop();
    DestroyZFork(p);
    return 0;
}

