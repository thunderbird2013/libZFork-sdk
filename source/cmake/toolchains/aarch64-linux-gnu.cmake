# Cross-toolchain: host x86_64 → target aarch64-linux-gnu (Ubuntu multiarch)
# Used with vcpkg (CHAINLOAD). Find modes must be BOTH so vcpkg_installed
# packages remain visible while still preferring the aarch64 sysroot.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_ASM_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_AR           aarch64-linux-gnu-ar CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB       aarch64-linux-gnu-ranlib CACHE FILEPATH "" FORCE)
set(CMAKE_STRIP        aarch64-linux-gnu-strip CACHE FILEPATH "" FORCE)

list(APPEND CMAKE_FIND_ROOT_PATH
    /usr/aarch64-linux-gnu
    /usr/lib/aarch64-linux-gnu
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

set(CMAKE_CROSSCOMPILING TRUE)

# pkg-config should not pick host x86_64 .pc files during cross builds.
if(NOT DEFINED ENV{PKG_CONFIG_LIBDIR} OR "$ENV{PKG_CONFIG_LIBDIR}" STREQUAL "")
    set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/aarch64-linux-gnu/pkgconfig")
endif()
