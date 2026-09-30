# vcpkg custom triplet: arm64 Linux via aarch64-linux-gnu cross-compiler
# Static deps must be PIC so they can be linked into libZFork.so.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

set(VCPKG_C_FLAGS "-fPIC")
set(VCPKG_CXX_FLAGS "-fPIC")
set(VCPKG_CMAKE_CONFIGURE_OPTIONS "-DCMAKE_POSITION_INDEPENDENT_CODE=ON")

get_filename_component(_ZFORK_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}/../cmake" ABSOLUTE)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${_ZFORK_CMAKE_DIR}/toolchains/aarch64-linux-gnu.cmake")
