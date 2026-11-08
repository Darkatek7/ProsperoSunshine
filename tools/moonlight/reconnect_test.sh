#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# P6 acceptance: N consecutive sessions (launch, stream, stop, cancel). Each must deliver at least
# 4 s worth of frames that decode without error.
#
#   tools/moonlight/reconnect_test.sh [host] [runs] [seconds]
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
HOST=${1:-${PS5_HOST:?pass the console address or set PS5_HOST}}
RUNS=${2:-10}
SECS=${3:-6}
ok=0
for i in $(seq 1 "$RUNS"); do
    log=out/moonlight/reconnect_$i.log
    bash tools/moonlight/run_smoke.sh "$HOST" "$SECS" >"$log" 2>&1
    frames=$(grep -m1 '^video_frames ' "$log" | awk '{print $2}')
    decoded=$(grep -c DECODE_OK "$log")
    if [[ "${frames:-0}" -ge $((SECS * 60 - 120)) && "$decoded" -ge 1 ]]; then
        ok=$((ok + 1))
        echo "run $i: ok frames=$frames"
    else
        echo "run $i: FAIL frames=${frames:-none} decoded=$decoded (see $log)"
    fi
done
echo "reconnect result: $ok/$RUNS"
[[ $ok -eq $RUNS ]]
