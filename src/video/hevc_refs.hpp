// SPDX-License-Identifier: GPL-3.0-only
// Which earlier pictures an HEVC picture is decoded from, read from its first slice header. The host needs it to get
// over a loss without a keyframe (docs/re/encoder-options.md, section 5): after sceAvcap2SetInvalidFrame the encoder
// goes on from the newest picture before the loss, and nothing but the slice header says which frame is the first to
// do so. A frame whose references all lie before the loss is one the client can decode.
//
// It reads as much of the stream's layout as that takes and gives nothing for a layout it doesn't know (reference
// picture sets in the sequence parameter set, long-term pictures, scaling lists, several sub-layers), so a caller falls
// back to a keyframe there. Measured on 13.00: no reference picture sets in the SPS, 4 bits of picture order count, one
// slice header layout. Portable (tests/hevc_refs_test.cpp, tests/fuzz/hevc_refs_fuzz.cpp).
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace prospero::hevc {

namespace detail {
// The payload of a NAL unit without its emulation prevention bytes, at most `limit` bytes of it.
inline std::vector<uint8_t> rbsp(std::span<const uint8_t> nal, std::size_t limit) {
    std::vector<uint8_t> out;
    out.reserve(nal.size() < limit ? nal.size() : limit);
    int zeros = 0;
    for (const uint8_t byte : nal) {
        if (out.size() >= limit) break;
        if (zeros >= 2 && byte == 3) {
            zeros = 0;
            continue;
        }
        zeros = byte == 0 ? zeros + 1 : 0;
        out.push_back(byte);
    }
    return out;
}

class Bits {
public:
    explicit Bits(const std::vector<uint8_t> &bytes) : bytes_(bytes) {}
    // Reading past the end gives zeros and makes ok() false.
    uint32_t bits(int count) {
        uint32_t value = 0;
        for (int i = 0; i < count; ++i) {
            const std::size_t byte = at_ >> 3;
            uint32_t bit = 0;
            if (byte < bytes_.size())
                bit = (bytes_[byte] >> (7 - (at_ & 7))) & 1u;
            else
                ok_ = false;
            value = (value << 1) | bit;
            ++at_;
        }
        return value;
    }
    bool flag() { return bits(1) != 0; }
    void skip(std::size_t count) {
        at_ += count;
        if (at_ > bytes_.size() * 8) ok_ = false;
    }
    // Exp-Golomb, unsigned. More than 31 leading zeros is no value of a header this reads.
    uint32_t ue() {
        int zeros = 0;
        while (ok_ && !flag()) {
            if (++zeros > 31) {
                ok_ = false;
                return 0;
            }
        }
        if (!ok_) return 0;
        return zeros ? ((1u << zeros) - 1u) + bits(zeros) : 0;
    }
    bool ok() const { return ok_; }

private:
    const std::vector<uint8_t> &bytes_;
    std::size_t at_ = 0;
    bool ok_ = true;
};

// Calls `each(type, payload)` for every NAL unit of an Annex B buffer, the payload without the two header bytes;
// stops when `each` returns false.
template <class Each>
void for_each_nal(std::span<const uint8_t> bytes, Each &&each) {
    std::size_t start = bytes.size();  // of the current NAL unit's header
    const auto emit = [&](std::size_t end) {
        while (end > start && bytes[end - 1] == 0) --end;  // trailing zeros belong to the next start code
        if (start + 2 > end) return true;
        const uint8_t type = (bytes[start] >> 1) & 0x3f;
        return each(type, bytes.subspan(start + 2, end - start - 2));
    };
    for (std::size_t i = 0; i + 2 < bytes.size(); ++i) {
        if (bytes[i] != 0 || bytes[i + 1] != 0 || bytes[i + 2] != 1) continue;
        if (start < bytes.size() && !emit(i)) return;
        start = i + 3;
        i += 2;
    }
    if (start < bytes.size()) emit(bytes.size());
}
}  // namespace detail

// What a slice header's layout depends on, from the sequence and the picture parameter set.
struct Layout {
    int poc_bits = 0;  // of the picture order count in a slice header
    bool separate_colour_planes = false;
    bool output_flag = false;
    int extra_bits = 0;

    // How many pictures in a row may be missing before a decoder puts the next one in the wrong place: it tells the
    // order of two pictures from these bits alone while they are less than half a turn apart.
    int most_missing() const { return poc_bits >= 4 && poc_bits <= 16 ? (1 << (poc_bits - 1)) - 1 : 0; }
};

// From an Annex B buffer holding the parameter sets (what the capture hands out before the first frame, or a keyframe
// with them in front). Nothing for a layout this doesn't read.
inline std::optional<Layout> layout(std::span<const uint8_t> headers) {
    Layout result;
    bool sps = false, pps = false, known = true;
    detail::for_each_nal(headers, [&](uint8_t type, std::span<const uint8_t> payload) {
        if (type == 33 && !sps) {
            const auto bytes = detail::rbsp(payload, 512);
            detail::Bits in(bytes);
            in.skip(4);  // sps_video_parameter_set_id
            const uint32_t sub_layers = in.bits(3);
            in.skip(1);  // sps_temporal_id_nesting_flag
            if (sub_layers != 0) {
                known = false;  // the profile and the ordering info then come per sub-layer
                return false;
            }
            in.skip(96);  // profile_tier_level of one layer
            in.ue();  // sps_seq_parameter_set_id
            const uint32_t chroma = in.ue();
            if (chroma == 3) result.separate_colour_planes = in.flag();
            in.ue();  // width
            in.ue();  // height
            if (in.flag()) {  // conformance window
                in.ue();
                in.ue();
                in.ue();
                in.ue();
            }
            in.ue();  // bit_depth_luma_minus8
            in.ue();  // bit_depth_chroma_minus8
            const uint32_t poc = in.ue();
            in.flag();  // sps_sub_layer_ordering_info_present_flag: one layer either way
            in.ue();  // sps_max_dec_pic_buffering_minus1
            in.ue();  // sps_max_num_reorder_pics
            in.ue();  // sps_max_latency_increase_plus1
            in.ue();  // log2_min_luma_coding_block_size_minus3
            in.ue();  // log2_diff_max_min_luma_coding_block_size
            in.ue();  // log2_min_luma_transform_block_size_minus2
            in.ue();  // log2_diff_max_min_luma_transform_block_size
            in.ue();  // max_transform_hierarchy_depth_inter
            in.ue();  // max_transform_hierarchy_depth_intra
            const bool scaling_lists = in.flag();
            if (scaling_lists && in.flag()) {
                known = false;  // sps_scaling_list_data
                return false;
            }
            in.flag();  // amp_enabled_flag
            in.flag();  // sample_adaptive_offset_enabled_flag
            if (in.flag()) {  // pcm
                in.skip(8);
                in.ue();
                in.ue();
                in.flag();
            }
            const uint32_t sets = in.ue();  // num_short_term_ref_pic_sets
            const bool long_term = in.flag();
            if (!in.ok() || poc > 12 || sets != 0 || long_term) {
                known = false;  // a slice may then name a set of the SPS, or long-term pictures
                return false;
            }
            result.poc_bits = static_cast<int>(poc) + 4;
            sps = true;
        } else if (type == 34 && !pps) {
            const auto bytes = detail::rbsp(payload, 64);
            detail::Bits in(bytes);
            in.ue();  // pps_pic_parameter_set_id
            in.ue();  // pps_seq_parameter_set_id
            in.flag();  // dependent_slice_segments_enabled_flag: only a picture's first slice is read
            result.output_flag = in.flag();
            result.extra_bits = static_cast<int>(in.bits(3));
            if (!in.ok()) {
                known = false;
                return false;
            }
            pps = true;
        }
        return true;
    });
    if (!known || !sps || !pps) return std::nullopt;
    return result;
}

// A picture's references, as distances in pictures: -1 is the picture before it.
struct Refs {
    bool keyframe = false;  // an IDR picture: it has no references
    int order = 0;  // its picture order count's low bits
    std::vector<int> used;  // the pictures it is decoded from
    std::vector<int> kept;  // the pictures it only tells the decoder to keep
};

// From an Annex B buffer holding one picture (its first slice). Nothing when the header isn't one this reads.
inline std::optional<Refs> refs(const Layout &layout, std::span<const uint8_t> picture) {
    std::optional<Refs> result;
    detail::for_each_nal(picture, [&](uint8_t type, std::span<const uint8_t> payload) {
        if (type >= 32) return true;  // parameter sets, SEI
        if (type > 21 || (type > 9 && type < 16)) return false;  // reserved slice types
        const auto bytes = detail::rbsp(payload, 96);
        detail::Bits in(bytes);
        const bool first = in.flag();  // first_slice_segment_in_pic_flag
        if (!first) return false;
        const bool irap = type >= 16;
        if (irap) in.flag();  // no_output_of_prior_pics_flag
        in.ue();  // slice_pic_parameter_set_id
        in.skip(static_cast<std::size_t>(layout.extra_bits));
        const uint32_t slice_type = in.ue();
        if (layout.output_flag) in.flag();
        if (layout.separate_colour_planes) in.skip(2);
        if (!in.ok() || slice_type > 2) return false;
        Refs found;
        if (type == 19 || type == 20) {
            found.keyframe = true;
            result = std::move(found);
            return false;
        }
        found.order = static_cast<int>(in.bits(layout.poc_bits));
        if (in.flag()) return false;  // short_term_ref_pic_set_sps_flag: the SPS has no sets (layout() saw to it)
        // st_ref_pic_set(0): the first set of all, so not predicted from another.
        const uint32_t negative = in.ue(), positive = in.ue();
        if (!in.ok() || negative > 16 || positive != 0) return false;  // pictures later in order: no low-delay stream
        int distance = 0;
        for (uint32_t i = 0; i < negative; ++i) {
            const uint32_t step = in.ue();
            const bool used = in.flag();
            if (!in.ok() || step > 1024) return false;
            distance -= static_cast<int>(step) + 1;
            (used ? found.used : found.kept).push_back(distance);
        }
        result = std::move(found);
        return false;
    });
    return result;
}

}  // namespace prospero::hevc
