#!/usr/bin/env bash
# Build libZFork for Linux (native x64 or cross arm64), Debug or Release.
# Usage:
#   wsl_build_variant.sh x64 Release|Debug
#   wsl_build_variant.sh arm64 Release|Debug
set -euo pipefail

ARCH="${1:?arch: x64|arm64}"
CFG="${2:?config: Release|Debug}"
# Repo-Root = Ordner ueber scripts/ (funktioniert lokal, WSL und in dist/source)
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VCPKG_ROOT="${VCPKG_ROOT:-${HOME}/vcpkg}"

case "${ARCH}" in
  x64)
    TRIPLET="x64-linux"
    BUILD_DIR="${ROOT}/build/wsl-linux-${CFG,,}"
    EXTRA_CMAKE=()
    ;;
  arm64)
    TRIPLET="arm64-linux-cross"
    BUILD_DIR="${ROOT}/build/wsl-linux-arm64-${CFG,,}"
    CUSTOM_TRIPLETS="${ROOT}/triplets"
    OVERLAY_PORTS="${ROOT}/overlays"
    TOOLCHAIN="${ROOT}/cmake/toolchains/aarch64-linux-gnu.cmake"
    export VCPKG_OVERLAY_TRIPLETS="${CUSTOM_TRIPLETS}"
    export VCPKG_OVERLAY_PORTS="${OVERLAY_PORTS}"
    EXTRA_CMAKE=(
      -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="${TOOLCHAIN}"
      -DVCPKG_OVERLAY_TRIPLETS="${CUSTOM_TRIPLETS}"
      -DVCPKG_OVERLAY_PORTS="${OVERLAY_PORTS}"
      -DCMAKE_SYSTEM_NAME=Linux
      -DCMAKE_SYSTEM_PROCESSOR=aarch64
    )
    command -v aarch64-linux-gnu-g++ >/dev/null \
      || { echo "ERROR: aarch64-linux-gnu-g++ fehlt"; exit 1; }
    ;;
  *)
    echo "Usage: $0 x64|arm64 Release|Debug"; exit 1
    ;;
esac

export VCPKG_ROOT
export VCPKG_DEFAULT_TRIPLET="${TRIPLET}"
mkdir -p "${BUILD_DIR}"

echo "==> ${ARCH} / ${CFG}  triplet=${TRIPLET}"
cmake -S "${ROOT}" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE="${CFG}" \
  -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET="${TRIPLET}" \
  -DLIBZFORK_BUILD_SHARED=ON \
  -DLIBZFORK_BUILD_STATIC=ON \
  -DLIBZFORK_BUILD_EXAMPLES=OFF \
  -DLIBZFORK_BUILD_TESTS=OFF \
  -DLIBZFORK_BUILD_APPS=OFF \
  "${EXTRA_CMAKE[@]}"

cmake --build "${BUILD_DIR}" --parallel "$(nproc)"
echo "==> OK ${BUILD_DIR}"
ls -lh "${BUILD_DIR}"/libZFork* 2>/dev/null || true
