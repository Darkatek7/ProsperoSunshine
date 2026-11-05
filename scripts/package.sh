#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Release archive from an existing build (layout: docs/THIRD_PARTY.md). Runs on the host, not in the
# SDK image: scripts/package.sh [version] -> dist/prosperosunshine-<version>.tar.gz
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
version=${1:-$(git -C "$root" describe --tags --always --dirty)}
name=prosperosunshine-$version
stage="$root/dist/$name"
expected_licenses="boost enet ffmpeg libdisplaydevice moonlight-common-c nanors nlohmann-json openssl opus
simple-web-server sunshine"

for elf in prospero_sunshine firmware_probe host_restart; do
    test -f "$root/out/$elf.elf" || { echo "out/$elf.elf is missing, build first" >&2; exit 1; }
done
for licence in $expected_licenses; do
    test -f "$root/out/deps/licenses/$licence.txt" || { echo "licence text $licence.txt is missing" >&2; exit 1; }
done

rm -rf "$stage" "$root/dist/$name.tar.gz"
mkdir -p "$stage/licenses"
cp "$root/out/prospero_sunshine.elf" "$root/out/firmware_probe.elf" "$root/out/host_restart.elf" "$root/CHANGELOG.md" "$root/LICENSE" \
    "$root/licenses/THIRD_PARTY.md" "$stage/"
cp "$root/LICENSE" "$stage/licenses/prosperosunshine.txt"
for licence in $expected_licenses; do cp "$root/out/deps/licenses/$licence.txt" "$stage/licenses/"; done
cp -r "$root/licenses/ps5-payload-sdk-runtime" "$stage/licenses/"

(cd "$stage" && find . -type f ! -name SHA256SUMS | LC_ALL=C sort | xargs sha256sum > SHA256SUMS)
tar -czf "$root/dist/$name.tar.gz" -C "$root/dist" "$name"
echo "dist/$name.tar.gz: $(find "$stage" -type f | wc -l) files"
