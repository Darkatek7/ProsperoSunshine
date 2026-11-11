#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Tests of the code that reads a request before anything else looks at it: Simple-Web-Server as pinned in deps.lock and
# patched by patches/simple-web-server/, configured the way the host's servers are (tests/deps/). They need the patched
# sources, which scripts/dep_headers.sh fetches and checks (no PS5 library is built), so they are not part of test.sh. Run
# it in the SDK image like the others:
#   docker run --rm --mount type=bind,source="$PWD",target=/repo prosperosunshine-sdk:v0.45 bash scripts/test_deps.sh [test ...]
# A test is a file tests/deps/<name>_test.cpp; give names to run only those. Servers listen on 127.0.0.1 with a port the
# system picks.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/dep_headers.sh
source "$root/scripts/dep_headers.sh"
mkdir -p "$root/out"
names=("$@")
if [ ${#names[@]} -eq 0 ]; then
    for f in "$root"/tests/deps/*_test.cpp; do names+=("$(basename "$f" .cpp)"); done
fi
for name in "${names[@]}"; do
    # The headers come as -isystem (Boost's are not warning-free); the tests' own code is held to the usual -Werror.
    clang++-18 -std=c++23 -stdlib=libstdc++ -Wall -Wextra -Werror -O1 -g -UNDEBUG -fsanitize=address,undefined \
        "${DEP_FLAGS[@]}" -I "$root/tests/deps" "$root/tests/deps/$name.cpp" -o "$root/out/$name"
    "$root/out/$name"
done
