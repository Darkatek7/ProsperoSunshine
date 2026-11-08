#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck disable=SC2154  # sdk, prefix, work, src_* and the rest come from scripts/deps.sh
# Header-only; Sunshine includes it as <Simple-Web-Server/...>.
set -euo pipefail
mkdir -p "$prefix/include/Simple-Web-Server"
cp "$src_simple_web_server"/*.hpp "$prefix/include/Simple-Web-Server/"
cp "$src_simple_web_server/LICENSE" "$prefix/licenses/simple-web-server.txt"
