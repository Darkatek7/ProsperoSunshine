#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck disable=SC2154  # sdk, prefix, work, src_* and the rest come from scripts/deps.sh
# Sunshine is compiled as part of the host; only its licence ships here.
set -euo pipefail
cp "$src_sunshine/LICENSE" "$prefix/licenses/sunshine.txt"
