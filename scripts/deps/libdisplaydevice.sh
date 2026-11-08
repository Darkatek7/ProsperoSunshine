#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck disable=SC2154  # sdk, prefix, work, src_* and the rest come from scripts/deps.sh
# Only the platform-neutral "common" part: Sunshine's display_device.h and logging.cpp need its
# types and logger. Display configuration itself does not exist on the PS5.
set -euo pipefail
s="$src_libdisplaydevice/src/common"
b="$work/libdisplaydevice-build"
rm -rf "$b" && mkdir -p "$b"
for f in "$s"/*.cpp; do
    "$sdk/bin/prospero-clang++" -std=c++23 -fexperimental-library -O2 -fPIC -I"$s/include" -isystem "$prefix/include" \
        -c "$f" -o "$b/$(basename "$f" .cpp).o"
done
"$sdk/bin/prospero-ar" rcs "$prefix/lib/libdisplaydevice_common.a" "$b"/*.o
cp -r "$s/include/display_device" "$prefix/include/"
cp "$src_libdisplaydevice/LICENSE" "$prefix/licenses/libdisplaydevice.txt"
