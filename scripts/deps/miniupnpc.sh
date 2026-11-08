#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# MiniUPnPc: the static library only, for the optional UPnP port mapping (src/net/upnp_client.cpp).
set -euo pipefail
b="$work/miniupnpc-build"
rm -rf "$b"
"$sdk/bin/prospero-cmake" -S "$src_miniupnpc" -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DUPNPC_BUILD_STATIC=ON -DUPNPC_BUILD_SHARED=OFF -DUPNPC_BUILD_TESTS=OFF \
    -DUPNPC_BUILD_SAMPLE=OFF -DCMAKE_VERBOSE_MAKEFILE=OFF >/dev/null
cmake --build "$b" >/dev/null
cmake --install "$b" >/dev/null
cp "$src_miniupnpc/LICENSE" "$prefix/licenses/miniupnpc.txt"
