#ifndef _WIN32
// Platform output factory for non-Windows.
// Prefer PortAudio when compiled in; otherwise NullOutput (silent).

#include "null_output.h"

#if defined(LIBZFORK_WITH_PORTAUDIO)
#  include "portaudio_output.h"
#endif

namespace libZFork {

std::unique_ptr<IAudioOutput> CreatePlatformAudioOutput(AudioBackendPreference /*pref*/) {
#if defined(LIBZFORK_WITH_PORTAUDIO)
    return std::make_unique<PortAudioOutput>();
#else
    return std::make_unique<NullOutput>();
#endif
}

} // namespace libZFork
#endif // !_WIN32
