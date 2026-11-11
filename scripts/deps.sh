#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Fetch the sources pinned in deps.lock, verify and patch them, and build static PS5 libraries into
# out/deps. Each library with a build step has its own scripts/deps/<name>.sh; finished builds leave
# a stamp so later runs only redo what changed. Pass library names to limit the run to those.
# "deps.sh --sources <name>..." only fetches, verifies, patches and extracts those sources into .cache/deps/src and builds
# nothing (the Linux tests of the HTTP library use it, scripts/dep_headers.sh); a patch that no longer applies stops it.
set -euo pipefail
sources_only=0
if [[ "${1:-}" == --sources ]]; then
    sources_only=1
    shift
    [[ $# -gt 0 ]] || { echo "deps.sh --sources: name the sources to prepare" >&2; exit 2; }
fi
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export sdk=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
export prefix="$root/out/deps"
export work=${DEPS_WORK:-/tmp/prospero-deps}
jobs=$(nproc)
export jobs
dl="$root/.cache/deps/dl"
mirror=${DEPS_MIRROR:-https://github.com/Darkatek7/ProsperoSunshine/releases/download/deps-mirror}
srcroot="$root/.cache/deps/src"
mkdir -p "$dl" "$srcroot"
[[ $sources_only -eq 1 ]] || mkdir -p "$prefix/include" "$prefix/lib" "$prefix/licenses" "$work"

# Source trees are extracted once into .cache/deps/src/<name>; a stamp records archive hash and
# patch set, so a changed pin or patch re-extracts the tree.
declare -A src version
order=()
while read -r name ver sha url; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    [[ $sources_only -eq 0 ]] || [[ " $* " == *" $name "* ]] || continue
    ext=${url##*/}; ext=${ext#*.}
    [[ "$ext" == tar.* ]] || ext=tar.gz
    file="$dl/$name-$ver.$ext"
    if [[ ! -f "$file" ]] && ! curl -fsSL --retry 2 -o "$file" "$url"; then
        # Archives from servers outside GitHub are mirrored in this repository's deps-mirror release (checked below
        # against deps.lock like any other download).
        echo "Download of $name failed, trying the mirror"
        rm -f "$file"
        curl -fsSL --retry 2 -o "$file" "$mirror/${url##*/}"
    fi
    echo "$sha  $file" | sha256sum -c --quiet -
    dir="$srcroot/$name"
    patches=("$root/patches/$name"/*.patch)
    [[ -e "${patches[0]}" ]] || patches=()
    want="$sha $(cat "${patches[@]}" /dev/null | sha256sum | cut -d' ' -f1)"
    if [[ "$(cat "$dir/.prospero-stamp" 2>/dev/null)" != "$want" ]]; then
        rm -rf "$dir"
        mkdir -p "$dir"
        tar -xf "$file" -C "$dir" --strip-components=1
        for p in "${patches[@]}"; do
            echo "Applying $(basename "$p") to $name"
            patch -d "$dir" -p1 --forward --quiet < "$p"
        done
        echo "$want" > "$dir/.prospero-stamp"
        rm -f "$prefix/.stamp-$name"
    fi
    src[$name]=$dir
    version[$name]=$ver
    order+=("$name")
done < "$root/deps.lock"

if [[ $sources_only -eq 1 ]]; then
    for name in "$@"; do
        [[ -n "${src[$name]:-}" ]] || { echo "deps.sh --sources: $name is not in deps.lock" >&2; exit 2; }
    done
    exit 0
fi
selected=("$@")
[[ ${#selected[@]} -gt 0 ]] || selected=("${order[@]}")
for name in "${selected[@]}"; do
    script="$root/scripts/deps/$name.sh"
    [[ -f "$script" ]] || continue
    stamp="$prefix/.stamp-$name"
    stamp_want="${version[$name]} $(sha256sum "$script" | cut -d' ' -f1)"
    [[ "$(cat "$stamp" 2>/dev/null)" == "$stamp_want" ]] && continue
    echo "Building $name ${version[$name]}"
    for dep in "${order[@]}"; do
        var="src_${dep//-/_}"
        export "$var=${src[$dep]}"
    done
    bash "$script"
    echo "$stamp_want" > "$stamp"
done
ls -la "$prefix/lib"/*.a
