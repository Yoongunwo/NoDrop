#!/usr/bin/env bash
set -euo pipefail

# Analyze stream index produced by fetch_stream.sh.
#
# Usage:
#   ./scripts/ctrl/analyze_stream.sh <stream.idx> [stream.raw]
#
# If stream.raw is provided and nodrop-dump exists, kernel event timestamp span
# (first/last event ts) is also summarized.

if [ "$#" -lt 1 ]; then
  echo "Usage: $0 <stream.idx> [stream.raw]" >&2
  exit 2
fi

IDX="$1"
RAW="${2:-}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DUMP_BIN="${SCRIPT_DIR}/nodrop-dump"

if [ ! -f "${IDX}" ]; then
  echo "[analyze_stream] idx not found: ${IDX}" >&2
  exit 1
fi

awk '
BEGIN {
  n=0; total_events=0; total_bytes=0; total_fetch_ns=0;
  first_start=0; last_end=0;
}
{
  # fetch_start_ns fetch_end_ns events bytes raw_off_start raw_off_end
  s=$1+0; e=$2+0; c=$3+0; b=$4+0;
  d=e-s;
  if (d < 0) d=0;
  n++;
  dur[n]=d;
  total_events += c;
  total_bytes += b;
  total_fetch_ns += d;
  if (first_start == 0 || s < first_start) first_start = s;
  if (e > last_end) last_end = e;
}
END {
  if (n == 0) {
    print "fetch_batches=0";
    print "total_events=0";
    print "total_bytes=0";
    print "note=no fetched batches in idx";
    exit 0;
  }

  # simple insertion sort for percentiles
  for (i=2; i<=n; i++) {
    v=dur[i]; j=i-1;
    while (j>=1 && dur[j] > v) { dur[j+1]=dur[j]; j--; }
    dur[j+1]=v;
  }

  p50_idx = int((n-1)*0.50)+1;
  p95_idx = int((n-1)*0.95)+1;
  p99_idx = int((n-1)*0.99)+1;

  wall_ns = last_end - first_start;
  if (wall_ns <= 0) wall_ns = total_fetch_ns;

  ev_per_wall_sec = (wall_ns > 0) ? (total_events * 1e9 / wall_ns) : 0;
  ev_per_fetch_sec = (total_fetch_ns > 0) ? (total_events * 1e9 / total_fetch_ns) : 0;
  bytes_per_wall_sec = (wall_ns > 0) ? (total_bytes * 1e9 / wall_ns) : 0;

  printf("fetch_batches=%d\n", n);
  printf("total_events=%d\n", total_events);
  printf("total_bytes=%d\n", total_bytes);
  printf("window_start_ns=%d\n", first_start);
  printf("window_end_ns=%d\n", last_end);
  printf("window_sec=%.6f\n", wall_ns/1e9);
  printf("fetch_time_sec=%.6f\n", total_fetch_ns/1e9);
  printf("throughput_events_per_sec_wall=%.3f\n", ev_per_wall_sec);
  printf("throughput_events_per_sec_fetch_only=%.3f\n", ev_per_fetch_sec);
  printf("throughput_bytes_per_sec_wall=%.3f\n", bytes_per_wall_sec);
  printf("fetch_latency_ms_p50=%.3f\n", dur[p50_idx]/1e6);
  printf("fetch_latency_ms_p95=%.3f\n", dur[p95_idx]/1e6);
  printf("fetch_latency_ms_p99=%.3f\n", dur[p99_idx]/1e6);
}
' "${IDX}"

if [ -n "${RAW}" ]; then
  if [ ! -f "${RAW}" ]; then
    echo "[analyze_stream] raw not found: ${RAW}" >&2
    exit 1
  fi
  if [ ! -x "${DUMP_BIN}" ]; then
    echo "kernel_event_ts_span=unavailable (nodrop-dump not found at ${DUMP_BIN})"
    exit 0
  fi

  "${DUMP_BIN}" "${RAW}" 2>/dev/null | awk '
  NR==1 { first=$1 }
  { last=$1; n++ }
  END {
    if (n == 0) {
      print "kernel_event_count=0";
      print "kernel_first_event_ns=0";
      print "kernel_last_event_ns=0";
      print "kernel_event_span_sec=0";
    } else {
      span=last-first;
      if (span < 0) span=0;
      printf("kernel_event_count=%d\n", n);
      printf("kernel_first_event_ns=%s\n", first);
      printf("kernel_last_event_ns=%s\n", last);
      printf("kernel_event_span_sec=%.6f\n", span/1e9);
    }
  }'
fi

