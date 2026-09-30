#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VCPKG_ROOT="${VCPKG_ROOT:-${HOME}/vcpkg}"
BUILD_DIR="${ROOT}/build/wsl-linux"
export VCPKG_ROOT
export VCPKG_DEFAULT_TRIPLET=x64-linux

cd "${ROOT}"
mkdir -p "${BUILD_DIR}"

echo "==> CMake configure (shared lib, no tests/apps)"
cmake -S "${ROOT}" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DLIBZFORK_BUILD_SHARED=ON \
  -DLIBZFORK_BUILD_STATIC=ON \
  -DLIBZFORK_BUILD_EXAMPLES=OFF \
  -DLIBZFORK_BUILD_TESTS=OFF \
  -DLIBZFORK_BUILD_APPS=OFF

echo "==> Build"
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

echo "==> Artifacts"
find "${BUILD_DIR}" -name 'libZFork*.so*' -o -name 'libZFork*.a' | sort
ls -lh "${BUILD_DIR}"/libZFork* 2>/dev/null || true
