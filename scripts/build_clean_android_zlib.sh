#!/usr/bin/env bash

set -euo pipefail

ANDROID_NDK_HOME="${1:?ANDROID_NDK_HOME is required}"
OUT_ROOT_INPUT="${2:?output root is required}"
ZLIB_VERSION="${ZLIB_VERSION:-1.3.1}"
ABI="${ANDROID_ABI:-arm64-v8a}"
API="${ANDROID_PLATFORM:-21}"

mkdir -p "${OUT_ROOT_INPUT}"
OUT_ROOT="$(cd "${OUT_ROOT_INPUT}" && pwd)"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_ROOT="${OUT_ROOT}/_work"
SRC_ARCHIVE="${WORK_ROOT}/zlib-${ZLIB_VERSION}.tar.gz"
SRC_DIR="${WORK_ROOT}/zlib-src"
BUILD_DIR="${WORK_ROOT}/build"
PREFIX_DIR="${OUT_ROOT}/prefix"

mkdir -p "${WORK_ROOT}"

if [ ! -f "${SRC_ARCHIVE}" ]; then
  curl -L --fail --retry 3 "https://github.com/madler/zlib/archive/refs/tags/v${ZLIB_VERSION}.tar.gz" -o "${SRC_ARCHIVE}"
fi

rm -rf "${SRC_DIR}" "${BUILD_DIR}" "${PREFIX_DIR}"
mkdir -p "${SRC_DIR}" "${BUILD_DIR}" "${PREFIX_DIR}"

tar -xzf "${SRC_ARCHIVE}" -C "${SRC_DIR}" --strip-components=1

cat >> "${SRC_DIR}/CMakeLists.txt" <<'EOF'

if(ANDROID AND NOT BUILD_SHARED_LIBS)
  add_compile_options(-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)
endif()
EOF

cmake -S "${SRC_DIR}" -B "${BUILD_DIR}" \
  -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="${ABI}" \
  -DANDROID_PLATFORM="${API}" \
  -DANDROID_STL="c++_static" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_INSTALL_PREFIX="${PREFIX_DIR}" \
  -DCMAKE_C_FLAGS="-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0" \
  -DCMAKE_C_FLAGS_RELEASE="-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0"

cmake --build "${BUILD_DIR}"
cmake --install "${BUILD_DIR}"

if [ ! -f "${PREFIX_DIR}/lib/libz.a" ]; then
  echo "Expected clean zlib archive missing: ${PREFIX_DIR}/lib/libz.a" >&2
  exit 1
fi

echo "Built clean Android zlib at ${PREFIX_DIR}/lib/libz.a"
