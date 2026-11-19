// SPDX-License-Identifier: GPL-3.0-only
#include "video/annex_b.hpp"
#include "video/frame_queue.hpp"
#include "video/nal_slices.h"
#include <cassert>
#include <iostream>
using namespace std::chrono_literals;
using prospero::EncodedFrame;
using prospero::FrameQueue;

int main() {
    const uint8_t idr[] = {0, 0, 0, 1, 0x65, 3};
    const uint8_t short_idr[] = {0, 0, 1, 0x65};
    const uint8_t predicted[] = {0, 0, 1, 0x41, 5};
    const uint8_t truncated[] = {0, 0, 0, 1};
    assert(prospero::has_idr(idr));
    assert(prospero::has_idr(short_idr));
    assert(!prospero::has_idr(predicted));
    assert(!prospero::has_idr(truncated));
    // HEVC: the type sits in bits 1-6 of the first header byte. 0x28 = 20 (IDR_N_LP), 0x02 = 1 (trailing
    // picture), 0x40 = 32 (VPS). An H.264 reading of these would be wrong: 0x65 as HEVC is type 50.
    const uint8_t hevc_idr[] = {0, 0, 0, 1, 0x28, 1, 3};
    const uint8_t hevc_cra[] = {0, 0, 1, 0x2a, 1, 3};
    const uint8_t hevc_p[] = {0, 0, 0, 1, 0x02, 1, 3};
    const uint8_t hevc_vps[] = {0, 0, 0, 1, 0x40, 1, 3};
    assert(prospero::has_idr(hevc_idr, prospero::Codec::hevc));
    assert(prospero::has_idr(hevc_cra, prospero::Codec::hevc));
    assert(!prospero::has_idr(hevc_p, prospero::Codec::hevc));
    assert(!prospero::has_idr(idr, prospero::Codec::hevc));
    assert(prospero::has_sps(hevc_vps, prospero::Codec::hevc));
    assert(!prospero::has_sps(hevc_idr, prospero::Codec::hevc));
    // The first slice ends the search: an access unit has its parameter sets and SEI in front of the slices and is all IDR or
    // none of it, so a P frame is not scanned to its end.
    const uint8_t sps_pps_idr[] = {0, 0, 0, 1, 0x67, 1, 0, 0, 0, 1, 0x68, 2, 0, 0, 0, 1, 0x65, 3};
    const uint8_t sei_idr[] = {0, 0, 0, 1, 0x06, 1, 0, 0, 0, 1, 0x65, 3};
    const uint8_t p_then_sps[] = {0, 0, 0, 1, 0x41, 5, 0, 0, 0, 1, 0x67, 1, 0, 0, 0, 1, 0x65, 3};
    assert(prospero::has_sps(sps_pps_idr) && prospero::has_idr(sps_pps_idr));
    assert(prospero::has_idr(sei_idr) && !prospero::has_sps(sei_idr));
    assert(!prospero::has_sps(p_then_sps) && !prospero::has_idr(p_then_sps));
    // HEVC: VPS 32, SPS 33, PPS 34 and prefix SEI 39 (0x4e) before an IDR_W_RADL slice (19, 0x26).
    const uint8_t hevc_sets_idr[] = {0, 0, 0, 1, 0x40, 1, 0, 0, 0, 1, 0x42, 1, 0, 0, 0, 1, 0x44, 1,
                                     0, 0, 0, 1, 0x4e, 1, 0, 0, 0, 1, 0x26, 1, 3};
    const uint8_t hevc_p_then_idr[] = {0, 0, 0, 1, 0x02, 1, 0, 0, 0, 1, 0x26, 1, 3};
    const uint8_t hevc_idr_then_vps[] = {0, 0, 0, 1, 0x26, 1, 3, 0, 0, 0, 1, 0x40, 1, 3};
    assert(prospero::has_idr(hevc_sets_idr, prospero::Codec::hevc) && prospero::has_sps(hevc_sets_idr, prospero::Codec::hevc));
    assert(!prospero::has_idr(hevc_p_then_idr, prospero::Codec::hevc));
    // An IDR without its VPS in front: no parameter sets, so CaptureSession puts the stored ones in front of it.
    assert(prospero::has_idr(hevc_idr_then_vps, prospero::Codec::hevc));
    assert(!prospero::has_sps(hevc_idr_then_vps, prospero::Codec::hevc));
    const auto now = std::chrono::steady_clock::now();
    auto frame = [&](uint64_t i, bool key, size_t n = 4) {
        return EncodedFrame{i, now, key, std::vector<uint8_t>(n)};
    };
    FrameQueue q(2, 12, 100ms);
    assert(!q.push(frame(0, false))); // A new decoder needs a random access point.
    assert(q.push(frame(1, true)));
    assert(q.push(frame(2, false)));
    assert(!q.push(frame(3, false))); // Count overflow loses references.
    assert(!q.pop(now));
    assert(q.needs_idr());
    assert(!q.push(frame(4, false)));
    assert(q.push(frame(5, true)));
    assert(q.pop(now)->index == 5);
    assert(!q.push(frame(7, false))); // Missing frame 6 invalidates the chain.
    assert(q.push(frame(8, true, 10)));
    assert(!q.push(frame(9, false, 4))); // Byte limit, even with spare frame slots.
    assert(q.bytes() == 0 && q.needs_idr());
    assert(q.push(frame(10, true)));
    assert(!q.pop(now + 101ms)); // Never deliver a frame older than the queue budget.
    assert(q.needs_idr());
    assert(!q.push(frame(11, true, 13))); // A single oversized frame cannot grow the queue.
    assert(q.bytes() == 0);
    // Slices: SPS, PPS and SEI don't count, IDR and non-IDR slices do; HEVC counts VCL types only (VPS 32 doesn't).
    const uint8_t h264_frame[] = {0, 0, 0, 1, 0x67, 1, 0, 0, 1, 0x68, 2, 0, 0, 1, 0x06, 3, 0, 0, 1, 0x65, 4, 0, 0, 1, 0x65, 5,
                                  0, 0, 1, 0x41, 6};
    assert(ps_nal_slices(h264_frame, sizeof h264_frame, 0) == 3);
    const uint8_t hevc_frame[] = {0, 0, 1, 0x40, 1, 0, 0, 1, 0x28, 1, 0, 0, 1, 0x02, 1};
    assert(ps_nal_slices(hevc_frame, sizeof hevc_frame, 1) == 2);
    assert(ps_nal_slices(truncated, sizeof truncated, 0) == 0);
    std::cout << "PASS: Annex B, decoder startup, frame loss, slices, count/byte/age limits and IDR recovery\n";
}
