#!/usr/bin/env bash
# Cross-build libZFork for Linux aarch64 (arm64) from x86_64 WSL/Ubuntu.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VCPKG_ROOT="${VCPKG_ROOT:-${HOME}/vcpkg}"
TRIPLET="arm64-linux-cross"
BUILD_DIR="${ROOT}/build/wsl-linux-arm64"
DIST_DIR="${ROOT}/dist/linux-arm64"
TOOLCHAIN="${ROOT}/cmake/toolchains/aarch64-linux-gnu.cmake"
CUSTOM_TRIPLETS="${ROOT}/triplets"
OVERLAY_PORTS="${ROOT}/overlays"

export VCPKG_ROOT
export VCPKG_OVERLAY_TRIPLETS="${CUSTOM_TRIPLETS}"
export VCPKG_OVERLAY_PORTS="${OVERLAY_PORTS}"
export VCPKG_DEFAULT_TRIPLET="${TRIPLET}"

cd "${ROOT}"
sed -i 's/\r$//' "${ROOT}/scripts/wsl_build_linux_arm64.sh" 2>/dev/null || true

command -v aarch64-linux-gnu-g++ >/dev/null \
  || { echo "ERROR: aarch64-linux-gnu-g++ fehlt (apt: g++-aarch64-linux-gnu)"; exit 1; }

mkdir -p "${BUILD_DIR}" "${DIST_DIR}"

echo "==> Host: $(uname -m)  Target: aarch64  Compiler: $(aarch64-linux-gnu-g++ -dumpmachine)"
echo "==> CMake configure (${TRIPLET})"
cmake -S "${ROOT}" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="${TOOLCHAIN}" \
  -DVCPKG_TARGET_TRIPLET="${TRIPLET}" \
  -DVCPKG_OVERLAY_TRIPLETS="${CUSTOM_TRIPLETS}" \
  -DVCPKG_OVERLAY_PORTS="${OVERLAY_PORTS}" \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DLIBZFORK_BUILD_SHARED=ON \
  -DLIBZFORK_BUILD_STATIC=ON \
  -DLIBZFORK_BUILD_EXAMPLES=OFF \
  -DLIBZFORK_BUILD_TESTS=OFF \
  -DLIBZFORK_BUILD_APPS=OFF

echo "==> Build"
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

echo "==> Verify ELF architecture"
SO="${BUILD_DIR}/libZFork.so.1.0.0"
[[ -f "${SO}" ]] || SO="$(find "${BUILD_DIR}" -name 'libZFork.so*' -type f | head -1)"
file "${SO}"
readelf -h "${SO}" | grep -E 'Class|Machine|Data'
aarch64-linux-gnu-nm -D "${SO}" 2>/dev/null | grep ' T zfork_Create$' || \
  aarch64-linux-gnu-nm "${SO}" | grep ' T zfork_Create$' || true

echo "==> Dist → ${DIST_DIR}"
cp -a "${BUILD_DIR}"/libZFork.so* "${DIST_DIR}/" 2>/dev/null || true
cp -a "${BUILD_DIR}"/libZFork_static.a "${DIST_DIR}/" 2>/dev/null || true
ls -lh "${DIST_DIR}"

echo "==> Done (arm64-linux)"
