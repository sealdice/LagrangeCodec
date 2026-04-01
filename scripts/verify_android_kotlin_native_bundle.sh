#!/usr/bin/env bash

set -euo pipefail

BUNDLE_ROOT="${1:?bundle path is required}"
NM_BIN="${2:-}"

if [ -d "${BUNDLE_ROOT}/lib" ]; then
  LIB_DIR="${BUNDLE_ROOT}/lib"
else
  LIB_DIR="${BUNDLE_ROOT}"
fi

if [ -z "${NM_BIN}" ]; then
  if command -v llvm-nm >/dev/null 2>&1; then
    NM_BIN="$(command -v llvm-nm)"
  else
    NM_BIN="$(command -v nm)"
  fi
fi

required=(libLagrangeCodec.a libavcodec.a libavformat.a libavutil.a libswresample.a libswscale.a libz.a)
for name in "${required[@]}"; do
  if [ ! -f "${LIB_DIR}/${name}" ]; then
    echo "Missing required archive: ${LIB_DIR}/${name}" >&2
    exit 1
  fi
done

public_symbols=(audio_to_pcm silk_decode silk_encode video_first_frame video_get_size)
for symbol in "${public_symbols[@]}"; do
  if ! "${NM_BIN}" --defined-only --extern-only "${LIB_DIR}/libLagrangeCodec.a" 2>/dev/null | grep -Eq "(^|[[:space:]])${symbol}$"; then
    echo "Missing exported symbol in libLagrangeCodec.a: ${symbol}" >&2
    exit 1
  fi
done

undefined_symbols_file="$(mktemp)"
trap 'rm -f "${undefined_symbols_file}"' EXIT

for archive in "${LIB_DIR}"/*.a; do
  "${NM_BIN}" -u "${archive}" 2>/dev/null | sed -nE 's/^.* U ([^[:space:]]+)$/\1/p' >> "${undefined_symbols_file}" || true
done

sort -u "${undefined_symbols_file}" -o "${undefined_symbols_file}"

forbidden=(stderr stdout stdin __errno_location __libc_start_main)
for symbol in "${forbidden[@]}"; do
  if grep -qx "${symbol}" "${undefined_symbols_file}"; then
    echo "Forbidden undefined symbol detected: ${symbol}" >&2
    exit 1
  fi
done

if grep -Eq '^__.*_chk$' "${undefined_symbols_file}"; then
  echo "Forbidden fortified/glibc-style undefined symbols detected:" >&2
  grep -E '^__.*_chk$' "${undefined_symbols_file}" >&2
  exit 1
fi

echo "Android Kotlin/Native bundle verification passed"
