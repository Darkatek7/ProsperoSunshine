#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# P4.3: kill the client while it holds D-pad down + R2, then check that the host lets go. The kill
# time is printed; compare it with the host's "CLIENT DISCONNECTED" and the console's
# "removeDevice" line for the virtual pad in the kernel log (TCP 3232).
#
#   tools/moonlight/hold_kill_test.sh [host]
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
HOST=${1:-${PS5_HOST:?pass the console address or set PS5_HOST}}
if command -v py >/dev/null 2>&1; then PY=(py -3); else PY=(python3); fi
REPO_WIN=$(pwd -W 2>/dev/null || pwd)
LJ=$("${PY[@]}" tools/moonlight/pair.py --host "$HOST" launch) || { echo "launch failed"; exit 1; }
getj() { "${PY[@]}" -c "import json,sys; print(json.loads(sys.argv[1])['$1'])" "$LJ"; }
MSYS_NO_PATHCONV=1 docker run -d --rm --name ml-hold --mount "type=bind,source=$REPO_WIN,target=/repo" \
    prosperosunshine-sdk:v0.45 bash -c "cd /repo && out/moonlight/stream_test --addr $HOST \
    --rtsp-url '$(getj sessionUrl0)' --rikey $(getj rikey) --rikeyid $(getj rikeyid) \
    --appversion '$(getj appversion)' --gfe '$(getj gfe)' --codec-mode $(getj codec_mode) \
    --out /tmp/hold.h264 --duration 30 > /repo/out/moonlight/hold_kill.log 2>&1" >/dev/null
for _ in $(seq 1 300); do
    grep -q "dpad down" out/moonlight/hold_kill.log 2>/dev/null && break
    sleep 0.05
done
docker kill ml-hold >/dev/null
echo "client killed while holding at $(date +%H:%M:%S.%N | cut -c1-12)"
grep -E "dpad down|release all" out/moonlight/hold_kill.log
