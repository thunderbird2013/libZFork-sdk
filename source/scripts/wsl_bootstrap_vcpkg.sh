#!/usr/bin/env bash
set -euo pipefail
VCPKG_DIR="${HOME}/vcpkg"
if [[ ! -x "${VCPKG_DIR}/vcpkg" ]]; then
  echo "==> Cloning vcpkg into ${VCPKG_DIR}"
  git clone --depth 1 https://github.com/microsoft/vcpkg.git "${VCPKG_DIR}"
  cd "${VCPKG_DIR}"
  ./bootstrap-vcpkg.sh -disableMetrics
else
  echo "==> Using existing ${VCPKG_DIR}"
  "${VCPKG_DIR}/vcpkg" version
fi
