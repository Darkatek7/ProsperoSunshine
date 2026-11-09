// SPDX-License-Identifier: GPL-3.0-only
// The QP the encoder uses on firmware 10.x and 9.60. Its capture record has no rate fields (capture.c), and the encoder holds one QP
// for IDR frames (cfg+0xc4) and one for the others (cfg+0xc8). Left alone it sent 0.3 to 0.4 Mbit/s in a moving game,
// whatever the client asked for, which shows as blocks. sceAvcap2SetVideoBitrate doesn't help there: the next read fails
// with 0x80c78000. So the QP follows the client's bitrate instead.
//
// Measured on 10.20 in GTA III, driving fast, HEVC 10 bit at 1080p60 (STATUS.md), average and busiest second:
// 30/34 about 2 and 3.1 Mbit/s, 27/31 2.4 and 3.7, 24/28 3 and 5.1, 21/25 5.3 and 10. Each step starts at about twice
// its busiest second, as busier games will need more; the encoder still doesn't hold the client's bitrate.
// HEVC 8 bit stops at 24/28 (issue 50): on 10.20 with a moving picture, 21/25 lost the capture in a loop (ReadVideo2
// 0x8195000d 20 to 50 ms after each open) at 1080p, 1440p and 4K; 24/28 held at 1440p and 4K (89 to 140 Mbit/s with
// animated noise) and at 1080p with a moving clip (39 to 58 Mbit/s). H.264 and HEVC 10 bit held 21/25. 13.00, which takes
// rate fields instead, didn't lose it at 63 Mbit/s. Noise at 1080p lost it at 24/28 too and held from 27/31: for that,
// capture.c makes a stream's reopens a step coarser after the read fails that way twice in a row (ps_qp_coarser).
// Portable, tested in tests/encoder_qp_test.cpp; configure() in capture.c applies it.
#ifndef PROSPERO_ENCODER_QP_H
#define PROSPERO_ENCODER_QP_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t idr, other;
} ps_qp;

// Where the IDR's QP sits, the others' 4 bytes on, for a record that moves by `shift`. 9.60 (firmware probe, phase ratesearch): 20 at
// 0xac gave 1.9 times the bytes, at 0xb0 1.6 times, and nothing changed at 0xc4 and 0xc8.
static inline size_t ps_qp_offset(ptrdiff_t shift) { return shift == -0x20 ? 0xac : 0xc4; }

// bitrate: the client's bitrate in bit/s, the average ps_capture_set_opening_bitrate() received. hevc_8_bit: HEVC without HDR.
static inline ps_qp ps_qp_for_bitrate(uint64_t bitrate, int hevc_8_bit)
{
    const uint32_t idr = bitrate >= 20000000u && !hevc_8_bit ? 21 : bitrate >= 12000000u ? 24 : bitrate >= 8000000u ? 27 : 30;
    const ps_qp qp = {idr, idr + 4};
    return qp;
}

// `steps` steps coarser than `qp`, no coarser than 30/34.
static inline ps_qp ps_qp_coarser(ps_qp qp, uint32_t steps)
{
    if (steps > 3) steps = 3;
    const uint32_t idr = qp.idr + 3 * steps > 30 ? (qp.idr > 30 ? qp.idr : 30) : qp.idr + 3 * steps;
    const ps_qp coarser = {idr, idr + (qp.other - qp.idr)};
    return coarser;
}

#endif
