#!/usr/bin/env bash

set -euo pipefail

BUILD_DIR="${1:?build dir is required}"
PREFIX_DIR="${2:?prefix dir is required}"
OUT_DIR="${3:-bundle/zig-androidArm64}"

mkdir -p "${OUT_DIR}/lib" "${OUT_DIR}/include/public" "${OUT_DIR}/bin"

cp -f "${BUILD_DIR}/libLagrangeCodec.a" "${OUT_DIR}/lib/"

if [ -f "${PREFIX_DIR}/lib/libffmpeg.a" ]; then
  cp -f "${PREFIX_DIR}/lib/libffmpeg.a" "${OUT_DIR}/lib/libffmpeg.a"
  LINK_HINT='lib/libLagrangeCodec.a lib/libffmpeg.a -lz -latomic -landroid -lmediandk -lm'
else
  for name in libavcodec.a libavformat.a libavutil.a libswresample.a libswscale.a; do
    cp -f "${PREFIX_DIR}/lib/${name}" "${OUT_DIR}/lib/${name}"
  done

  if [ -f "${PREFIX_DIR}/lib/libz.a" ]; then
    cp -f "${PREFIX_DIR}/lib/libz.a" "${OUT_DIR}/lib/libz.a"
  elif [ -f "${PREFIX_DIR}/lib/libzlib.a" ]; then
    cp -f "${PREFIX_DIR}/lib/libzlib.a" "${OUT_DIR}/lib/libz.a"
  fi

  LINK_HINT='lib/libLagrangeCodec.a lib/libavformat.a lib/libavcodec.a lib/libswresample.a lib/libswscale.a lib/libavutil.a -lz -latomic -landroid -lmediandk -lm'
fi

if [ -f "${BUILD_DIR}/android_runtime_smoke" ]; then
  cp -f "${BUILD_DIR}/android_runtime_smoke" "${OUT_DIR}/bin/"
fi

cp -f include/public/*.h "${OUT_DIR}/include/public/"
printf '%s\n' "$LINK_HINT" > "${OUT_DIR}/link-order.txt"

cat > "${OUT_DIR}/README.txt" <<EOF
Zig-based Android arm64 experimental bundle.

Contents:
- lib/libLagrangeCodec.a
- lib/libffmpeg.a or the split FFmpeg archives
- bin/android_runtime_smoke (if built)

Recommended Android link order:
- ${LINK_HINT}

Notes:
- The FFmpeg archives come from yearsyan/ffmpeg-android-build (${PREFIX_DIR}).
- Android system libraries are expected from the platform image, not bundled here.
EOF

find "${OUT_DIR}" -maxdepth 3 -type f | sort
