#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck disable=SC2154  # sdk, prefix, work, src_* and the rest come from scripts/deps.sh
# Boost: only the libraries Sunshine's kept sources include, via the CMake superproject.
# Compiled libraries come from the dependency closure (filesystem, log, thread, atomic, ...); asio, ptree,
# format, endian, bind and core are header-only. Boost.Log uses std::regex and has no IPC or syslog backend.
set -euo pipefail
b="$work/boost-build"
rm -rf "$b"
"$sdk/bin/prospero-cmake" -S "$src_boost" -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_VERBOSE_MAKEFILE=OFF \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DBOOST_ENABLE_MPI=OFF -DBOOST_ENABLE_PYTHON=OFF \
    "-DBOOST_INCLUDE_LIBRARIES=asio;property_tree;log;filesystem;format;endian;bind;core;process" \
    -DBOOST_LOG_USE_REGEX_BACKEND=std::regex -DBOOST_LOG_WITHOUT_SETTINGS_PARSERS=ON \
    -DBOOST_LOG_WITHOUT_IPC=ON -DBOOST_LOG_WITHOUT_SYSLOG=ON \
    -DBOOST_FILESYSTEM_DISABLE_STATX=ON >/dev/null
cmake --build "$b" -j"$jobs" >/dev/null
cmake --install "$b" >/dev/null
cp "$src_boost/LICENSE_1_0.txt" "$prefix/licenses/boost.txt"
