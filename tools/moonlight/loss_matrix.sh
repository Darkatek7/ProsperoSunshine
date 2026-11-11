#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Streams once for every combination of a loss pattern, a bitrate and a codec, and prints one table: what reached the
# client, how often it asked for an IDR, and what the host counted and sent. Made for issue 37, where a lossy path made
# the host send IDR after IDR and twice the bitrate the client asked for.
#
#   PS5_HOST=<console> bash tools/moonlight/loss_matrix.sh [seconds per run]
#
# Losses hit only the video packets (UDP from the console's port 47998), inside the client's container:
#   random  every packet is dropped with the given probability (iptables, as in loss_test.sh)
#   burst   the same share, lost in runs of BURST_LEN packets on average (netem's Gilbert-Elliott model)
#   buffer  a bottleneck in front of the client that drains at BUFFER_KBIT kbit/s and holds BUFFER_PACKETS packets, like
#           the relay in issue 37 that kept the first 39 packets of every burst (netem rate and limit); BUFFER_KBIT=6x
#           drains at six times the stream's bitrate (20 Mbit/s for 3.4 Mbit/s), so that only bursts overflow it
# burst and buffer need the kernel modules ifb, sch_netem, sch_ingress, cls_u32 and act_mirred on the Docker host
# (`sudo modprobe -a ifb sch_netem sch_ingress cls_u32 act_mirred`); without them their rows are skipped.
#
# The matrix, from the environment:
#   LOSSES="0 1 3 5 10"           percent, for random and burst
#   KINDS="random burst buffer"
#   RATES="3400@30 20000@60"      kbit/s@fps; 3400@30 is issue 37's stream
#   CODECS="h264 hevc"
#   BURST_LEN=8  BUFFER_KBIT=6x (or kbit/s)  BUFFER_PACKETS=39  BUFFER_LOSSES="0"
#   STREAM_ARGS="--no-script"     more stream_test options; without --no-script the client presses buttons
#   PACKET_SIZE=1024              the client's video packets: 1024 as Moonlight over the internet (the host then paces
#                                 as for a remote client), 1392 as Moonlight on a LAN (not paced; needs a client built after
#                                 --packet-size was added to stream_test.c)
#   PAUSE=8                       seconds between runs, for the host to end the session
# Each run's client log is kept in out/loss_matrix/, the table in out/loss_matrix/table.txt.
#
# The host's columns (recoveries, sent kbit/s, round trip) come from STREAMS on the control socket (TCP 47995), which
# answers from the LAN only while the empty file /data/prosperosunshine/control_lan exists; without it they show "-".
# The client runs in the SDK container on Docker's bridge network (shaping it with --network host would shape the
# machine). Build it once with tools/moonlight/build.sh and pair it with pair.py. Something must move on the screen:
# the home screen is too quiet to load the encoder.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
HOST=${PS5_HOST:?set PS5_HOST to the console address}
SECS=${1:-${SECS:-30}}
LOSSES=${LOSSES:-0 1 3 5 10}
KINDS=${KINDS:-random burst buffer}
RATES=${RATES:-3400@30 20000@60}
CODECS=${CODECS:-h264 hevc}
BURST_LEN=${BURST_LEN:-8}
BUFFER_KBIT=${BUFFER_KBIT:-6x}
BUFFER_PACKETS=${BUFFER_PACKETS:-39}
BUFFER_LOSSES=${BUFFER_LOSSES:-0}
BASE_STREAM_ARGS=${STREAM_ARGS:---no-script}
PAUSE=${PAUSE:-8}
PACKET_SIZE=${PACKET_SIZE:-}
VIDEO_PORT=${VIDEO_PORT:-47998}
CTL_PORT=${CTL_PORT:-47995}
IMAGE=${IMAGE:-prosperosunshine-sdk:v0.45}
OUTDIR=out/moonlight
RUNDIR=out/loss_matrix
DEBS=$RUNDIR/debs
TABLE=$RUNDIR/table.txt
mkdir -p "$RUNDIR"
[ -f "$OUTDIR/stream_test" ] || { echo "build the client first: bash tools/moonlight/build.sh"; exit 2; }
# run_smoke.sh writes its log there from this shell.
[ -w "$OUTDIR" ] || { echo "$OUTDIR isn't writable (a container made it as root?): sudo chown -R $(id -u):$(id -g) $OUTDIR"; exit 2; }

# One command on the control socket; prints the answer up to END. Empty when the socket doesn't answer.
ctl() {
  python3 - "$HOST" "$CTL_PORT" "$1" <<'EOF' 2>/dev/null
import socket, sys
try:
    s = socket.create_connection((sys.argv[1], int(sys.argv[2])), timeout=3)
    s.sendall(sys.argv[3].encode() + b"\n")
    buf = b""
    while not buf.rstrip().endswith(b"END"):
        d = s.recv(4096)
        if not d:
            break
        buf += d
    print(buf.decode(errors="replace"), end="")
except OSError:
    pass
EOF
}

# Someone may be streaming: go on only while the host is free.
# (Not grep -q: it would stop reading at the state line, pair.py then dies of SIGPIPE and pipefail calls the host busy.)
if ! python3 tools/moonlight/pair.py --host "$HOST" info 2>&1 | grep "state = SUNSHINE_SERVER_FREE" >/dev/null; then
  echo "the host is not free (someone streams, or it doesn't answer); stopping"
  exit 1
fi
HOST_COLUMNS=1
if ! ctl STREAMS | grep -q "^END"; then
  HOST_COLUMNS=0
  echo "note: the control socket doesn't answer from here (no control_lan on the console?); host columns stay empty"
fi

# The client's container gets iptables and tc from packages fetched once, so that a run doesn't wait for apt while the
# host holds its launched session.
if ! ls "$DEBS"/*.deb >/dev/null 2>&1; then
  echo "fetching iptables and iproute2 for the client's container (once, into $DEBS)"
  mkdir -p "$DEBS"
  docker run --rm --mount "type=bind,source=$(pwd),target=/repo" "$IMAGE" bash -c \
    "apt-get -qq update >/dev/null && apt-get -qq install -y --download-only iptables iproute2 >/dev/null && cp /var/cache/apt/archives/*.deb /repo/$DEBS/ && chown -R $(id -u):$(id -g) /repo/$DEBS" ||
    { echo "fetching the packages failed"; exit 1; }
fi
SETUP="dpkg -i /repo/$DEBS/*.deb >/dev/null 2>&1; command -v iptables >/dev/null && command -v tc >/dev/null"
# Sends the video packets through netem on an ifb device: netem only shapes what leaves a device.
ifb_chain() {
  echo "ip link add ifb0 type ifb && ip link set ifb0 up && tc qdisc add dev eth0 handle ffff: ingress && tc filter add dev eth0 parent ffff: protocol ip u32 match ip protocol 17 0xff match ip sport $VIDEO_PORT 0xffff action mirred egress redirect dev ifb0 && tc qdisc add dev ifb0 root netem $1"
}

NETEM=1
case " $KINDS " in
  *" burst "* | *" buffer "*)
    if ! docker run --rm --cap-add NET_ADMIN --mount "type=bind,source=$(pwd),target=/repo" "$IMAGE" bash -c \
      "$SETUP && $(ifb_chain "loss gemodel 1% 10% 100% 0%")" >/dev/null 2>&1; then
      NETEM=0
      echo "note: netem on ifb doesn't work in the container; burst and buffer rows are skipped"
      echo "      (on the Docker host: sudo modprobe -a ifb sch_netem sch_ingress cls_u32 act_mirred)"
    fi
    ;;
esac

# Gilbert-Elliott with every packet lost in the bad state: r = 1/BURST_LEN leaves it, p = share x r / (1 - share)
# enters it, so the bad state holds the given share of the packets and a run lasts BURST_LEN packets on average.
gemodel() {
  awk -v l="$1" -v b="$BURST_LEN" 'BEGIN { s = l / 100; r = 1 / b; p = s * r / (1 - s); printf "loss gemodel %.4f%% %.4f%% 100%% 0%%", p * 100, r * 100 }'
}

# Asks STREAMS every 2 s while the client runs and writes "<seconds since the last poll> <the stream's line>".
poll_streams() {
  local last=$(date +%s.%N) now
  while :; do
    sleep 2
    now=$(date +%s.%N)
    ctl STREAMS | grep "^OK uuid=" | while IFS= read -r line; do
      echo "$(awk -v a="$last" -v b="$now" 'BEGIN { printf "%.3f", b - a }') $line"
    done
    last=$now
  done
}

# The host's figures from the polls: the stream seen most often (the one this run started), its time-weighted sent
# rate (each STREAMS answers for the time since the one before; the first poll is left out, it covers the start),
# its last recovery count and its last round trip.
host_figures() {
  awk '
    { for (i = 2; i <= NF; i++) { split($i, kv, "="); f[kv[1]] = kv[2] }
      u = f["uuid"]; n[u]++; dt[u, n[u]] = $1; kb[u, n[u]] = f["sent_kbps"]; rec[u] = f["recoveries"]; rtt[u] = f["rtt_ms"] }
    END {
      best = ""; for (u in n) if (best == "" || n[u] > n[best]) best = u
      if (best == "") { print "- - -"; exit }
      w = 0; s = 0
      for (i = 2; i <= n[best]; i++) if (kb[best, i] ~ /^[0-9]+$/) { w += dt[best, i]; s += dt[best, i] * kb[best, i] }
      printf "%s %s %s\n", rec[best], (w > 0 ? sprintf("%.0f", s / w) : "-"), rtt[best]
    }' "$1"
}

# The client's figures from its log. (Its rtp_video line has no FEC counts: moonlight-common-c fills them for audio only.)
client_figures() {
  [ -f "$1" ] || { echo "- - - 0 - -"; return; }
  awk '
    /^video_frames / { frames = $2 }
    /^video_idr_frames / { idr = $2 }
    /^video_frame_gaps / { sub(/\).*/, ""); missing = $NF }
    /^video_max_gap_ms / { gap = $2 }
    /^video_bitrate_kbps / { kbps = $2 }
    /lc: IDR frame request sent/ { asked++ }
    END { printf "%s %s %s %d %s %s\n", (frames == "" ? "-" : frames), (missing == "" ? "-" : missing),
                 (idr == "" ? "-" : idr), asked, (kbps == "" ? "-" : kbps), (gap == "" ? "-" : gap) }' "$1"
}

FORMAT="%-7s %5s %6s %4s %-5s | %7s %7s %5s %5s | %5s %7s %7s %5s | %6s %3s\n"
header() {
  printf "$FORMAT" kind loss% kbps fps codec frames missing idr asked hrec sent rcvd rtt maxgap rc
  printf "$FORMAT" "" "" "" "" "" "" "" "" "" "" kbit/s kbit/s ms ms ""
}
{
  echo "# loss matrix, $(date -u +%Y-%m-%dT%H:%MZ), $SECS s per run, packets of ${PACKET_SIZE:-1024} bytes"
  echo "# frames/missing/idr: what the client received; asked: its IDR requests; hrec: recoveries the host counted;"
  echo "# sent: video the host sent (STREAMS), rcvd: video the client received, rtt: the host's last round trip;"
  echo "# maxgap: the longest time without a frame at the client; rc: run_smoke.sh's exit code"
  header
} | tee "$TABLE"

run() {  # kind loss kbps fps codec netem-or-iptables-setup
  local kind=$1 loss=$2 kbps=$3 fps=$4 codec=$5 shape=$6
  local name="${kind}_${loss}_${kbps}_${fps}_${codec}" streams="$RUNDIR/${kind}_${loss}_${kbps}_${fps}_${codec}.streams"
  local args="$BASE_STREAM_ARGS --bitrate $kbps --fps $fps${PACKET_SIZE:+ --packet-size $PACKET_SIZE}" format=h264
  [ "$codec" = hevc ] && { args="$args --hevc"; format=hevc; }
  : >"$streams"
  rm -f "$OUTDIR/stream_test.log"
  local poller=""
  if [ "$HOST_COLUMNS" = 1 ]; then
    poll_streams >>"$streams" &
    poller=$!
  fi
  DOCKER_ARGS="--cap-add NET_ADMIN" PRE_CMD="$SETUP${shape:+ && $shape}" STREAM_ARGS="$args" VIDEO_FORMAT=$format \
    bash tools/moonlight/run_smoke.sh "$HOST" "$SECS" --mode "1920x1080x$fps" >"$RUNDIR/$name.run" 2>&1
  local rc=$?
  [ -n "$poller" ] && { kill "$poller" 2>/dev/null; wait "$poller" 2>/dev/null; }
  cp "$OUTDIR/stream_test.log" "$RUNDIR/$name.log" 2>/dev/null
  read -r frames missing idr asked rcvd gap < <(client_figures "$RUNDIR/$name.log")
  read -r hrec sent rtt < <(host_figures "$streams")
  printf "$FORMAT" "$kind" "$loss" "$kbps" "$fps" "$codec" "$frames" "$missing" "$idr" "$asked" "$hrec" "$sent" "$rcvd" \
    "$rtt" "$gap" "$rc" | tee -a "$TABLE"
  sleep "$PAUSE"
}

for codec in $CODECS; do
  for rate in $RATES; do
    kbps=${rate%@*}
    fps=${rate#*@}
    [ "$fps" = "$rate" ] && fps=60
    for kind in $KINDS; do
      case $kind in
        random)
          for loss in $LOSSES; do
            shape=""
            [ "$loss" != 0 ] && shape="iptables -A INPUT -p udp --sport $VIDEO_PORT -m statistic --mode random --probability $(awk -v l="$loss" 'BEGIN { printf "%.4f", l / 100 }') -j DROP"
            run random "$loss" "$kbps" "$fps" "$codec" "$shape"
          done
          ;;
        burst)
          [ "$NETEM" = 1 ] || continue
          for loss in $LOSSES; do
            [ "$loss" = 0 ] && continue  # the same as random 0
            run burst "$loss" "$kbps" "$fps" "$codec" "$(ifb_chain "$(gemodel "$loss")")"
          done
          ;;
        buffer)
          [ "$NETEM" = 1 ] || continue
          for loss in $BUFFER_LOSSES; do
            drain=$BUFFER_KBIT
            [ "${drain%x}" != "$drain" ] && drain=$((${drain%x} * kbps))
            netem="rate ${drain}kbit limit $BUFFER_PACKETS"
            [ "$loss" != 0 ] && netem="loss random $loss% $netem"
            run buffer "$loss" "$kbps" "$fps" "$codec" "$(ifb_chain "$netem")"
          done
          ;;
        *) echo "unknown kind $kind" ;;
      esac
    done
  done
done
echo "table: $TABLE, logs: $RUNDIR/"
