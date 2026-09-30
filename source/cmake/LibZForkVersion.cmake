# Single source of truth: <repo>/VERSION  (e.g. "1.0.0")
# Included by the top-level CMakeLists.txt before project().

set(_libzfork_version_file "${CMAKE_CURRENT_LIST_DIR}/../VERSION")
if(NOT EXISTS "${_libzfork_version_file}")
    message(FATAL_ERROR "VERSION file missing: ${_libzfork_version_file}")
endif()

file(READ "${_libzfork_version_file}" _libzfork_version_raw)
string(STRIP "${_libzfork_version_raw}" LIBZFORK_VERSION)

if(NOT LIBZFORK_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR "VERSION must be MAJOR.MINOR.PATCH (got: '${LIBZFORK_VERSION}')")
endif()

set(LIBZFORK_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(LIBZFORK_VERSION_MINOR "${CMAKE_MATCH_2}")
set(LIBZFORK_VERSION_PATCH "${CMAKE_MATCH_3}")
set(LIBZFORK_VERSION_STRING "${LIBZFORK_VERSION}")
set(LIBZFORK_FILEVERSION "${LIBZFORK_VERSION_MAJOR},${LIBZFORK_VERSION_MINOR},${LIBZFORK_VERSION_PATCH},0")
set(LIBZFORK_FILEVERSION_STR "${LIBZFORK_VERSION}")

message(STATUS "libZFork version: ${LIBZFORK_VERSION}")
