#if defined(__ANDROID__)
// Static codec libs (FLAC, mpg123, lame) reference stdin/stdout/stderr.
// When linking them into a shared library, force those symbols to be resolved from libc.
#include <stdio.h>

namespace {
struct AndroidStdioAnchor {
    AndroidStdioAnchor() noexcept {
        (void)stdin;
        (void)stdout;
        (void)stderr;
    }
};
const AndroidStdioAnchor kAndroidStdioAnchor{};
} // namespace
#endif
