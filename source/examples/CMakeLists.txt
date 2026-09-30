cmake_minimum_required(VERSION 3.25)

# ──────────────────────────────────────────────────────────────────────────────
# Helper: create one example executable linked against a specific libZFork target
# ──────────────────────────────────────────────────────────────────────────────
function(add_example NAME SOURCE LINK_TARGET)
    add_executable(${NAME} ${SOURCE})

    target_link_libraries(${NAME} PRIVATE ${LINK_TARGET})

    # Allow examples to include internal headers (core/, timeline/, engine/, …)
    target_include_directories(${NAME} PRIVATE "${LIBZFORK_ROOT}/src")

    set_target_properties(${NAME} PROPERTIES
        CXX_STANDARD          20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS        OFF
    )

    target_compile_options(${NAME} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W3 /utf-8>
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2 /Ob2 /Oi /Ot /GL /GS- /DNDEBUG>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
        $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:Release>>:-O3 -DNDEBUG>
    )

    target_link_options(${NAME} PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/LTCG /OPT:REF /OPT:ICF /DEBUG:NONE>
    )

    # Match the CRT of the library we're linking against
    if(VCPKG_TARGET_TRIPLET MATCHES "-static$")
        set_target_properties(${NAME} PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        set_target_properties(${NAME} PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endfunction()

# ──────────────────────────────────────────────────────────────────────────────
# Example sources
# ──────────────────────────────────────────────────────────────────────────────
set(EXAMPLE_SOURCES
    player_example.cpp
    fft_demo.cpp
    soundcard_test.cpp
    encoder_demo.cpp
    id3_reader.cpp
    playlist_demo.cpp
    playlist_file_demo.cpp
    equalizer_demo.cpp
    echo_demo.cpp
    dynamic_stream.cpp
    record_demo.cpp
    format_demo.cpp
    callback_demo.cpp
    broadcast_demo.cpp
    broadcast_stream_demo.cpp
    playlist_manager_demo.cpp
    custom_output_demo.cpp
    zfork2_basic.cpp
    zfork2_spatial.cpp
    zfork2_multivoice.cpp
    zfork2_soundbank_v1.cpp
    zfork2_dj_demo.cpp
    deck_manager_demo.cpp
    timeline_demo.cpp
    latency_probe.cpp
    zfork2_bank_builder.cpp
    zfork2_bank_compile.cpp
)

# Base names (used to derive executable names)
set(EXAMPLE_BASENAMES
    zfork_player
    fft_demo
    soundcard_test
    encoder_demo
    id3_reader
    playlist_demo
    playlist_file_demo
    equalizer_demo
    echo_demo
    dynamic_stream
    record_demo
    format_demo
    callback_demo
    broadcast_demo
    broadcast_stream_demo
    playlist_manager_demo
    custom_output_demo
    zfork2_basic
    zfork2_spatial
    zfork2_multivoice
    zfork2_soundbank_v1
    zfork2_dj_demo
    deck_manager_demo
    timeline_demo
    latency_probe
    zfork2_bank_builder
    zfork2_bank_compile
)

# ──────────────────────────────────────────────────────────────────────────────
# Examples that use internal C++ classes (not exported via .def) —
# these can only be built against the static lib.
# ──────────────────────────────────────────────────────────────────────────────
set(STATIC_ONLY_EXAMPLES
    deck_manager_demo
    timeline_demo
)

# ──────────────────────────────────────────────────────────────────────────────
# Build each example against every available library variant
# ──────────────────────────────────────────────────────────────────────────────
list(LENGTH EXAMPLE_SOURCES _len)
math(EXPR _last "${_len} - 1")

foreach(_idx RANGE ${_last})
    list(GET EXAMPLE_SOURCES   ${_idx} _src)
    list(GET EXAMPLE_BASENAMES ${_idx} _base)

    list(FIND STATIC_ONLY_EXAMPLES "${_base}" _static_only_idx)
    set(_static_only FALSE)
    if(_static_only_idx GREATER_EQUAL 0)
        set(_static_only TRUE)
    endif()

    # ── DLL-Variante: foo.exe  (kein _dll-Suffix)
    if(TARGET libZFork::shared AND NOT _static_only)
        add_example("${_base}" "${_src}" libZFork::shared)
        if(_base STREQUAL "soundcard_test")
            target_link_libraries(soundcard_test PRIVATE winmm)
        endif()
    endif()

    # ── Statische Variante: foo_static.exe
    if(TARGET libZFork::static)
        add_example("${_base}_static" "${_src}" libZFork::static)
        if(_base STREQUAL "soundcard_test")
            target_link_libraries(soundcard_test_static PRIVATE winmm)
        endif()
    endif()

    # ── Fallback: kein DLL verfügbar → bare name = static
    if(NOT TARGET libZFork::shared AND TARGET libZFork::static AND NOT _static_only)
        add_example("${_base}" "${_src}" libZFork::static)
    endif()
endforeach()

# ──────────────────────────────────────────────────────────────────────────────
# linux_output_demo — external PCM callback -> PortAudio output
# Linux/Unix only (demonstrates custom external audio sink path).
# ──────────────────────────────────────────────────────────────────────────────
if(UNIX)
    set(_have_portaudio FALSE)
    if(TARGET portaudio::portaudio OR TARGET portaudio_static OR TARGET portaudio)
        set(_have_portaudio TRUE)
    endif()

    if(TARGET libZFork::shared)
        add_example("linux_output_demo" "linux_output_demo.cpp" libZFork::shared)
        if(_have_portaudio)
            target_compile_definitions(linux_output_demo PRIVATE LIBZFORK_HAVE_PORTAUDIO_OUT=1)
            if(TARGET portaudio::portaudio)
                target_link_libraries(linux_output_demo PRIVATE portaudio::portaudio)
            elseif(TARGET portaudio_static)
                target_link_libraries(linux_output_demo PRIVATE portaudio_static)
            else()
                target_link_libraries(linux_output_demo PRIVATE portaudio)
            endif()
        endif()
    endif()

    if(TARGET libZFork::static)
        add_example("linux_output_demo_static" "linux_output_demo.cpp" libZFork::static)
        if(_have_portaudio)
            target_compile_definitions(linux_output_demo_static PRIVATE LIBZFORK_HAVE_PORTAUDIO_OUT=1)
            if(TARGET portaudio::portaudio)
                target_link_libraries(linux_output_demo_static PRIVATE portaudio::portaudio)
            elseif(TARGET portaudio_static)
                target_link_libraries(linux_output_demo_static PRIVATE portaudio_static)
            else()
                target_link_libraries(linux_output_demo_static PRIVATE portaudio)
            endif()
        endif()
    endif()

    if(TARGET libZFork::shared)
        add_example("linux_features_demo" "linux_features_demo.cpp" libZFork::shared)
        if(_have_portaudio)
            target_compile_definitions(linux_features_demo PRIVATE LIBZFORK_HAVE_PORTAUDIO_OUT=1)
            if(TARGET portaudio::portaudio)
                target_link_libraries(linux_features_demo PRIVATE portaudio::portaudio)
            elseif(TARGET portaudio_static)
                target_link_libraries(linux_features_demo PRIVATE portaudio_static)
            else()
                target_link_libraries(linux_features_demo PRIVATE portaudio)
            endif()
        endif()
    endif()

    if(TARGET libZFork::static)
        add_example("linux_features_demo_static" "linux_features_demo.cpp" libZFork::static)
        if(_have_portaudio)
            target_compile_definitions(linux_features_demo_static PRIVATE LIBZFORK_HAVE_PORTAUDIO_OUT=1)
            if(TARGET portaudio::portaudio)
                target_link_libraries(linux_features_demo_static PRIVATE portaudio::portaudio)
            elseif(TARGET portaudio_static)
                target_link_libraries(linux_features_demo_static PRIVATE portaudio_static)
            else()
                target_link_libraries(linux_features_demo_static PRIVATE portaudio)
            endif()
        endif()
    endif()

    if(TARGET libZFork::shared)
        add_example("linux_internal_player_demo" "linux_internal_player_demo.cpp" libZFork::shared)
        if(_have_portaudio)
            target_compile_definitions(linux_internal_player_demo PRIVATE LIBZFORK_HAVE_PORTAUDIO_OUT=1)
            if(TARGET portaudio::portaudio)
                target_link_libraries(linux_internal_player_demo PRIVATE portaudio::portaudio)
            elseif(TARGET portaudio_static)
                target_link_libraries(linux_internal_player_demo PRIVATE portaudio_static)
            else()
                target_link_libraries(linux_internal_player_demo PRIVATE portaudio)
            endif()
        endif()
    endif()
    if(TARGET libZFork::static)
        add_example("linux_internal_player_demo_static" "linux_internal_player_demo.cpp" libZFork::static)
        if(_have_portaudio)
            target_compile_definitions(linux_internal_player_demo_static PRIVATE LIBZFORK_HAVE_PORTAUDIO_OUT=1)
            if(TARGET portaudio::portaudio)
                target_link_libraries(linux_internal_player_demo_static PRIVATE portaudio::portaudio)
            elseif(TARGET portaudio_static)
                target_link_libraries(linux_internal_player_demo_static PRIVATE portaudio_static)
            else()
                target_link_libraries(linux_internal_player_demo_static PRIVATE portaudio)
            endif()
        endif()
    endif()
endif()

# ──────────────────────────────────────────────────────────────────────────────
# spectrum_gui — Win32 GUI Spectrum Analyzer (kein Konsolenfenster)
# ──────────────────────────────────────────────────────────────────────────────
if(WIN32)
    # ── spectrum_gui.exe — DLL-gelinkt ────────────────────────────────────────
    if(TARGET libZFork::shared)
        add_executable(spectrum_gui WIN32 spectrum_gui.cpp)
        target_link_libraries(spectrum_gui PRIVATE libZFork::shared Shell32)
        target_include_directories(spectrum_gui PRIVATE "${LIBZFORK_ROOT}/src")
        set_target_properties(spectrum_gui PROPERTIES
            CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
        target_compile_options(spectrum_gui PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/W3 /utf-8>
            $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2 /GL /GS- /DNDEBUG>)
        target_link_options(spectrum_gui PRIVATE
            $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/LTCG /OPT:REF /OPT:ICF>)
        if(VCPKG_TARGET_TRIPLET MATCHES "-static$")
            set_target_properties(spectrum_gui PROPERTIES
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        else()
            set_target_properties(spectrum_gui PROPERTIES
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        endif()
    endif()

    # ── spectrum_gui_static.exe — vollständig statisch ────────────────────────
    if(TARGET libZFork::static)
        add_executable(spectrum_gui_static WIN32 spectrum_gui.cpp)
        target_link_libraries(spectrum_gui_static PRIVATE libZFork::static Shell32)
        target_include_directories(spectrum_gui_static PRIVATE "${LIBZFORK_ROOT}/src")
        set_target_properties(spectrum_gui_static PROPERTIES
            CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
        target_compile_options(spectrum_gui_static PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/W3 /utf-8>
            $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2 /GL /GS- /DNDEBUG>)
        target_link_options(spectrum_gui_static PRIVATE
            $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/LTCG /OPT:REF /OPT:ICF>)
        if(VCPKG_TARGET_TRIPLET MATCHES "-static$")
            set_target_properties(spectrum_gui_static PROPERTIES
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        else()
            set_target_properties(spectrum_gui_static PROPERTIES
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        endif()
    endif()
endif()

# ──────────────────────────────────────────────────────────────────────────────
# imgui_studio — kombinierter DeckManager + Timeline GUI (Win32 + DX11 + ImGui)
# Nur gebaut wenn imgui gefunden wird und libZFork::static verfügbar ist.
# ──────────────────────────────────────────────────────────────────────────────
find_package(imgui CONFIG QUIET)

if(WIN32 AND imgui_FOUND AND TARGET libZFork::static)
    add_executable(imgui_studio WIN32 imgui_studio.cpp)

    target_link_libraries(imgui_studio PRIVATE
        libZFork::static
        imgui::imgui
        d3d11
        dxgi
        comdlg32
    )

    target_include_directories(imgui_studio PRIVATE "${LIBZFORK_ROOT}/src")
    target_compile_definitions(imgui_studio PRIVATE _CRT_SECURE_NO_WARNINGS)

    set_target_properties(imgui_studio PROPERTIES
        CXX_STANDARD          20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS        OFF
    )

    target_compile_options(imgui_studio PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W3 /utf-8>
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2 /GL /GS- /DNDEBUG>
    )
    target_link_options(imgui_studio PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/LTCG /OPT:REF /OPT:ICF /DEBUG:NONE>
    )

    if(VCPKG_TARGET_TRIPLET MATCHES "-static$")
        set_target_properties(imgui_studio PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        set_target_properties(imgui_studio PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()

    message(STATUS "imgui_studio: ENABLED (Win32 + DX11 + Dear ImGui)")
elseif(NOT imgui_FOUND)
    message(STATUS "imgui_studio: SKIPPED — imgui nicht installiert (vcpkg install imgui[win32-binding,dx11-binding])")
elseif(NOT TARGET libZFork::static)
    message(STATUS "imgui_studio: SKIPPED — libZFork::static nicht verfügbar")
endif()
