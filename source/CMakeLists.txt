cmake_minimum_required(VERSION 3.25)

# Version is read from the top-level VERSION file (MAJOR.MINOR.PATCH).
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/LibZForkVersion.cmake")

project(libZFork
    VERSION ${LIBZFORK_VERSION}
    DESCRIPTION "Modern C++20 audio playback and processing library"
    LANGUAGES CXX C
)

# Keep committed generated headers in sync for IDEs / non-CMake consumers.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libzfork_version.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/libzfork/libzfork_version.h"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libzfork_version.rc.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/libzfork_version.rc.h"
    @ONLY
)

# ── C++20 ─────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Options ───────────────────────────────────────────────────────────────────
option(LIBZFORK_BUILD_SHARED   "Build shared library (DLL)"  ON)
option(LIBZFORK_BUILD_STATIC   "Build static library"        ON)
option(LIBZFORK_BUILD_EXAMPLES "Build example applications"  ON)
option(LIBZFORK_BUILD_TESTS    "Build Catch2 test suite"     ON)
option(LIBZFORK_BUILD_APPS     "Build standalone applications" ON)
option(LIBZFORK_ENABLE_MP3     "Enable MP3 support (mpg123)" ON)
option(LIBZFORK_ENABLE_OGG     "Enable OGG Vorbis support"   ON)
option(LIBZFORK_ENABLE_FLAC    "Enable FLAC support"         ON)
option(LIBZFORK_ENABLE_AAC     "Enable AAC support (faad2)"  ON)
option(LIBZFORK_ENABLE_MP3ENC  "Enable MP3 encoding (LAME)"  ON)
option(LIBZFORK_ENABLE_OPUS    "Enable Opus support (opusfile)" ON)
option(LIBZFORK_ENABLE_BROADCAST "Enable Icecast/Shoutcast broadcast streaming" ON)
option(LIBZFORK_ENABLE_PORTAUDIO "Enable PortAudio mic capture backend" ON)

# ── vcpkg dependencies ─────────────────────────────────────────────────────────
find_package(mpg123   CONFIG REQUIRED)
find_package(Vorbis   CONFIG REQUIRED)
find_package(FLAC     CONFIG REQUIRED)
find_package(SoundTouch CONFIG REQUIRED)
find_package(JPEG           REQUIRED)
find_package(PNG            REQUIRED)
find_package(ZLIB           REQUIRED)
if(LIBZFORK_ENABLE_PORTAUDIO)
    find_package(portaudio CONFIG QUIET)
    if(NOT portaudio_FOUND)
        find_library(PORTAUDIO_LIBRARY NAMES portaudio portaudio_static)
        find_path(PORTAUDIO_INCLUDE_DIR NAMES portaudio.h)
        if(PORTAUDIO_LIBRARY AND PORTAUDIO_INCLUDE_DIR)
            add_library(portaudio::portaudio UNKNOWN IMPORTED)
            set_target_properties(portaudio::portaudio PROPERTIES
                IMPORTED_LOCATION "${PORTAUDIO_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${PORTAUDIO_INCLUDE_DIR}"
            )
            set(portaudio_FOUND TRUE)
        endif()
    endif()
    if(NOT portaudio_FOUND)
        message(WARNING "LIBZFORK_ENABLE_PORTAUDIO=ON but portaudio not found; disabling PortAudio backend")
        set(LIBZFORK_ENABLE_PORTAUDIO OFF)
    endif()
endif()

# faad2 — unofficial port, may use module-find
find_package(faad2 CONFIG QUIET)
if(NOT faad2_FOUND)
    find_library(FAAD2_LIBRARY NAMES faad2 faad REQUIRED)
    find_path(FAAD2_INCLUDE_DIR NAMES faad.h neaacdec.h REQUIRED)
    add_library(faad2::faad2 UNKNOWN IMPORTED)
    set_target_properties(faad2::faad2 PROPERTIES
        IMPORTED_LOCATION "${FAAD2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FAAD2_INCLUDE_DIR}"
    )
endif()

# opusfile
find_package(OpusFile CONFIG QUIET)
if(NOT OpusFile_FOUND)
    find_library(OPUSFILE_LIBRARY NAMES opusfile REQUIRED)
    find_path(OPUSFILE_INCLUDE_DIR NAMES opusfile.h opus/opusfile.h REQUIRED)
    add_library(OpusFile::opusfile UNKNOWN IMPORTED)
    set_target_properties(OpusFile::opusfile PROPERTIES
        IMPORTED_LOCATION "${OPUSFILE_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPUSFILE_INCLUDE_DIR}"
    )
endif()

# mp3lame
find_package(mp3lame CONFIG QUIET)
if(NOT mp3lame_FOUND)
    find_library(LAME_LIBRARY NAMES mp3lame libmp3lame REQUIRED)
    find_path(LAME_INCLUDE_DIR NAMES lame/lame.h REQUIRED)
    add_library(mp3lame::mp3lame UNKNOWN IMPORTED)
    set_target_properties(mp3lame::mp3lame PROPERTIES
        IMPORTED_LOCATION "${LAME_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LAME_INCLUDE_DIR}"
    )
endif()

# ── Source files ───────────────────────────────────────────────────────────────
set(LIBZFORK_SOURCES
    src/output/waveout_output.cpp
    src/output/wasapi_output.cpp
    src/output/auto_output.cpp
    src/output/null_output.cpp
    src/output/portaudio_output.cpp
    src/engine/voice.cpp
    src/engine/voice_pool.cpp
    src/engine/channel_group.cpp
    src/engine/mixer_graph.cpp
    src/engine/audio_context.cpp
    src/engine/sound.cpp
    src/engine/sound_cache.cpp
    src/engine/soundbank_v1.cpp
    src/engine/spatial.cpp
    src/engine/api2.cpp
    src/core/player.cpp
    src/core/deck_manager.cpp
    src/core/api.cpp
    src/timeline/timeline_player.cpp
    src/decoders/decoder_base.cpp
    src/decoders/wav_decoder.cpp
    src/decoders/pcm_decoder.cpp
    src/decoders/aiff_decoder.cpp
    src/decoders/cda_decoder.cpp
    src/decoders/decoder_factory.cpp
    src/encoders/wav_encoder.cpp
    src/effects/equalizer.cpp
    src/effects/echo.cpp
    src/effects/reverb.cpp
    src/effects/soundtouch_proc.cpp
    src/effects/center_cut.cpp
    src/effects/stereo_enhancer.cpp
    src/analysis/fft.cpp
    src/analysis/spectrum.cpp
    src/utils/tags.cpp
    src/utils/musical_key.cpp
    src/utils/tools.cpp
    src/utils/wavelet.cpp
)

if(LIBZFORK_ENABLE_BROADCAST)
    list(APPEND LIBZFORK_SOURCES
        src/streaming/broadcast_libshout_transport.cpp
        src/streaming/mp3_broadcast_encoder.cpp
    )
endif()

if(LIBZFORK_ENABLE_MP3)
    list(APPEND LIBZFORK_SOURCES src/decoders/mp3_decoder.cpp)
endif()
if(LIBZFORK_ENABLE_OGG)
    list(APPEND LIBZFORK_SOURCES
        src/decoders/ogg_decoder.cpp
        src/encoders/ogg_encoder.cpp
    )
endif()
if(LIBZFORK_ENABLE_FLAC)
    list(APPEND LIBZFORK_SOURCES
        src/decoders/flac_decoder.cpp
        src/decoders/flac_ogg_decoder.cpp
        src/encoders/flac_encoder.cpp
    )
endif()
if(LIBZFORK_ENABLE_OPUS)
    list(APPEND LIBZFORK_SOURCES src/decoders/opus_decoder.cpp)
endif()
if(LIBZFORK_ENABLE_AAC)
    list(APPEND LIBZFORK_SOURCES
        src/decoders/aac_decoder.cpp
        src/decoders/m4a_decoder.cpp
        src/third_party/mp4read/mp4read.c
    )
endif()
if(LIBZFORK_ENABLE_MP3ENC)
    list(APPEND LIBZFORK_SOURCES src/encoders/mp3_encoder.cpp)
endif()

if(ANDROID)
    list(APPEND LIBZFORK_SOURCES src/utils/android_stdio_fix.cpp)
endif()

set(LIBZFORK_PUBLIC_HEADERS
    include/libzfork/libzfork.h
)

# ── Helper: apply common settings to a target ─────────────────────────────────
# LIBZFORK_ROOT is captured here (top-level CMakeLists) so the function works
# correctly even when called from a subdirectory (CMAKE_CURRENT_SOURCE_DIR
# changes as CMake descends into add_subdirectory).
set(LIBZFORK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

function(libzfork_configure_target TARGET_NAME)
    target_include_directories(${TARGET_NAME}
        PUBLIC
            $<BUILD_INTERFACE:${LIBZFORK_ROOT}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${LIBZFORK_ROOT}/src
    )

    target_link_libraries(${TARGET_NAME} PRIVATE
        Vorbis::vorbis
        Vorbis::vorbisfile
        Vorbis::vorbisenc
        FLAC::FLAC
        FLAC::FLAC++
        SoundTouch::SoundTouch
        JPEG::JPEG
        PNG::PNG
        ZLIB::ZLIB
    )

    if(TARGET MPG123::libmpg123)
        target_link_libraries(${TARGET_NAME} PRIVATE MPG123::libmpg123)
    elseif(TARGET mpg123)
        target_link_libraries(${TARGET_NAME} PRIVATE mpg123)
    endif()

    if(TARGET faad2::faad2)
        target_link_libraries(${TARGET_NAME} PRIVATE faad2::faad2)
    endif()

    if(TARGET mp3lame::mp3lame)
        target_link_libraries(${TARGET_NAME} PRIVATE mp3lame::mp3lame)
    endif()

    if(LIBZFORK_ENABLE_OPUS AND TARGET OpusFile::opusfile)
        target_link_libraries(${TARGET_NAME} PRIVATE OpusFile::opusfile)
    endif()
    if(LIBZFORK_ENABLE_PORTAUDIO)
        if(TARGET portaudio::portaudio)
            target_link_libraries(${TARGET_NAME} PRIVATE portaudio::portaudio)
        elseif(TARGET portaudio_static)
            target_link_libraries(${TARGET_NAME} PRIVATE portaudio_static)
        elseif(TARGET portaudio)
            target_link_libraries(${TARGET_NAME} PRIVATE portaudio)
        endif()
    endif()

    target_compile_definitions(${TARGET_NAME} PRIVATE
        $<$<BOOL:${LIBZFORK_ENABLE_MP3}>:LIBZFORK_WITH_MP3>
        $<$<BOOL:${LIBZFORK_ENABLE_OGG}>:LIBZFORK_WITH_OGG>
        $<$<BOOL:${LIBZFORK_ENABLE_FLAC}>:LIBZFORK_WITH_FLAC>
        $<$<BOOL:${LIBZFORK_ENABLE_AAC}>:LIBZFORK_WITH_AAC>
        $<$<BOOL:${LIBZFORK_ENABLE_MP3ENC}>:LIBZFORK_WITH_MP3ENC>
        $<$<BOOL:${LIBZFORK_ENABLE_OPUS}>:LIBZFORK_WITH_OPUS>
        $<$<BOOL:${LIBZFORK_ENABLE_BROADCAST}>:LIBZFORK_WITH_BROADCAST>
        $<$<BOOL:${LIBZFORK_ENABLE_PORTAUDIO}>:LIBZFORK_WITH_PORTAUDIO>
        $<$<PLATFORM_ID:Windows>:UNICODE>
        $<$<PLATFORM_ID:Windows>:_UNICODE>
        $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
        $<$<PLATFORM_ID:Windows>:NOMINMAX>
    )

    # ── CRT selection: match what vcpkg triplet expects ────────────────────────
    # x64-windows          → /MD  (dynamic CRT, dynamic vcpkg libs)
    # x64-windows-static-md→ /MD  (dynamic CRT, static vcpkg libs)  ← DLL mode
    # x64-windows-static   → /MT  (static CRT,  static vcpkg libs)  ← LIB mode
    if(VCPKG_TARGET_TRIPLET MATCHES "-static$")
        # fully static: /MT release, /MTd debug
        set_target_properties(${TARGET_NAME} PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        # dynamic CRT: /MD release, /MDd debug (default for MSVC)
        set_target_properties(${TARGET_NAME} PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()

    target_compile_options(${TARGET_NAME} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /Zc:__cplusplus /utf-8>
        # Hard Release: full optimisation, WPO, no debug info
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2 /Ob2 /Oi /Ot /GL /GS- /DNDEBUG>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
        $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:Release>>:-O3 -DNDEBUG>
    )

    # Hard Release linker flags: LTCG, dead-code stripping, no PDB
    target_link_options(${TARGET_NAME} PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/LTCG /OPT:REF /OPT:ICF /DEBUG:NONE>
    )

    set_target_properties(${TARGET_NAME} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
        # Suppress PDB generation for Release
        COMPILE_PDB_NAME_RELEASE ""
    )
endfunction()

# ── Shared library (DLL) ───────────────────────────────────────────────────────
if(LIBZFORK_BUILD_SHARED)
    add_library(libZFork_shared SHARED ${LIBZFORK_SOURCES})
    add_library(libZFork::shared ALIAS libZFork_shared)

    set_target_properties(libZFork_shared PROPERTIES
        OUTPUT_NAME "libZFork"
        VERSION     ${PROJECT_VERSION}
        SOVERSION   ${PROJECT_VERSION_MAJOR}
        DEBUG_POSTFIX "d"
    )
    if(NOT WIN32)
        set_target_properties(libZFork_shared PROPERTIES PREFIX "")
    endif()

    target_compile_definitions(libZFork_shared
        PRIVATE LIBZFORK_BUILDING_DLL
        PUBLIC  LIBZFORK_DLL
    )

    # DEF file controls the export table — only the 25 public C symbols are exported.
    # This prevents mangled C++ names, vtable thunks and internal ordinals from leaking.
    if(MSVC)
        target_link_options(libZFork_shared PRIVATE
            "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/libzfork.def"
        )
    endif()

    # Windows-Dateieigenschaften (Name, Copyright, Version, Sprache Deutsch) + Icon
    if(WIN32)
        set(_libzfork_rc "${CMAKE_CURRENT_SOURCE_DIR}/src/libzfork.rc")
        set(_libzfork_ico "${CMAKE_CURRENT_SOURCE_DIR}/src/libzfork.ico")
        target_sources(libZFork_shared PRIVATE "${_libzfork_rc}")
        set_source_files_properties("${_libzfork_rc}" PROPERTIES
            OBJECT_DEPENDS "${_libzfork_ico}"
        )
    endif()

    libzfork_configure_target(libZFork_shared)

    if(ANDROID)
        target_link_libraries(libZFork_shared PRIVATE m atomic dl log)
        # Static codec .a files reference stdio symbols resolved from libc.so at load time.
        target_link_options(libZFork_shared PRIVATE
            "SHELL:-Wl,--allow-shlib-undefined"
            "SHELL:-lc"
        )
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(libZFork_shared PRIVATE c m dl pthread)
    endif()
endif()

# ── Static library ─────────────────────────────────────────────────────────────
if(LIBZFORK_BUILD_STATIC)
    add_library(libZFork_static STATIC ${LIBZFORK_SOURCES})
    add_library(libZFork::static ALIAS libZFork_static)

    set_target_properties(libZFork_static PROPERTIES
        OUTPUT_NAME "libZFork_static"
        DEBUG_POSTFIX "d"
    )
    if(NOT WIN32)
        set_target_properties(libZFork_static PROPERTIES PREFIX "")
    endif()

    target_compile_definitions(libZFork_static
        PUBLIC LIBZFORK_STATIC
    )

    libzfork_configure_target(libZFork_static)
endif()

# ── Examples ───────────────────────────────────────────────────────────────────
if(LIBZFORK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ── Standalone apps ────────────────────────────────────────────────────────────
if(LIBZFORK_BUILD_APPS)
    add_subdirectory(apps)
endif()

# ── Tests ──────────────────────────────────────────────────────────────────────
if(LIBZFORK_BUILD_TESTS)
    find_package(Catch2 3 CONFIG REQUIRED)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Install ────────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(LIBZFORK_BUILD_SHARED)
    install(TARGETS libZFork_shared
        EXPORT  libZForkTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

if(LIBZFORK_BUILD_STATIC)
    install(TARGETS libZFork_static
        EXPORT  libZForkTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

install(FILES ${LIBZFORK_PUBLIC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libzfork
)

install(EXPORT libZForkTargets
    FILE      libZForkTargets.cmake
    NAMESPACE libZFork::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libZFork
)

configure_package_config_file(
    cmake/libZForkConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/libZForkConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libZFork
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libZForkConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libZForkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/libZForkConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libZFork
)
