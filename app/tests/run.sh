#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Builds and runs the app's PC tests (app/tests/psapp_test.cpp) with the host compiler, e.g. in the
# app build image: docker run --rm -v "$PWD":/repo prosperosunshine-app:2 bash app/tests/run.sh
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/include/app"
ln -s "$repo/app/src/psapp" "$work/include/app/psapp"
cxx=$(command -v clang++-18 || command -v clang++ || command -v g++)
"$cxx" -std=c++20 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -I "$work/include" \
    "$repo/app/tests/psapp_test.cpp" "$repo/app/src/psapp/conf.cpp" "$repo/app/src/psapp/autoload.cpp" \
    "$repo/app/src/psapp/diag.cpp" "$repo/app/src/psapp/web.cpp" \
    -o "$work/psapp_test"
"$work/psapp_test"
