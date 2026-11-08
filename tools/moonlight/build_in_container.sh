#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Runs INSIDE the prosperosunshine-sdk container (see build.sh).
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

REPO=${REPO:-/repo}
SRC=$REPO/.cache/deps/src
B=${BUILD_DIR:-/tmp/mlc}
OUT=$REPO/out/moonlight

if ! dpkg -s libssl-dev libopus-dev >/dev/null 2>&1; then
  apt-get update -qq
  apt-get install -y --no-install-recommends libssl-dev libopus-dev pkg-config >/dev/null
fi

rm -rf "$B"
mkdir -p "$B" "$OUT"
# container-local copy; moonlight-common-c expects its submodules inside its tree
cp -r "$SRC/moonlight-common-c" "$B/src"
rm -rf "$B/src/enet" "$B/src/nanors"
cp -r "$SRC/enet" "$B/src/enet"
cp -r "$SRC/nanors" "$B/src/nanors"

export CC=clang-18 CXX=clang++-18 DEBIAN_FRONTEND=noninteractive
cmake -S "$B/src" -B "$B/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 >/dev/null
cmake --build "$B/build" 2>&1 | tail -5

ENET_LIB=$(find "$B/build" -name 'libenet.a' | head -1)
MLC_LIB=$(find "$B/build" -name 'libmoonlight-common-c.a' | head -1)
echo "libs: $MLC_LIB $ENET_LIB"

# shellcheck disable=SC2046  # pkg-config prints several flags, each its own word
$CC -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
  -I"$B/src/src" $(pkg-config --cflags opus) \
  "$REPO/tools/moonlight/stream_test.c" -o "$OUT/stream_test" \
  "$MLC_LIB" "$ENET_LIB" $(pkg-config --libs opus) -lssl -lcrypto -lpthread -lm
ls -l "$OUT/stream_test"
