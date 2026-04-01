#!/usr/bin/env bash

set -euo pipefail

ANDROID_NDK_HOME="${1:?ANDROID_NDK_HOME is required}"
OUT_ROOT="${2:?output root is required}"
FFMPEG_VERSION="${FFMPEG_VERSION:-5.0}"
API="${ANDROID_PLATFORM:-21}"
ABI="${ANDROID_ABI:-arm64-v8a}"

if [[ "${ABI}" != "arm64-v8a" ]]; then
  echo "Only arm64-v8a is supported by this script" >&2
  exit 1
fi

HOST_OS="$(uname | tr '[:upper:]' '[:lower:]')"
HOST_ARCH="$(uname -m)"
if [[ "${HOST_OS}" == "linux" ]]; then
  HOST_TAG="linux-${HOST_ARCH}"
elif [[ "${HOST_OS}" == "darwin" ]]; then
  HOST_TAG="darwin-${HOST_ARCH}"
else
  echo "Unsupported host OS: ${HOST_OS}" >&2
  exit 1
fi

if [[ ! -d "${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/${HOST_TAG}" ]]; then
  if [[ "${HOST_OS}" == "linux" && -d "${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64" ]]; then
    HOST_TAG="linux-x86_64"
  elif [[ "${HOST_OS}" == "darwin" && -d "${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/darwin-x86_64" ]]; then
    HOST_TAG="darwin-x86_64"
  else
    echo "Unable to locate NDK prebuilt toolchain under ${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt" >&2
    exit 1
  fi
fi

TOOLCHAIN_ROOT="${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/${HOST_TAG}"
TOOLCHAIN_BIN="${TOOLCHAIN_ROOT}/bin"
SYSROOT="${TOOLCHAIN_ROOT}/sysroot"
TRIPLE="aarch64-linux-android"
TARGET_CPU="armv8-a"
TARGET_ARCH="aarch64"

export CC="${TOOLCHAIN_BIN}/${TRIPLE}${API}-clang"
export CXX="${TOOLCHAIN_BIN}/${TRIPLE}${API}-clang++"
export AR="${TOOLCHAIN_BIN}/llvm-ar"
export AS="${TOOLCHAIN_BIN}/llvm-as"
export LD="${CC}"
export NM="${TOOLCHAIN_BIN}/llvm-nm"
export RANLIB="${TOOLCHAIN_BIN}/llvm-ranlib"
export STRIP="${TOOLCHAIN_BIN}/llvm-strip"

WORK_ROOT="${OUT_ROOT}/_work"
SRC_ARCHIVE="${WORK_ROOT}/ffmpeg-n${FFMPEG_VERSION}.tar.gz"
SRC_DIR="${WORK_ROOT}/ffmpeg-src"
BUILD_DIR="${WORK_ROOT}/ffmpeg-build"
PREFIX_DIR="${OUT_ROOT}/prefix"
LOG_C="${SRC_DIR}/libavutil/log.c"

mkdir -p "${WORK_ROOT}"

if [[ ! -f "${SRC_ARCHIVE}" ]]; then
  curl -L --fail --retry 3 "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n${FFMPEG_VERSION}.tar.gz" -o "${SRC_ARCHIVE}"
fi

rm -rf "${SRC_DIR}" "${BUILD_DIR}"
mkdir -p "${SRC_DIR}" "${BUILD_DIR}" "${PREFIX_DIR}"
tar -xzf "${SRC_ARCHIVE}" -C "${SRC_DIR}" --strip-components=1

export LOG_C
PYTHON_BIN="$(command -v python3 || command -v python)"
if [[ -z "${PYTHON_BIN}" ]]; then
  echo "python3 or python is required to patch FFmpeg sources" >&2
  exit 1
fi

"${PYTHON_BIN}" - <<'PY'
import os
from pathlib import Path

path = Path(os.environ["LOG_C"])
text = path.read_text(encoding="utf-8")

replacements = [
    (
        '#include "log.h"\n#include "thread.h"\n',
        '#include "log.h"\n#include "thread.h"\n\n#if defined(__ANDROID__)\n#include <android/log.h>\n#endif\n',
        'android log include',
    ),
    (
        'static void ansi_fputs(int level, int tint, const char *str, int local_use_color)\n{\n    if (local_use_color == 1) {\n        fprintf(stderr,\n                "\\033[%"PRIu32";3%"PRIu32"m%s\\033[0m",\n                (color[level] >> 4) & 15,\n                color[level] & 15,\n                str);\n    } else if (tint && use_color == 256) {\n        fprintf(stderr,\n                "\\033[48;5;%"PRIu32"m\\033[38;5;%dm%s\\033[0m",\n                (color[level] >> 16) & 0xff,\n                tint,\n                str);\n    } else if (local_use_color == 256) {\n        fprintf(stderr,\n                "\\033[48;5;%"PRIu32"m\\033[38;5;%"PRIu32"m%s\\033[0m",\n                (color[level] >> 16) & 0xff,\n                (color[level] >> 8) & 0xff,\n                str);\n    } else\n        fputs(str, stderr);\n}\n',
        '#if defined(__ANDROID__)\nstatic int av_log_android_priority(int level)\n{\n    if (level <= AV_LOG_ERROR)   return ANDROID_LOG_ERROR;\n    if (level <= AV_LOG_WARNING) return ANDROID_LOG_WARN;\n    if (level <= AV_LOG_INFO)    return ANDROID_LOG_INFO;\n    if (level <= AV_LOG_VERBOSE) return ANDROID_LOG_VERBOSE;\n    return ANDROID_LOG_DEBUG;\n}\n\nstatic void ansi_fputs(int level, int tint, const char *str, int local_use_color)\n{\n    (void)level;\n    (void)tint;\n    (void)str;\n    (void)local_use_color;\n}\n#else\nstatic void ansi_fputs(int level, int tint, const char *str, int local_use_color)\n{\n    if (local_use_color == 1) {\n        fprintf(stderr,\n                "\\033[%"PRIu32";3%"PRIu32"m%s\\033[0m",\n                (color[level] >> 4) & 15,\n                color[level] & 15,\n                str);\n    } else if (tint && use_color == 256) {\n        fprintf(stderr,\n                "\\033[48;5;%"PRIu32"m\\033[38;5;%dm%s\\033[0m",\n                (color[level] >> 16) & 0xff,\n                tint,\n                str);\n    } else if (local_use_color == 256) {\n        fprintf(stderr,\n                "\\033[48;5;%"PRIu32"m\\033[38;5;%"PRIu32"m%s\\033[0m",\n                (color[level] >> 16) & 0xff,\n                (color[level] >> 8) & 0xff,\n                str);\n    } else\n        fputs(str, stderr);\n}\n#endif\n',
        'android ansi_fputs override',
    ),
    (
        '    format_line(ptr, level, fmt, vl, part, &print_prefix, type);\n    snprintf(line, sizeof(line), "%s%s%s%s", part[0].str, part[1].str, part[2].str, part[3].str);\n\n#if HAVE_ISATTY\n',
        '    format_line(ptr, level, fmt, vl, part, &print_prefix, type);\n    snprintf(line, sizeof(line), "%s%s%s%s", part[0].str, part[1].str, part[2].str, part[3].str);\n\n#if defined(__ANDROID__)\n    sanitize((uint8_t *)line);\n    if (*line)\n        __android_log_write(av_log_android_priority(level), "FFmpeg", line);\n    goto end;\n#endif\n\n#if HAVE_ISATTY\n',
        'android default callback override',
    ),
    (
        '        if (is_atty == 1)\n            fprintf(stderr, "    Last message repeated %d times\\r", count);\n',
        '        if (is_atty == 1) {\n#if defined(__ANDROID__)\n            __android_log_print(ANDROID_LOG_INFO, "FFmpeg", "Last message repeated %d times", count);\n#else\n            fprintf(stderr, "    Last message repeated %d times\\r", count);\n#endif\n        }\n',
        'android repeated message stderr removal',
    ),
    (
        '    if (count > 0) {\n        fprintf(stderr, "    Last message repeated %d times\\n", count);\n        count = 0;\n    }\n',
        '    if (count > 0) {\n#if defined(__ANDROID__)\n        __android_log_print(ANDROID_LOG_INFO, "FFmpeg", "Last message repeated %d times", count);\n#else\n        fprintf(stderr, "    Last message repeated %d times\\n", count);\n#endif\n        count = 0;\n    }\n',
        'android repeated flush stderr removal',
    ),
]

for old, new, label in replacements:
    if old not in text:
        raise SystemExit(f"failed to locate {label} in {path}")
    text = text.replace(old, new, 1)

path.write_text(text, encoding="utf-8")
PY

pushd "${BUILD_DIR}" >/dev/null

"${SRC_DIR}/configure" \
  --prefix="${PREFIX_DIR}" \
  --target-os=android \
  --arch="${TARGET_ARCH}" \
  --cpu="${TARGET_CPU}" \
  --sysroot="${SYSROOT}" \
  --cc="${CC}" \
  --cxx="${CXX}" \
  --ld="${LD}" \
  --ar="${AR}" \
  --nm="${NM}" \
  --ranlib="${RANLIB}" \
  --strip="${STRIP}" \
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
  --enable-avcodec \
  --enable-avformat \
  --enable-avutil \
  --enable-swresample \
  --enable-swscale \
  --enable-zlib \
  --enable-protocol=file \
  --enable-encoder=png \
  --enable-decoder=mp3 \
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
  --extra-libs="-llog -ldl -latomic -landroid -lm"

make -j"$(nproc 2>/dev/null || echo 4)"
make install

popd >/dev/null

for required_lib in libavcodec.a libavformat.a libavutil.a libswresample.a libswscale.a; do
  if [[ ! -f "${PREFIX_DIR}/lib/${required_lib}" ]]; then
    echo "Expected FFmpeg archive missing: ${PREFIX_DIR}/lib/${required_lib}" >&2
    exit 1
  fi
done

echo "Built Android FFmpeg static libraries under ${PREFIX_DIR}/lib"
