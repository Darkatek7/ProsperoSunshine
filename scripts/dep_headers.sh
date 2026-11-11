#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck disable=SC2034  # DEP_FLAGS is read by the scripts that source this one
# Sourced by scripts/test_deps.sh and scripts/fuzz.sh: prepares the patched, header-only sources the Linux tests of the HTTP
# library need (Simple-Web-Server with patches/simple-web-server/*, Boost's headers with patches/boost/*) and sets DEP_FLAGS to
# the compiler flags that find them. It builds nothing for the PS5: scripts/deps.sh --sources fetches the pinned archives,
# checks their SHA256, applies the patches and extracts the trees into .cache/deps/src, where the host build finds them
# again (same stamp), so a CI run that built or restored them has nothing to do here. A patch that no longer applies, or an
# archive that doesn't match deps.lock, stops the caller. The trees are the ones the host is built from, not the headers
# of the build machine, so a change of a pin or a patch changes what the tests run against.
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
bash "$root/scripts/deps.sh" --sources simple-web-server boost || { echo "dep_headers: the sources could not be prepared" >&2; exit 1; }
src="$root/.cache/deps/src"
DEP_FLAGS=(-pthread -isystem "$src/simple-web-server")
# The CMake superproject keeps every library's headers in libs/<name>/include (the numeric ones one level deeper); the host's
# out/deps/include is the merge of them. Passing each directory spares a 700 MB copy. Boost's own tests and tools also have
# folders called include, so only the ones that hold a boost/ directory count.
while IFS= read -r dir; do
    [ -d "$dir/boost" ] && DEP_FLAGS+=(-isystem "$dir")
done < <(find "$src/boost/libs" -maxdepth 4 -type d -name include -not -path '*/test/*' -not -path '*/tools/*' | sort)
[ -f "$src/boost/libs/asio/include/boost/asio.hpp" ] || { echo "dep_headers: Boost's asio headers are missing" >&2; exit 1; }
