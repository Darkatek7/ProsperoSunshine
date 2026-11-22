// SPDX-License-Identifier: GPL-3.0-only
// What the console's video out mode (sceAvcap2GetVideoOutMode; the bits in docs/re/hdr.md) says about the capture sizes the
// output takes. Portable C, for capture.c and video_ps5.cpp; tested in tests/capture_open_test.c.
#ifndef PROSPERO_CAPTURE_OUTPUT_MODE_H
#define PROSPERO_CAPTURE_OUTPUT_MODE_H

#include <stdint.h>

#define PS_OUT_4K_SCAN_IN 0x8u  // output class 6 at 60 Hz (on a PS5 Pro also at 120 Hz and class 7)
#define PS_OUT_WQHD 0x20u       // output class 5 or 6 at 60 Hz (on a PS5 Pro as 4K_SCAN_IN)
#define PS_OUT_120HZ 0x40u

// 0 when the mode says the output can't hold a capture of this resolution (5: 2560x1440 needs WQHD, 6: 3840x2160 needs
// 4K_SCAN_IN); Remote Play turns those modes away by the same bits. Seen on 13.00: OpenVideo refused 3840x2160 at 0x140 and
// 0x144 (4K at 120 Hz, a game or the Mario 64 demo in front), and on 10.20 a larger mode at 0x44 (issue 39). A set bit
// proves nothing (0x13c at a 1080p output on a 10.20 Pro), and no mode yet (0) says nothing: both give 1.
static inline int ps_output_holds(uint32_t output, uint32_t resolution)
{
    if (!output) return 1;
    if (resolution == 6) return (output & PS_OUT_4K_SCAN_IN) != 0;
    if (resolution == 5) return (output & PS_OUT_WQHD) != 0;
    return 1;
}

// The output runs at 120 Hz, as a game sets it while the console's 120 Hz output is on: then a PS5 takes no 1440p or 4K.
static inline int ps_output_at_120hz(uint32_t output) { return (output & PS_OUT_120HZ) != 0; }

// Whether a reopen of a running stream whose size the output refuses may go on at 1080p, for streams that follow the output's
// size (device_follow_size.hpp): only from 1440p or 4K. A smaller size at 1080p would outgrow what the client set its decoder
// up for, and at 1080p there is nothing to fall back to.
static inline int ps_reopen_may_follow(int follow, uint32_t resolution) { return follow && (resolution == 5 || resolution == 6); }

#endif
