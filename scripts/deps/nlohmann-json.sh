#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck disable=SC2154  # sdk, prefix, work, src_* and the rest come from scripts/deps.sh
# Header-only.
set -euo pipefail
cp -r "$src_nlohmann_json/include/nlohmann" "$prefix/include/"
cp "$src_nlohmann_json/LICENSE.MIT" "$prefix/licenses/nlohmann-json.txt"
