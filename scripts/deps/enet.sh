#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck disable=SC2154  # sdk, prefix, work, src_* and the rest come from scripts/deps.sh
# ENet: moonlight's fork, configured by its own CMake feature checks.
set -euo pipefail
b="$work/enet-build"
rm -rf "$b"
"$sdk/bin/prospero-cmake" -S "$src_enet" -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_VERBOSE_MAKEFILE=OFF >/dev/null
cmake --build "$b" >/dev/null
cmake --install "$b" >/dev/null
mv -f "$prefix/lib/static/libenet.a" "$prefix/lib/libenet.a" && rmdir "$prefix/lib/static"
cp "$src_enet/LICENSE" "$prefix/licenses/enet.txt"
