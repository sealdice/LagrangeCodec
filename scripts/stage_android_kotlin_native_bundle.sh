#!/usr/bin/env bash

set -euo pipefail

BUILD_DIR="${1:-build}"
OUT_DIR="${2:-bundle/androidArm64-kotlinNative}"
ANDROID_NATIVE_PREFIX="${3:-}"
LIB_SOURCE_DIR="${BUILD_DIR}/vcpkg_installed/arm64-android/lib"
PUBLIC_INCLUDE_DIR="include/public"
AR_BIN="${AR_BIN:-}"
RANLIB_BIN="${RANLIB_BIN:-}"

if [ -n "${ANDROID_NATIVE_PREFIX}" ]; then
  LIB_SOURCE_DIR="${ANDROID_NATIVE_PREFIX}/lib"
fi

if [ -z "${AR_BIN}" ] && [ -n "${ANDROID_NDK_HOME:-}" ]; then
  NDK_TOOL_BIN="${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/bin"
  if [ -x "${NDK_TOOL_BIN}/llvm-ar" ]; then
    AR_BIN="${NDK_TOOL_BIN}/llvm-ar"
  fi
  if [ -x "${NDK_TOOL_BIN}/llvm-ranlib" ]; then
    RANLIB_BIN="${NDK_TOOL_BIN}/llvm-ranlib"
  fi
fi

if [ -z "${AR_BIN}" ]; then
  AR_BIN="$(command -v llvm-ar || command -v ar)"
fi

if [ -z "${RANLIB_BIN}" ]; then
  RANLIB_BIN="$(command -v llvm-ranlib || command -v ranlib || true)"
fi

mkdir -p "${OUT_DIR}/lib" "${OUT_DIR}/include/public"
mkdir -p "${OUT_DIR}/bin"

cp -f "${BUILD_DIR}/libLagrangeCodec.a" "${OUT_DIR}/lib/"

required=(libavformat.a libavcodec.a libavutil.a libswresample.a libswscale.a)
missing=0
for name in "${required[@]}"; do
  if [ ! -f "${LIB_SOURCE_DIR}/${name}" ]; then
    echo "Missing required dependency: ${LIB_SOURCE_DIR}/${name}" >&2
    missing=1
  fi
done

zlib_source=""
if [ -f "${LIB_SOURCE_DIR}/libz.a" ]; then
  zlib_source="${LIB_SOURCE_DIR}/libz.a"
elif [ -f "${LIB_SOURCE_DIR}/libzlib.a" ]; then
  zlib_source="${LIB_SOURCE_DIR}/libzlib.a"
else
  echo "Missing required dependency: ${LIB_SOURCE_DIR}/libz.a (or libzlib.a)" >&2
  missing=1
fi

if [ "${missing}" -ne 0 ]; then
  echo "Available files under ${LIB_SOURCE_DIR}:" >&2
  ls -la "${LIB_SOURCE_DIR}" >&2 || true
  exit 1
fi

for name in "${required[@]}"; do
  cp -f "${LIB_SOURCE_DIR}/${name}" "${OUT_DIR}/lib/${name}"
done

cp -f "${zlib_source}" "${OUT_DIR}/lib/libz.a"

merged_archive_tmp="${OUT_DIR}/lib/libLagrangeCodec-merged.a"
mri_script="$(mktemp)"
trap 'rm -f "${mri_script}"' EXIT

cat > "${mri_script}" <<EOF
CREATE ${merged_archive_tmp}
ADDLIB ${BUILD_DIR}/libLagrangeCodec.a
ADDLIB ${OUT_DIR}/lib/libavformat.a
ADDLIB ${OUT_DIR}/lib/libavcodec.a
ADDLIB ${OUT_DIR}/lib/libswresample.a
ADDLIB ${OUT_DIR}/lib/libswscale.a
ADDLIB ${OUT_DIR}/lib/libavutil.a
ADDLIB ${OUT_DIR}/lib/libz.a
SAVE
END
EOF

"${AR_BIN}" -M < "${mri_script}"
if [ -n "${RANLIB_BIN}" ]; then
  "${RANLIB_BIN}" "${merged_archive_tmp}"
fi

mv -f "${merged_archive_tmp}" "${OUT_DIR}/lib/libLagrangeCodec.a"

cp -f "${PUBLIC_INCLUDE_DIR}"/*.h "${OUT_DIR}/include/public/"
cp -f "docs/android-arm64-kotlin-native.md" "${OUT_DIR}/README.md"

if [ -f "${BUILD_DIR}/android_runtime_smoke" ]; then
  cp -f "${BUILD_DIR}/android_runtime_smoke" "${OUT_DIR}/bin/"
fi

cat > "${OUT_DIR}/link-order.txt" <<'EOF'
libLagrangeCodec.a
EOF

cat > "${OUT_DIR}/kotlin-native-linker-opts.txt" <<'EOF'
-llog
-ldl
-latomic
-landroid
-lm
EOF

cat > "${OUT_DIR}/replace-into-acidify-codec.txt" <<'EOF'
Prefer linking only ./lib/libLagrangeCodec.a for Kotlin/Native Android.

You may still copy the separate FFmpeg archives for debugging/reference, but the recommended path is to use the merged libLagrangeCodec.a first.

Copy archives into acidify-codec/src/nativeInterop/lib/androidArm64/.

Expected files:
- libLagrangeCodec.a
- libavcodec.a
- libavformat.a
- libavutil.a
- libswresample.a
- libswscale.a
- libz.a

Optional smoke binary (when present):
- bin/android_runtime_smoke

Keep the Kotlin/Native linker opts aligned with kotlin-native-linker-opts.txt.
EOF

echo "Staged Android Kotlin/Native bundle at ${OUT_DIR}"
find "${OUT_DIR}" -maxdepth 3 -type f | sort
