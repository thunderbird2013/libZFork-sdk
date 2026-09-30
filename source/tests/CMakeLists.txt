cmake_minimum_required(VERSION 3.25)

# ── Output-backend unit tests ────────────────────────────────────────────────
add_executable(zfork_tests
    test_audio_output.cpp
    test_player_lifecycle.cpp
    test_engine_phase2.cpp
    test_engine_phase3.cpp
    test_engine_phase4.cpp
    test_engine_phase5.cpp
    test_soundbank.cpp
)

# Tests link the static lib so they run without deploying the DLL and have
# access to all internal C++ symbols (Player, VoicePool, MixerGraph, …).
# The static lib target exists when build.ps1 passes -DllStatic ON for the
# dll-static variant (or when building with -DLIBZFORK_BUILD_STATIC=ON).
if(TARGET libZFork_static)
    target_link_libraries(zfork_tests PRIVATE libZFork::static)
elseif(TARGET libZFork_shared)
    # Fallback: shared lib only exports the 25 C-API symbols via .def —
    # internal classes (Player, VoicePool …) are not visible.  This will
    # cause linker errors for the unit tests.  Prefer building with static.
    message(WARNING
        "zfork_tests: libZFork_static not found, falling back to shared lib. "
        "Internal C++ symbols will be unresolved. "
        "Run build.ps1 with -DllStatic ON for the test variant.")
    target_link_libraries(zfork_tests PRIVATE libZFork::shared)
else()
    message(FATAL_ERROR "libZFork target not found — build tests together with the library")
endif()

target_link_libraries(zfork_tests PRIVATE Catch2::Catch2WithMain)

# Pull in all third-party deps (SoundTouch, Vorbis, FLAC, mpg123, …) that the
# static lib's .obj files reference.  libzfork_configure_target() is defined in
# the parent CMakeLists.txt and wires up includes, defines, runtime lib, and
# every vcpkg dependency with the same settings as the library itself.
libzfork_configure_target(zfork_tests)

# libzfork_configure_target uses CMAKE_CURRENT_SOURCE_DIR which resolves to
# the tests/ subdirectory when called from here.  Override with the absolute
# repo-root paths so that "engine/voice.h", "core/player.h", etc. are found.
target_include_directories(zfork_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

# Additional test-only define so headers know we're linking statically.
target_compile_definitions(zfork_tests PRIVATE LIBZFORK_STATIC)

# Register with CTest
include(CTest)
include(Catch)
catch_discover_tests(zfork_tests)
