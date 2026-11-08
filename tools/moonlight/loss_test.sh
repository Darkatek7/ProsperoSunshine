#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# P6.2: stream while the client container randomly drops a share of the incoming video UDP packets.
# Docker Desktop's NAT does not keep the host's source port, so video is matched by size instead
# (video packets carry ~1 KB, audio packets well under 600 bytes). FEC should repair light loss;
# heavier loss must recover through the client's IDR request instead of freezing.
#
#   tools/moonlight/loss_test.sh [host] [seconds] [drop probability, e.g. 0.03]
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
HOST=${1:-${PS5_HOST:?pass the console address or set PS5_HOST}}
SECS=${2:-30}
P=${3:-0.03}
DOCKER_ARGS="--cap-add NET_ADMIN" \
PRE_CMD="apt-get -qq update >/dev/null && apt-get -qq install -y iptables >/dev/null && iptables -A INPUT -p udp -m length --length 600:65535 -m statistic --mode random --probability $P -j DROP" \
    bash tools/moonlight/run_smoke.sh "$HOST" "$SECS"
