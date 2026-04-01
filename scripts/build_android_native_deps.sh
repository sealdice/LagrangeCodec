#!/usr/bin/env bash

set -euo pipefail

ANDROID_NDK_HOME="${1:?ANDROID_NDK_HOME is required}"
OUT_ROOT="${2:?output root is required}"

bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build_clean_android_zlib.sh" "${ANDROID_NDK_HOME}" "${OUT_ROOT}"
bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build_android_ffmpeg.sh" "${ANDROID_NDK_HOME}" "${OUT_ROOT}"
