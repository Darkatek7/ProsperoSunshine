#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck disable=SC2154  # sdk, prefix, work, src_* and the rest come from scripts/deps.sh
# Opus: float build with runtime CPU detection.
set -euo pipefail
b="$work/opus-build"
rm -rf "$b"
"$sdk/bin/prospero-cmake" -S "$src_opus" -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_BUILD_TESTING=OFF \
    -DCMAKE_VERBOSE_MAKEFILE=OFF >/dev/null
cmake --build "$b" >/dev/null
cmake --install "$b" >/dev/null
cp "$src_opus/COPYING" "$prefix/licenses/opus.txt"
