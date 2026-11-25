// SPDX-License-Identifier: GPL-3.0-only
#include "video/hevc_refs.hpp"
#include "hevc_refs_fixtures.hpp"
#include <cassert>
#include <iostream>
#include <span>
#include <vector>
using namespace prospero;

namespace {
template <std::size_t N>
std::span<const uint8_t> bytes(const uint8_t (&array)[N]) {
    return {array, N};
}
std::vector<int> range(int from, int to) {
    std::vector<int> out;
    for (int i = from; i >= to; --i) out.push_back(i);
    return out;
}
}  // namespace

int main() {
    // The console's stream: 4 bits of picture order count, so at most 7 pictures may be missing in a row.
    const auto layout = hevc::layout(bytes(fixtures::console_sets));
    assert(layout);
    assert(layout->poc_bits == 4 && layout->extra_bits == 0 && !layout->output_flag && !layout->separate_colour_planes);
    assert(layout->most_missing() == 7);

    // The first frame is an IDR picture: no references.
    auto refs = hevc::refs(*layout, bytes(fixtures::console_frame_1));
    assert(refs && refs->keyframe && refs->used.empty() && refs->kept.empty());

    // The frames behind it use the picture before them and keep what there is.
    refs = hevc::refs(*layout, bytes(fixtures::console_frame_2));
    assert(refs && !refs->keyframe && refs->order == 1 && refs->used == std::vector<int>{-1} && refs->kept.empty());
    refs = hevc::refs(*layout, bytes(fixtures::console_frame_3));
    assert(refs && refs->order == 2 && refs->used == std::vector<int>{-1} && refs->kept == std::vector<int>{-2});
    refs = hevc::refs(*layout, bytes(fixtures::console_frame_100));
    assert(refs && refs->order == 3 && refs->used == std::vector<int>{-1} && refs->kept == range(-2, -9));

    // After SetInvalidFrame the next frame uses the newest picture before the loss and still names the lost ones:
    // frame 100 lost, 101 is decoded from 99; 198 to 200 lost, 201 from 197; 293 to 300 lost, 301 from 292.
    refs = hevc::refs(*layout, bytes(fixtures::console_frame_101));
    assert(refs && refs->order == 4 && refs->used == std::vector<int>{-2});
    assert((refs->kept == std::vector<int>{-1, -3, -4, -5, -6, -7, -8, -9}));
    refs = hevc::refs(*layout, bytes(fixtures::console_frame_201));
    assert(refs && refs->used == std::vector<int>{-4});
    assert((refs->kept == std::vector<int>{-1, -2, -3, -5, -6, -7, -8, -9}));
    refs = hevc::refs(*layout, bytes(fixtures::console_frame_301));
    assert(refs && refs->used == std::vector<int>{-9} && refs->kept == range(-1, -8));
    // The frame after that is an ordinary one again: from the picture before it.
    refs = hevc::refs(*layout, bytes(fixtures::console_frame_102));
    assert(refs && refs->order == 5 && refs->used == std::vector<int>{-1} && refs->kept == range(-2, -9));

    // A frame with the parameter sets in front, as the host sends a keyframe: they are passed over.
    std::vector<uint8_t> with_sets(std::begin(fixtures::console_sets), std::end(fixtures::console_sets));
    with_sets.insert(with_sets.end(), std::begin(fixtures::console_frame_101), std::end(fixtures::console_frame_101));
    refs = hevc::refs(*layout, with_sets);
    assert(refs && refs->used == std::vector<int>{-2});
    assert(hevc::layout(with_sets));

    // Another encoder's stream of the same kind: x265 with 8 bits of picture order count and two reference frames.
    const auto x265 = hevc::layout(bytes(fixtures::x265_sets));
    assert(x265 && x265->poc_bits == 8 && x265->most_missing() == 127);
    refs = hevc::refs(*x265, bytes(fixtures::x265_frame_50));
    assert(refs && !refs->keyframe && refs->order == 49 && (refs->used == std::vector<int>{-1, -2}) && refs->kept.empty());

    // A layout this doesn't read gives nothing: several sub-layers move everything behind the SPS's first byte.
    std::vector<uint8_t> layered(std::begin(fixtures::console_sets), std::end(fixtures::console_sets));
    assert(layered[33] == 0x01 && layered[34] == 0x01);  // the SPS's NAL header ends, its first byte: one layer, nesting
    layered[34] = 0x03;
    assert(!hevc::layout(layered));
    // Nor do parameter sets that are missing or cut short, or bytes that are no stream.
    assert(!hevc::layout({}));
    for (std::size_t cut = 0; cut + 1 < sizeof fixtures::console_sets; ++cut) {
        const auto part = hevc::layout(std::span(fixtures::console_sets, cut));
        // Only the last bytes of the PPS can go without the answer changing: it reads the PPS's first byte and a bit.
        assert(!part || cut >= sizeof fixtures::console_sets - 6);
    }
    const uint8_t noise[] = {0xff, 0x00, 0x00, 0x01, 0x42, 0x01, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x44, 0x01, 0xff};
    assert(!hevc::layout(bytes(noise)));

    // A slice header that is cut short, a picture without a slice, and a later slice of a picture give nothing.
    for (std::size_t cut = 0; cut < 9; ++cut) assert(!hevc::refs(*layout, std::span(fixtures::console_frame_101, cut)));
    assert(!hevc::refs(*layout, bytes(fixtures::console_sets)));
    std::vector<uint8_t> later(std::begin(fixtures::console_frame_101), std::end(fixtures::console_frame_101));
    later[6] &= 0x7f;  // first_slice_segment_in_pic_flag off
    assert(!hevc::refs(*layout, later));
    // Emulation prevention bytes inside the header are taken out before it is read: with one put behind two zero bytes
    // the header reads as without it.
    const uint8_t plain[] = {0x00, 0x00, 0x01, 0x02, 0x01, 0xd0, 0x00, 0x00, 0x40};
    const uint8_t escaped[] = {0x00, 0x00, 0x01, 0x02, 0x01, 0xd0, 0x00, 0x00, 0x03, 0x40};
    const auto a = hevc::refs(*layout, bytes(plain)), b = hevc::refs(*layout, bytes(escaped));
    assert(a.has_value() == b.has_value());
    if (a) assert(a->order == b->order && a->used == b->used && a->kept == b->kept);

    std::cout << "PASS hevc_refs_test\n";
    return 0;
}
