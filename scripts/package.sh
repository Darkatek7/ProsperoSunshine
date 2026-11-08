#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Release archive from an existing build (layout: docs/THIRD_PARTY.md). Runs on the host, not in the
# SDK image: scripts/package.sh [version] -> dist/prosperosunshine-<version>.tar.gz. With PROSPERO_DEBUG=1 it packs the
# debug flavor from out/debug (scripts/build.sh) as dist/prosperosunshine-<version>-debug.tar.gz; its line info
# (prospero_sunshine.debug.elf) and link map stay out of the archive.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
version=${1:-$(git -C "$root" describe --tags --always --dirty)}
name=prosperosunshine-$version
out="$root/out"
if [[ "${PROSPERO_DEBUG:-0}" == 1 ]]; then
    name=$name-debug
    out="$root/out/debug"
fi
stage="$root/dist/$name"
expected_licenses="boost enet ffmpeg libdisplaydevice miniupnpc moonlight-common-c nanors nlohmann-json openssl opus
simple-web-server sunshine"

for elf in prospero_sunshine firmware_probe host_restart; do
    test -f "$out/$elf.elf" || { echo "$out/$elf.elf is missing, build first" >&2; exit 1; }
done
for licence in $expected_licenses; do
    test -f "$root/out/deps/licenses/$licence.txt" || { echo "licence text $licence.txt is missing" >&2; exit 1; }
done

rm -rf "$stage" "$root/dist/$name.tar.gz"
mkdir -p "$stage/licenses"
cp "$out/prospero_sunshine.elf" "$out/firmware_probe.elf" "$out/host_restart.elf" "$root/CHANGELOG.md" "$root/LICENSE" \
    "$root/licenses/THIRD_PARTY.md" "$stage/"
cp "$root/LICENSE" "$stage/licenses/prosperosunshine.txt"
for licence in $expected_licenses; do cp "$root/out/deps/licenses/$licence.txt" "$stage/licenses/"; done
cp -r "$root/licenses/ps5-payload-sdk-runtime" "$stage/licenses/"

(cd "$stage" && find . -type f ! -name SHA256SUMS | LC_ALL=C sort | xargs sha256sum > SHA256SUMS)
tar -czf "$root/dist/$name.tar.gz" -C "$root/dist" "$name"
echo "dist/$name.tar.gz: $(find "$stage" -type f | wc -l) files"
