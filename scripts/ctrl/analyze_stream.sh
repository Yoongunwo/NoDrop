#!/usr/bin/env bash
set -euo pipefail

# Required metrics summary for NoDrop fetch-only experiments.
#
# Usage:
#   ./scripts/ctrl/analyze_stream.sh <stream.idx> <stream.raw> <stat_before.txt> <stat_after.txt>
#
# Output fields:
#   kernel_syscalls_called
#   collected_events
#   missed_events
#   drop_rate_percent
#   throughput_events_per_sec
#   latency_ms_p50/p95/p99

if [ "$#" -ne 4 ]; then
  echo "Usage: $0 <stream.idx> <stream.raw> <stat_before.txt> <stat_after.txt>" >&2
  exit 2
fi

IDX="$1"
RAW="$2"
STAT_BEFORE="$3"
STAT_AFTER="$4"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DUMP_BIN="${SCRIPT_DIR}/nodrop-dump"

for f in "${IDX}" "${RAW}" "${STAT_BEFORE}" "${STAT_AFTER}"; do
  if [ ! -f "${f}" ]; then
    echo "[analyze_stream] file not found: ${f}" >&2
    exit 1
  fi
done

if [ ! -x "${DUMP_BIN}" ]; then
  echo "[analyze_stream] nodrop-dump not found/executable: ${DUMP_BIN}" >&2
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
  END { if (!ok) exit 1; }' "$f"
}

read -r n1 d1 u1 < <(parse_stat_file "${STAT_BEFORE}")
read -r n2 d2 u2 < <(parse_stat_file "${STAT_AFTER}")

read -r delta_n delta_d delta_u < <(
  awk -v n1="${n1}" -v d1="${d1}" -v u1="${u1}" -v n2="${n2}" -v d2="${d2}" -v u2="${u2}" '
  BEGIN {
    dn=n2-n1; dd=d2-d1; du=u2-u1;
    if (dn<0) dn=0; if (dd<0) dd=0; if (du<0) du=0;
    print dn, dd, du;
  }'
)

read -r fetch_batches wall_ns p50_ms p95_ms p99_ms < <(
  awk '
  BEGIN { n=0; first=0; last=0; }
  {
    s=$1+0; e=$2+0;
    d=e-s; if (d<0) d=0;
    n++; dur[n]=d;
    if (first==0 || s<first) first=s;
    if (e>last) last=e;
  }
  END {
    if (n==0) { print 0, 0, 0, 0, 0; exit 0; }

    for (i=2; i<=n; i++) {
      v=dur[i]; j=i-1;
      while (j>=1 && dur[j] > v) { dur[j+1]=dur[j]; j--; }
      dur[j+1]=v;
    }

    p50 = dur[int((n-1)*0.50)+1] / 1e6;
    p95 = dur[int((n-1)*0.95)+1] / 1e6;
    p99 = dur[int((n-1)*0.99)+1] / 1e6;
    wall = last-first; if (wall < 0) wall=0;

    printf "%d %.0f %.6f %.6f %.6f\n", n, wall, p50, p95, p99;
  }' "${IDX}"
)

kernel_event_count="$("${DUMP_BIN}" "${RAW}" 2>/dev/null | awk 'END{print NR+0}')"

drop_rate_percent="$(awk -v miss="${delta_u}" -v called="${delta_n}" 'BEGIN{
  if (called<=0) print "0.000000";
  else printf "%.6f", (miss*100.0/called);
}')"

throughput_eps="$(awk -v c="${kernel_event_count}" -v w="${wall_ns}" 'BEGIN{
  if (w<=0) print "0.000";
  else printf "%.3f", (c*1e9/w);
}')"

echo "kernel_syscalls_called=${delta_n}"
echo "collected_events=${kernel_event_count}"
echo "missed_events=${delta_u}"
echo "drop_rate_percent=${drop_rate_percent}"
echo "throughput_events_per_sec=${throughput_eps}"
echo "latency_ms_p50=${p50_ms}"
echo "latency_ms_p95=${p95_ms}"
echo "latency_ms_p99=${p99_ms}"
echo "fetch_batches=${fetch_batches}"

