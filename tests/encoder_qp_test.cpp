// SPDX-License-Identifier: GPL-3.0-only
// The QP firmware 10.x encodes with, from the client's bitrate (platform/ps5/encoder_qp.h). tests/capture_open_test.c
// takes it through configure().
#include "platform/ps5/encoder_qp.h"
#include <cassert>
#include <cstdio>

int main() {
    const auto at = [](uint64_t kbit) { return ps_qp_for_bitrate(kbit * 1000, 0); };
    const auto hevc8 = [](uint64_t kbit) { return ps_qp_for_bitrate(kbit * 1000, 1); };
    assert(at(5000).idr == 30 && at(5000).other == 34);
    assert(at(10000).idr == 27 && at(10000).other == 31);
    assert(at(16364).idr == 24 && at(16364).other == 28);  // Moonlight for PC at 1920x1200, 60 fps
    assert(at(20000).idr == 21 && at(20000).other == 25);
    assert(at(46988).idr == 21);  // Moonlight for iOS
    // The edges, and a higher bitrate never gets a coarser picture.
    assert(at(7999).idr == 30 && at(8000).idr == 27);
    assert(at(11999).idr == 27 && at(12000).idr == 24);
    assert(at(19999).idr == 24 && at(20000).idr == 21);
    // HEVC 8 bit stops at 24/28 (issue 50), and is the same as the others below it.
    assert(hevc8(19999).idr == 24 && hevc8(20000).idr == 24 && hevc8(22988).other == 28 && hevc8(150000).idr == 24);
    assert(hevc8(5000).idr == 30 && hevc8(10000).idr == 27 && hevc8(12000).idr == 24);
    for (int hevc_8_bit = 0; hevc_8_bit < 2; hevc_8_bit++)
        for (uint64_t bitrate = 0; bitrate < 200000000u; bitrate += 250000u) {
            const ps_qp qp = ps_qp_for_bitrate(bitrate, hevc_8_bit), next = ps_qp_for_bitrate(bitrate + 250000u, hevc_8_bit);
            assert(next.idr <= qp.idr && next.other <= qp.other);
            assert(qp.idr >= 1 && qp.other <= 51 && qp.other > qp.idr);  // 52 and above make Start fail
            assert(qp.idr >= ps_qp_for_bitrate(bitrate, 0).idr);
        }
    // Coarser steps after failed reads (issue 50): three QP apart, never past 30/34, the gap to the others kept.
    const auto step = [](uint32_t idr, uint32_t steps) { return ps_qp_coarser(ps_qp{idr, idr + 4}, steps); };
    assert(step(24, 0).idr == 24 && step(24, 1).idr == 27 && step(24, 1).other == 31 && step(24, 2).idr == 30);
    assert(step(24, 3).idr == 30 && step(21, 3).idr == 30 && step(30, 1).idr == 30 && step(30, 1).other == 34);
    assert(step(21, 1).idr == 24 && step(21, 1).other == 28 && step(27, 9).idr == 30);
    std::puts("PASS: encoder QP on 10.x (from the client's bitrate, edges, never coarser for more, HEVC 8 bit at most 24/28, coarser steps)");
    return 0;
}
