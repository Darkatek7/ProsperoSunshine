#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Builds and runs the app's PC tests (app/tests/psapp_test.cpp and settings_layout_test.cpp) with the host compiler, e.g.
# in the app build image: docker run --rm -v "$PWD":/repo prosperosunshine-app:2 bash app/tests/run.sh
# Twice: as the normal app and as the debug flavor (PROSPERO_DEBUG, src/psapp/flavor.hpp), whose option list may differ.
# SETTINGS_SVG=<file> also writes the settings page's layout there as an SVG picture (from the normal flavor).
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/include/app"
ln -s "$repo/app/src/psapp" "$work/include/app/psapp"
cxx=$(command -v clang++-18 || command -v clang++ || command -v g++)
sources=("$repo/app/tests/psapp_test.cpp" "$repo/app/tests/settings_layout_test.cpp" "$repo/app/src/psapp/conf.cpp"
    "$repo/app/src/psapp/autoload.cpp" "$repo/app/src/psapp/diag.cpp" "$repo/app/src/psapp/web.cpp"
    "$repo/app/src/psapp/settings_layout.cpp")
for flavor in normal debug; do
    defines=()
    [ "$flavor" = normal ] || defines=(-DPROSPERO_DEBUG=1)
    "$cxx" -std=c++20 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined "${defines[@]}" -I "$work/include" \
        "${sources[@]}" -o "$work/psapp_test_$flavor"
    echo "$flavor:"
    if [ "$flavor" = normal ] && [ -n "${SETTINGS_SVG:-}" ]; then
        "$work/psapp_test_$flavor" --svg "$SETTINGS_SVG"
    else
        "$work/psapp_test_$flavor"
    fi
done
