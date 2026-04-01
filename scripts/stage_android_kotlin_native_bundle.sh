#!/usr/bin/env bash

set -euo pipefail

BUILD_DIR="${1:-build}"
OUT_DIR="${2:-bundle/androidArm64-kotlinNative}"
CUSTOM_ZLIB_LIB="${3:-}"
VCPKG_LIB_DIR="${BUILD_DIR}/vcpkg_installed/arm64-android/lib"
PUBLIC_INCLUDE_DIR="include/public"

mkdir -p "${OUT_DIR}/lib" "${OUT_DIR}/include/public"

cp -f "${BUILD_DIR}/libLagrangeCodec.a" "${OUT_DIR}/lib/"

required=(libavformat.a libavcodec.a libavutil.a libswresample.a libswscale.a)
missing=0
for name in "${required[@]}"; do
  if [ ! -f "${VCPKG_LIB_DIR}/${name}" ]; then
    echo "Missing required dependency: ${VCPKG_LIB_DIR}/${name}" >&2
    missing=1
  fi
done

zlib_source=""
if [ -n "${CUSTOM_ZLIB_LIB}" ] && [ -f "${CUSTOM_ZLIB_LIB}" ]; then
  zlib_source="${CUSTOM_ZLIB_LIB}"
elif [ -f "${VCPKG_LIB_DIR}/libz.a" ]; then
  zlib_source="${VCPKG_LIB_DIR}/libz.a"
elif [ -f "${VCPKG_LIB_DIR}/libzlib.a" ]; then
  zlib_source="${VCPKG_LIB_DIR}/libzlib.a"
else
  echo "Missing required dependency: ${VCPKG_LIB_DIR}/libz.a (or libzlib.a)" >&2
  missing=1
fi

if [ "${missing}" -ne 0 ]; then
  echo "Available files under ${VCPKG_LIB_DIR}:" >&2
  ls -la "${VCPKG_LIB_DIR}" >&2 || true
  exit 1
fi

for name in "${required[@]}"; do
  cp -f "${VCPKG_LIB_DIR}/${name}" "${OUT_DIR}/lib/${name}"
done

cp -f "${zlib_source}" "${OUT_DIR}/lib/libz.a"
cp -f "${PUBLIC_INCLUDE_DIR}"/*.h "${OUT_DIR}/include/public/"
cp -f "docs/android-arm64-kotlin-native.md" "${OUT_DIR}/README.md"

cat > "${OUT_DIR}/link-order.txt" <<'EOF'
libLagrangeCodec.a
libavformat.a
libavcodec.a
libswresample.a
libswscale.a
libavutil.a
libz.a
EOF

cat > "${OUT_DIR}/kotlin-native-linker-opts.txt" <<'EOF'
-llog
-ldl
-latomic
-landroid
-lm
EOF

cat > "${OUT_DIR}/replace-into-acidify-codec.txt" <<'EOF'
Copy every archive from ./lib/ into acidify-codec/src/nativeInterop/lib/androidArm64/.

Expected files:
- libLagrangeCodec.a
- libavcodec.a
- libavformat.a
- libavutil.a
- libswresample.a
- libswscale.a
- libz.a

Keep the Kotlin/Native linker opts aligned with kotlin-native-linker-opts.txt.
EOF

echo "Staged Android Kotlin/Native bundle at ${OUT_DIR}"
find "${OUT_DIR}" -maxdepth 3 -type f | sort
