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
    echo "::error::Missing required archive: ${LIB_DIR}/${name}"
    echo "Missing required archive: ${LIB_DIR}/${name}" >&2
    exit 1
  fi
done

public_symbols=(audio_to_pcm silk_decode silk_encode video_first_frame video_get_size)
defined_symbols_file="$(mktemp)"
undefined_symbols_file="$(mktemp)"
trap 'rm -f "${defined_symbols_file}" "${undefined_symbols_file}"' EXIT

"${NM_BIN}" --defined-only "${LIB_DIR}/libLagrangeCodec.a" 2>/dev/null > "${defined_symbols_file}" || true

for symbol in "${public_symbols[@]}"; do
  if ! grep -Eq "(^|[[:space:]])(_)?${symbol}$" "${defined_symbols_file}"; then
    sample_symbols="$(grep -E 'audio_to_pcm|silk_|video_' "${defined_symbols_file}" | tr '\n' ';' | sed 's/"/%22/g' | cut -c1-400)"
    echo "::error::Missing exported symbol in libLagrangeCodec.a: ${symbol}; visible symbols=${sample_symbols}"
    echo "Missing exported symbol in libLagrangeCodec.a: ${symbol}" >&2
    exit 1
  fi
done

for archive in "${LIB_DIR}"/*.a; do
  "${NM_BIN}" -u "${archive}" 2>/dev/null | sed -nE 's/^.* U ([^[:space:]]+)$/\1/p' >> "${undefined_symbols_file}" || true
done

sort -u "${undefined_symbols_file}" -o "${undefined_symbols_file}"

forbidden=(stderr stdout stdin __errno_location __libc_start_main)
for symbol in "${forbidden[@]}"; do
  if grep -qx "${symbol}" "${undefined_symbols_file}"; then
    echo "::error::Forbidden undefined symbol detected: ${symbol}"
    echo "Forbidden undefined symbol detected: ${symbol}" >&2
    exit 1
  fi
done

if grep -Eq '^__.*_chk$' "${undefined_symbols_file}"; then
  chk_symbols="$(grep -E '^__.*_chk$' "${undefined_symbols_file}" | tr '\n' ';' | sed 's/"/%22/g' | cut -c1-400)"
  echo "::error::Forbidden fortified/glibc-style undefined symbols detected: ${chk_symbols}"
  echo "Forbidden fortified/glibc-style undefined symbols detected:" >&2
  grep -E '^__.*_chk$' "${undefined_symbols_file}" >&2
  exit 1
fi

echo "Android Kotlin/Native bundle verification passed"
