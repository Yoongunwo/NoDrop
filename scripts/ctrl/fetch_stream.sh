#!/usr/bin/env bash
set -euo pipefail

# Periodically fetch kernel buffers into a single raw stream and an index file.
# Index format per line:
# fetch_start_ns fetch_end_ns events bytes raw_off_start raw_off_end
#
# Usage:
#   ./scripts/ctrl/fetch_stream.sh [interval_sec] [out_dir]
#
# Example:
#   ./scripts/ctrl/fetch_stream.sh 0.2 /tmp/nodrop

INTERVAL="${1:-0.2}"
OUT_DIR="${2:-/tmp/nodrop}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CTRL_BIN="${SCRIPT_DIR}/ctrl"

RAW="${OUT_DIR}/stream.raw"
IDX="${OUT_DIR}/stream.idx"
TMP="${OUT_DIR}/fetch.tmp.raw"

mkdir -p "${OUT_DIR}"
: > "${RAW}"
: > "${IDX}"

echo "[fetch_stream] interval=${INTERVAL}s"
echo "[fetch_stream] raw=${RAW}"
echo "[fetch_stream] idx=${IDX}"

while true; do
  line="$("${CTRL_BIN}" count 2>/dev/null || true)"
  nlen="$(echo "${line}" | sed -n 's/.*unflushed_len=\([0-9]\+\).*/\1/p')"
  ncnt="$(echo "${line}" | sed -n 's/.*unflushed_count=\([0-9]\+\).*/\1/p')"

  if [ "${nlen:-0}" -gt 0 ]; then
    t0="$(date +%s%N)"
    "${CTRL_BIN}" fetch "${TMP}" >/dev/null 2>&1 || true
    t1="$(date +%s%N)"

    bytes="$(stat -c%s "${TMP}" 2>/dev/null || echo 0)"
    if [ "${bytes}" -gt 0 ]; then
      off0="$(stat -c%s "${RAW}" 2>/dev/null || echo 0)"
      cat "${TMP}" >> "${RAW}"
      off1="$(stat -c%s "${RAW}" 2>/dev/null || echo 0)"
      echo "${t0} ${t1} ${ncnt:-0} ${bytes} ${off0} ${off1}" >> "${IDX}"
    fi
  fi

  sleep "${INTERVAL}"
done

