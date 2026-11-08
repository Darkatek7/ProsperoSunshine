#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck disable=SC2154  # sdk, prefix, work, src_* and the rest come from scripts/deps.sh
# moonlight-common-c is compiled as part of the host (RtspParser and headers); only its licence ships here.
set -euo pipefail
cp "$src_moonlight_common_c/LICENSE.txt" "$prefix/licenses/moonlight-common-c.txt"
