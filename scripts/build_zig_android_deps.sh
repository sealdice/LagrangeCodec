#!/usr/bin/env bash

set -euo pipefail
set -x

OUT_ROOT_INPUT="${1:?output root is required}"
API="${ANDROID_PLATFORM:-21}"
TARGET_TRIPLE="aarch64-linux-android"
ZIG_BIN="${ZIG:-zig}"
ZLIB_VERSION="${ZLIB_VERSION:-1.3.1}"
FFMPEG_VERSION="${FFMPEG_VERSION:-5.0}"
AR_BIN="$(command -v llvm-ar || command -v ar)"
RANLIB_BIN="$(command -v llvm-ranlib || command -v ranlib || true)"

mkdir -p "${OUT_ROOT_INPUT}"
OUT_ROOT="$(cd "${OUT_ROOT_INPUT}" && pwd)"
WORK_ROOT="${OUT_ROOT}/_work"
BIN_DIR="${WORK_ROOT}/bin"
PREFIX_DIR="${OUT_ROOT}/prefix"

mkdir -p "${WORK_ROOT}" "${BIN_DIR}" "${PREFIX_DIR}"

download_with_fallbacks() {
  local output_path="$1"
  shift

  local url
  for url in "$@"; do
    echo "Trying download: ${url}"
    if curl -L --fail --retry 3 "${url}" -o "${output_path}"; then
      return 0
    fi
  done

  echo "Failed to download ${output_path} from all candidate URLs" >&2
  return 1
}

cat > "${BIN_DIR}/zig-cc" <<EOF
#!/usr/bin/env bash
exec "${ZIG_BIN}" cc -target ${TARGET_TRIPLE} -D__ANDROID_API__=${API} "\$@"
EOF

cat > "${BIN_DIR}/zig-cxx" <<EOF
#!/usr/bin/env bash
exec "${ZIG_BIN}" c++ -target ${TARGET_TRIPLE} -D__ANDROID_API__=${API} "\$@"
EOF

cat > "${BIN_DIR}/zig-ar" <<EOF
#!/usr/bin/env bash
exec "${AR_BIN}" "\$@"
EOF

cat > "${BIN_DIR}/zig-ranlib" <<EOF
#!/usr/bin/env bash
exec "${RANLIB_BIN}" "\$@"
EOF

chmod +x "${BIN_DIR}/zig-cc" "${BIN_DIR}/zig-cxx" "${BIN_DIR}/zig-ar" "${BIN_DIR}/zig-ranlib"

ZLIB_ARCHIVE="${WORK_ROOT}/zlib-${ZLIB_VERSION}.tar.gz"
ZLIB_SRC_DIR="${WORK_ROOT}/zlib-src"
ZLIB_BUILD_DIR="${WORK_ROOT}/zlib-build"

if [[ ! -f "${ZLIB_ARCHIVE}" ]]; then
  download_with_fallbacks \
    "${ZLIB_ARCHIVE}" \
    "https://zlib.net/zlib-${ZLIB_VERSION}.tar.gz" \
    "https://www.zlib.net/fossils/zlib-${ZLIB_VERSION}.tar.gz" \
    "https://github.com/madler/zlib/releases/download/v${ZLIB_VERSION}/zlib-${ZLIB_VERSION}.tar.gz" \
    "https://codeload.github.com/madler/zlib/tar.gz/refs/tags/v${ZLIB_VERSION}"
fi

rm -rf "${ZLIB_SRC_DIR}" "${ZLIB_BUILD_DIR}"
mkdir -p "${ZLIB_SRC_DIR}" "${ZLIB_BUILD_DIR}"
tar -xzf "${ZLIB_ARCHIVE}" -C "${ZLIB_SRC_DIR}" --strip-components=1

mkdir -p "${PREFIX_DIR}/include" "${PREFIX_DIR}/lib"

ZLIB_SOURCES=(
  adler32.c
  compress.c
  crc32.c
  deflate.c
  gzclose.c
  gzlib.c
  gzread.c
  gzwrite.c
  inflate.c
  infback.c
  inftrees.c
  inffast.c
  trees.c
  uncompr.c
  zutil.c
)

for src in "${ZLIB_SOURCES[@]}"; do
  "${BIN_DIR}/zig-cc" \
    -O3 \
    -fPIC \
    -I"${ZLIB_SRC_DIR}" \
    -c "${ZLIB_SRC_DIR}/${src}" \
    -o "${ZLIB_BUILD_DIR}/${src%.c}.o"
done

"${BIN_DIR}/zig-ar" rcs "${PREFIX_DIR}/lib/libz.a" "${ZLIB_BUILD_DIR}"/*.o
"${BIN_DIR}/zig-ranlib" "${PREFIX_DIR}/lib/libz.a"

cp -f "${ZLIB_SRC_DIR}/zlib.h" "${PREFIX_DIR}/include/"
cp -f "${ZLIB_SRC_DIR}/zconf.h" "${PREFIX_DIR}/include/"

FFMPEG_ARCHIVE="${WORK_ROOT}/ffmpeg-n${FFMPEG_VERSION}.tar.gz"
FFMPEG_SRC_DIR="${WORK_ROOT}/ffmpeg-src"
FFMPEG_BUILD_DIR="${WORK_ROOT}/ffmpeg-build"

if [[ ! -f "${FFMPEG_ARCHIVE}" ]]; then
  download_with_fallbacks \
    "${FFMPEG_ARCHIVE}" \
    "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n${FFMPEG_VERSION}.tar.gz" \
    "https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/refs/tags/n${FFMPEG_VERSION}" \
    "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.gz"
fi

rm -rf "${FFMPEG_SRC_DIR}" "${FFMPEG_BUILD_DIR}"
mkdir -p "${FFMPEG_SRC_DIR}" "${FFMPEG_BUILD_DIR}"
tar -xzf "${FFMPEG_ARCHIVE}" -C "${FFMPEG_SRC_DIR}" --strip-components=1

NM_BIN="$(command -v llvm-nm || command -v nm)"
STRIP_BIN="$(command -v llvm-strip || command -v strip)"

pushd "${FFMPEG_BUILD_DIR}" >/dev/null
"${FFMPEG_SRC_DIR}/configure" \
  --prefix="${PREFIX_DIR}" \
  --target-os=android \
  --arch=aarch64 \
  --cc="${BIN_DIR}/zig-cc" \
  --cxx="${BIN_DIR}/zig-cxx" \
  --ar="${BIN_DIR}/zig-ar" \
  --nm="${NM_BIN}" \
  --ranlib="${BIN_DIR}/zig-ranlib" \
  --strip="${STRIP_BIN}" \
  --enable-cross-compile \
  --enable-pic \
  --disable-asm \
  --disable-x86asm \
  --disable-shared \
  --enable-static \
  --disable-programs \
  --disable-doc \
  --disable-debug \
  --disable-autodetect \
  --disable-network \
  --disable-postproc \
  --disable-avdevice \
  --disable-iconv \
  --disable-bzlib \
  --disable-lzma \
  --disable-sdl2 \
  --disable-everything \
  --disable-logging \
  --enable-avcodec \
  --enable-avformat \
  --enable-avutil \
  --enable-swresample \
  --enable-swscale \
  --enable-zlib \
  --enable-protocol=file \
  --enable-encoder=png \
  --enable-decoder=mp3 \
  --enable-decoder=mp3float \
  --enable-decoder=aac \
  --enable-decoder=flac \
  --enable-decoder=vorbis \
  --enable-decoder=pcm_s16le \
  --enable-decoder=h264 \
  --enable-decoder=hevc \
  --enable-decoder=vp8 \
  --enable-decoder=vp9 \
  --enable-decoder=mjpeg \
  --enable-demuxer=aac \
  --enable-demuxer=aiff \
  --enable-demuxer=flac \
  --enable-demuxer=mp3 \
  --enable-demuxer=ogg \
  --enable-demuxer=pcm_alaw \
  --enable-demuxer=wav \
  --enable-demuxer=flv \
  --enable-demuxer=h264 \
  --enable-demuxer=hevc \
  --enable-demuxer=matroska \
  --enable-demuxer=mov \
  --enable-demuxer=avi \
  --enable-parser=aac \
  --enable-parser=mpegaudio \
  --enable-parser=h264 \
  --enable-parser=hevc \
  --enable-parser=vorbis \
  --enable-bsf=h264_mp4toannexb \
  --enable-bsf=hevc_mp4toannexb \
  --enable-fft \
  --extra-cflags="-I${PREFIX_DIR}/include -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0" \
  --extra-ldflags="-L${PREFIX_DIR}/lib" \
  --extra-libs="-lm"

make -j"$(nproc 2>/dev/null || echo 4)"
make install
popd >/dev/null

echo "Built Zig Android deps under ${PREFIX_DIR}"
find "${PREFIX_DIR}" -maxdepth 2 -type f | sort
