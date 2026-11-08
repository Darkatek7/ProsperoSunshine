#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# End-to-end smoke test: serverinfo -> /launch (pair.py) -> stream_test -> ffmpeg verification -> /cancel.  The client must already be paired (pair.py pair).
#
#   tools/moonlight/run_smoke.sh [host] [duration_s] [extra pair.py launch args, e.g. --corever]
#   STREAM_ARGS="--bitrate 20000" passes extra options to stream_test.
#   VIDEO_FORMAT=hevc checks the output as HEVC (with STREAM_ARGS="--hevc").
#   NO_CANCEL=1 skips the /cancel at the end.
#   CLIENT_DIR=out/moonlight-client2 uses another paired identity (a second device), OUT_TAG=2 names its output files
#   apart, so two clients can run at once.
#   DOCKER_ARGS="--cap-add NET_ADMIN" and PRE_CMD="<shell>" run the client container with extra
#   rights and a setup command first, e.g. to inject packet loss (see loss_test.sh).
#   The client is the Windows program from build_windows.sh if it is built, so that Docker's network doesn't falsify the
#   result, and the one in the container with DOCKER_ARGS or PRE_CMD, or with NATIVE=0.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
HOST=${1:-${PS5_HOST:?pass the console address or set PS5_HOST}}
DUR=${2:-20}
shift 2 2>/dev/null || true
LAUNCH_ARGS=("$@")
if command -v py >/dev/null 2>&1; then PY=(py -3); else PY=(python3); fi
REPO_WIN=$(pwd -W 2>/dev/null || pwd)
IMAGE=${IMAGE:-prosperosunshine-sdk:v0.45}
OUTDIR=out/moonlight
TAG=${OUT_TAG:-}
ID_ARGS=()
[ -n "${CLIENT_DIR:-}" ] && ID_ARGS=(--dir "$CLIENT_DIR")
mkdir -p "$OUTDIR" out/moonlight-client
dock() { MSYS_NO_PATHCONV=1 docker run --rm ${DOCKER_ARGS:-} --mount "type=bind,source=$REPO_WIN,target=/repo" "$IMAGE" bash -c "$1"; }

if [ -z "${NATIVE:-}" ]; then
  NATIVE=0
  [ -f "$OUTDIR/stream_test.exe" ] && [ -z "${DOCKER_ARGS:-}${PRE_CMD:-}" ] && NATIVE=1
fi
if [ "$NATIVE" = 1 ]; then
  [ -f "$OUTDIR/stream_test.exe" ] || { echo "build first: tools/moonlight/build_windows.sh"; exit 2; }
else
  [ -f "$OUTDIR/stream_test" ] || { echo "build first: tools/moonlight/build.sh"; exit 2; }
fi

"${PY[@]}" tools/moonlight/pair.py --host "$HOST" "${ID_ARGS[@]}" info >"$OUTDIR/serverinfo$TAG.txt" || { cat "$OUTDIR/serverinfo$TAG.txt"; exit 1; }
LJ=$("${PY[@]}" tools/moonlight/pair.py --host "$HOST" "${ID_ARGS[@]}" launch "${LAUNCH_ARGS[@]}") || { echo "launch failed"; exit 1; }
echo "launch: $LJ"
getj() { "${PY[@]}" -c "import json,sys; print(json.loads(sys.argv[1])['$1'])" "$LJ"; }
RTSP=$(getj sessionUrl0); RIKEY=$(getj rikey); RIKEYID=$(getj rikeyid)
APPVER=$(getj appversion); GFE=$(getj gfe); CODEC=$(getj codec_mode)

rm -f "out/moonlight-test$TAG.h264"
CLIENT_ARGS="--addr $HOST --rtsp-url '$RTSP' --rikey $RIKEY --rikeyid $RIKEYID --appversion '$APPVER' --gfe '$GFE' --codec-mode $CODEC --out out/moonlight-test$TAG.h264 --audio-out out/moonlight-test$TAG.s16 --duration $DUR ${STREAM_ARGS:-}"
run_client() {
  if [ "$NATIVE" = 1 ]; then
    eval "MSYS_NO_PATHCONV=1 timeout $((DUR+60)) out/moonlight/stream_test.exe $CLIENT_ARGS"
  else
    dock "cd /repo && ${PRE_CMD:-true} && timeout $((DUR+60)) out/moonlight/stream_test $CLIENT_ARGS"
  fi
}
run_client 2>&1 | tee "$OUTDIR/stream_test$TAG.log"
RC=${PIPESTATUS[0]}
echo "stream_test exit code: $RC"
# NO_CANCEL=1 leaves the session app running, for a second client that resumes it.
[ -n "${NO_CANCEL:-}" ] || "${PY[@]}" tools/moonlight/pair.py --host "$HOST" "${ID_ARGS[@]}" cancel
if [ -s "out/moonlight-test$TAG.h264" ]; then
  dock "cd /repo && ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=codec_name,profile,width,height,avg_frame_rate,r_frame_rate,nb_read_frames -of default=nw=1 -f ${VIDEO_FORMAT:-h264} out/moonlight-test$TAG.h264; echo '--- ffmpeg full decode (-xerror):'; ffmpeg -v error -xerror -f ${VIDEO_FORMAT:-h264} -i out/moonlight-test$TAG.h264 -f null - && echo DECODE_OK"
fi
exit $RC
