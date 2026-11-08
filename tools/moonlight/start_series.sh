#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Starts many short streams in a row, alternating H.264 and HEVC, to catch rare failed starts. Stops if someone else
# is streaming. The log of every failed run stays in out/moonlight/series/.
#
#   tools/moonlight/start_series.sh [host] [runs] [seconds]
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
HOST=${1:-${PS5_HOST:?pass the console address or set PS5_HOST}}
RUNS=${2:-100}
DUR=${3:-5}
OUTDIR=out/moonlight/series
mkdir -p "$OUTDIR"
# A healthy run delivers at least this many frames; the first ones can come late.
MIN_FRAMES=$((DUR * 60 * 8 / 10))

fails=0
for ((run = 1; run <= RUNS; run++)); do
  state=$(curl -s -m 4 "http://$HOST:47989/serverinfo" | grep -oE '<state>[^<]*')
  if [ "$state" != "<state>SUNSHINE_SERVER_FREE" ]; then
    echo "run $run: host not free (${state:-no answer}), stopping"
    break
  fi
  codec=h264 args="--no-script"
  if ((run % 2 == 0)); then codec=hevc args="--no-script --hevc"; fi
  start=$SECONDS
  log=$(VIDEO_FORMAT=$codec STREAM_ARGS="$args" bash tools/moonlight/run_smoke.sh "$HOST" "$DUR" 2>&1)
  rc=$?
  frames=$(printf '%s\n' "$log" | sed -n 's/^video_frames  *\([0-9]*\).*/\1/p' | head -n 1)
  gaps=$(printf '%s\n' "$log" | sed -n 's/^video_frame_gaps  *\([0-9]*\).*/\1/p' | head -n 1)
  result=ok
  if [ "$rc" != 0 ] || [ "${frames:-0}" -lt "$MIN_FRAMES" ] || ! printf '%s\n' "$log" | grep -q DECODE_OK; then
    result=FAIL
    fails=$((fails + 1))
    printf '%s\n' "$log" >"$OUTDIR/fail-$run.log"
  fi
  echo "run $run $codec rc=$rc frames=${frames:-0} gaps=${gaps:-?} $((SECONDS - start))s $result"
  sleep 2
done
echo "series done: $((run - 1)) runs, $fails failed"
[ "$fails" = 0 ]
