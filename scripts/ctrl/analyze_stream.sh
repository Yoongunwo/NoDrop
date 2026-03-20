#!/usr/bin/env bash
set -euo pipefail

# Analyze stream index produced by fetch_stream.sh.
#
# Usage:
#   ./scripts/ctrl/analyze_stream.sh <stream.idx> [stream.raw] [stat_before.txt] [stat_after.txt]
#
# If stream.raw is provided and nodrop-dump exists, kernel event timestamp span
# (first/last event ts) is also summarized.
# If stat_before/stat_after are provided (output of `./scripts/ctrl/ctrl stat`),
# drop deltas and drop rates for the experiment window are also summarized.

if [ "$#" -lt 1 ]; then
  echo "Usage: $0 <stream.idx> [stream.raw] [stat_before.txt] [stat_after.txt]" >&2
  exit 2
fi

IDX="$1"
RAW="${2:-}"
STAT_BEFORE="${3:-}"
STAT_AFTER="${4:-}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DUMP_BIN="${SCRIPT_DIR}/nodrop-dump"

if [ ! -f "${IDX}" ]; then
  echo "[analyze_stream] idx not found: ${IDX}" >&2
  exit 1
fi

parse_stat_file() {
  # prints: n_evts drop_evts drop_unsolved
  local f="$1"
  awk '
  NR==2 {
    if (NF >= 3) {
      print $1, $2, $3;
      ok=1;
    }
  }
  END {
    if (!ok) exit 1;
  }' "$f"
}

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

if [ -n "${STAT_BEFORE}" ] && [ -n "${STAT_AFTER}" ]; then
  if [ ! -f "${STAT_BEFORE}" ]; then
    echo "[analyze_stream] stat_before not found: ${STAT_BEFORE}" >&2
    exit 1
  fi
  if [ ! -f "${STAT_AFTER}" ]; then
    echo "[analyze_stream] stat_after not found: ${STAT_AFTER}" >&2
    exit 1
  fi

  if ! read -r n1 d1 u1 < <(parse_stat_file "${STAT_BEFORE}"); then
    echo "[analyze_stream] failed to parse stat_before: ${STAT_BEFORE}" >&2
    exit 1
  fi
  if ! read -r n2 d2 u2 < <(parse_stat_file "${STAT_AFTER}"); then
    echo "[analyze_stream] failed to parse stat_after: ${STAT_AFTER}" >&2
    exit 1
  fi

  awk -v n1="${n1}" -v d1="${d1}" -v u1="${u1}" -v n2="${n2}" -v d2="${d2}" -v u2="${u2}" '
  BEGIN {
    dn = n2 - n1;
    dd = d2 - d1;
    du = u2 - u1;
    if (dn < 0) dn = 0;
    if (dd < 0) dd = 0;
    if (du < 0) du = 0;

    total = dn + dd;
    drop_rate = (total > 0) ? (dd * 100.0 / total) : 0.0;
    unsolved_rate = (total + du > 0) ? (du * 100.0 / (total + du)) : 0.0;

    printf("delta_n_evts=%d\n", dn);
    printf("delta_drop_evts=%d\n", dd);
    printf("delta_drop_unsolved=%d\n", du);
    printf("drop_rate_internal_percent=%.6f\n", drop_rate);
    printf("drop_unsolved_rate_percent=%.6f\n", unsolved_rate);
  }'
fi
