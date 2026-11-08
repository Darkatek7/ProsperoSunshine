#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck disable=SC2154  # sdk, prefix, work, src_* and the rest come from scripts/deps.sh
# FFmpeg: static libavutil, libavcodec and libswscale with nothing enabled but the two bitstream
# filters that pull in the H.264/HEVC coded bitstream (CBS) reader and writer Sunshine's cbs.cpp
# calls, plus the generic AVCodecContext/AVPacket API video.h includes. No codec is compiled in.
# Sunshine also includes FFmpeg-internal headers (cbs_h264.h, cbs_h265.h, h264_levels.h), which
# upstream does not install; like LizardByte/build-deps it gets them (and their transitive headers)
# installed next to the public ones. The ff_cbs_* symbols are not hidden in a static archive.
set -euo pipefail
s="$src_ffmpeg"
b="$work/ffmpeg-build"
rm -rf "$b" && mkdir -p "$b" "$prefix/licenses"
cd "$b"
"$s/configure" --prefix="$prefix" --enable-cross-compile --target-os=freebsd --arch=x86_64 \
    --cc="$sdk/bin/prospero-clang" --cxx="$sdk/bin/prospero-clang++" --ar="$sdk/bin/prospero-ar" \
    --ranlib="$sdk/bin/prospero-ranlib" --nm="$sdk/bin/prospero-nm" --strip="$sdk/bin/prospero-strip" \
    --host-cc=clang-18 --pkg-config=false --extra-cflags='-O2 -w' \
    --disable-everything --disable-autodetect --disable-programs --disable-doc --disable-network \
    --disable-x86asm --disable-debug --disable-shared --enable-static --enable-pic \
    --disable-avformat --disable-avfilter --disable-avdevice --disable-swresample \
    --enable-avcodec --enable-avutil --enable-swscale --enable-bsf=h264_metadata,hevc_metadata >/dev/null
make -j"$jobs" >/dev/null
make install >/dev/null

# Headers of the internal CBS API: everything the three headers Sunshine includes pull in.
printf '#include "libavcodec/cbs_h264.h"\n#include "libavcodec/cbs_h265.h"\n#include "libavcodec/h264_levels.h"\n' \
    | "$sdk/bin/prospero-clang" -MM -x c -I"$s" -I"$b" - | sed 's/\$//' | tr ' ' '\n' \
    | grep -E '\.h$' | sort -u | while read -r h; do
        rel=${h#"$s"/}; rel=${rel#"$b"/}
        mkdir -p "$prefix/include/$(dirname "$rel")"
        cp "$h" "$prefix/include/$rel"
    done
cp "$s/COPYING.LGPLv2.1" "$prefix/licenses/ffmpeg.txt"
