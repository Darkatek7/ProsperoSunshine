// SPDX-License-Identifier: GPL-3.0-only
// P0.4: drive Sunshine's unmodified cbs.cpp (compiled into this binary) against FFmpeg's coded
// bitstream reader/writer. It takes the first access unit of an H.264 capture, prints what the SPS
// says, lets Sunshine rewrite the VUI and bitstream-restriction fields the way its video pipeline
// does (cbs::make_sps_h264, then the old-bytes -> new-bytes replacement), re-parses the result
// and checks it. Input is /data/prosperosunshine-probe/capture.h264 (or argv[1]); without it a
// small stream is assembled in code.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/cbs.h>
#include <libavcodec/cbs_h264.h>
#include <libavcodec/h264_levels.h>
#include <ps5/klog.h>
}
#include "cbs.h"  // Sunshine's, resolved through -idirafter so FFmpeg's config.h wins

#define LOG(...) klog_printf("ProsperoSunshine: cbs " __VA_ARGS__)

namespace {

using bytes = std::vector<std::uint8_t>;

// ---- Annex B helpers ---------------------------------------------------------------------------

struct nal_span {
    std::size_t start;  // first byte of the start code
    std::size_t body;   // first byte after the start code (the NAL header)
    std::size_t end;    // one past the last byte before the next start code
};

std::vector<nal_span> split_nals(const bytes &s) {
    std::vector<nal_span> out;
    for (std::size_t i = 0; i + 3 <= s.size();) {
        if (s[i] == 0 && s[i + 1] == 0 && s[i + 2] == 1) {
            const std::size_t start = (i > 0 && s[i - 1] == 0) ? i - 1 : i;
            if (!out.empty()) out.back().end = start;
            out.push_back({start, i + 3, s.size()});
            i += 3;
        } else {
            ++i;
        }
    }
    return out;
}

int nal_type(const bytes &s, const nal_span &n) { return n.body < s.size() ? s[n.body] & 0x1f : -1; }

// Everything from the stream start through the last slice of the first IDR picture.
bytes first_access_unit(const bytes &s) {
    bool seen_idr = false;
    std::size_t end = s.size();
    for (const auto &n : split_nals(s)) {
        const int t = nal_type(s, n);
        if (seen_idr && t != 5) {
            end = n.start;
            break;
        }
        if (t == 5) seen_idr = true;
    }
    return bytes(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(end));
}

// ---- Synthetic stream --------------------------------------------------------------------------

// Bit writer with the Exp-Golomb codes H.264 headers use.
struct bit_writer {
    bytes data;
    int used = 8;  // bits used in the last byte; 8 means "start a new byte"
    void bit(unsigned b) {
        if (used == 8) {
            data.push_back(0);
            used = 0;
        }
        data.back() = static_cast<std::uint8_t>(data.back() | (b & 1u) << (7 - used++));
    }
    void u(int n, unsigned v) {
        for (int i = n - 1; i >= 0; --i) bit((v >> i) & 1u);
    }
    void ue(unsigned v) {
        const unsigned x = v + 1;
        int len = 0;
        while ((x >> len) > 1) ++len;
        u(len, 0);
        u(len + 1, x);
    }
    void se(int v) { ue(v > 0 ? static_cast<unsigned>(2 * v - 1) : static_cast<unsigned>(-2 * v)); }
    void trailing() {
        bit(1);
        while (used != 8) bit(0);
    }
};

// Start code, NAL header and RBSP with emulation prevention bytes.
void append_nal(bytes &out, int ref_idc, int type, const bytes &rbsp) {
    out.insert(out.end(), {0, 0, 0, 1, static_cast<std::uint8_t>(ref_idc << 5 | type)});
    int zeros = 0;
    for (std::uint8_t b : rbsp) {
        if (zeros >= 2 && b <= 3) {
            out.push_back(3);
            zeros = 0;
        }
        out.push_back(b);
        zeros = b == 0 ? zeros + 1 : 0;
    }
}

// Hand-assembled stream with the parameters of a 1080p Main-profile H.264 encode without VUI,
// as x264 with `--profile main --level 4.0 --ref 1 --no-vui` would write them (POC type 2 and
// CAVLC keep the slice header short). The slice payload is filler: CBS only parses headers.
bytes synthetic_stream() {
    bit_writer sps;
    sps.u(8, 77);  // profile_idc: Main
    sps.u(8, 0);   // constraint flags and reserved bits
    sps.u(8, 40);  // level_idc 4.0
    sps.ue(0);     // seq_parameter_set_id
    sps.ue(0);     // log2_max_frame_num_minus4
    sps.ue(2);     // pic_order_cnt_type
    sps.ue(1);     // max_num_ref_frames
    sps.bit(0);    // gaps_in_frame_num_value_allowed_flag
    sps.ue(119);   // pic_width_in_mbs_minus1: 1920 pixels
    sps.ue(67);    // pic_height_in_map_units_minus1: 1088 pixels
    sps.bit(1);    // frame_mbs_only_flag
    sps.bit(1);    // direct_8x8_inference_flag
    sps.bit(1);    // frame_cropping_flag
    sps.ue(0);     // crop left
    sps.ue(0);     // crop right
    sps.ue(0);     // crop top
    sps.ue(4);     // crop bottom: 1088 -> 1080
    sps.bit(0);    // vui_parameters_present_flag
    sps.trailing();

    bit_writer pps;
    pps.ue(0);     // pic_parameter_set_id
    pps.ue(0);     // seq_parameter_set_id
    pps.bit(0);    // entropy_coding_mode_flag: CAVLC
    pps.bit(0);    // bottom_field_pic_order_in_frame_present_flag
    pps.ue(0);     // num_slice_groups_minus1
    pps.ue(0);     // num_ref_idx_l0_default_active_minus1
    pps.ue(0);     // num_ref_idx_l1_default_active_minus1
    pps.bit(0);    // weighted_pred_flag
    pps.u(2, 0);   // weighted_bipred_idc
    pps.se(0);     // pic_init_qp_minus26
    pps.se(0);     // pic_init_qs_minus26
    pps.se(0);     // chroma_qp_index_offset
    pps.bit(0);    // deblocking_filter_control_present_flag
    pps.bit(0);    // constrained_intra_pred_flag
    pps.bit(0);    // redundant_pic_cnt_present_flag
    pps.trailing();

    bit_writer slice;
    slice.ue(0);    // first_mb_in_slice
    slice.ue(7);    // slice_type: I, all slices
    slice.ue(0);    // pic_parameter_set_id
    slice.u(4, 0);  // frame_num
    slice.ue(0);    // idr_pic_id
    slice.bit(0);   // no_output_of_prior_pics_flag
    slice.bit(0);   // long_term_reference_flag
    slice.se(0);    // slice_qp_delta
    slice.u(8, 0xAA);
    slice.u(8, 0x55);
    slice.u(8, 0xAA);
    slice.trailing();

    bytes out;
    append_nal(out, 3, 7, sps.data);
    append_nal(out, 3, 8, pps.data);
    append_nal(out, 3, 5, slice.data);
    return out;
}

bool read_file(const char *path, bytes &out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    std::array<std::uint8_t, 65536> buf;
    std::size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), f)) > 0)
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n));
    fclose(f);
    return !out.empty();
}

// ---- FFmpeg glue -------------------------------------------------------------------------------

struct packet_deleter {
    void operator()(AVPacket *p) const { av_packet_free(&p); }
};
using packet_ptr = std::unique_ptr<AVPacket, packet_deleter>;
struct context_deleter {
    void operator()(AVCodecContext *c) const { avcodec_free_context(&c); }
};
using context_ptr = std::unique_ptr<AVCodecContext, context_deleter>;

packet_ptr make_packet(const bytes &data) {
    packet_ptr p {av_packet_alloc()};
    if (!p || av_new_packet(p.get(), static_cast<int>(data.size())) < 0) return nullptr;
    std::memcpy(p->data, data.data(), data.size());
    return p;
}

// What the SPS of a packet says, read the way cbs.cpp reads it (CBS context, active SPS).
struct sps_info {
    bool ok = false;
    unsigned profile = 0, level = 0, width = 0, height = 0, max_refs = 0;
    bool vui = false, full_range = false, restriction = false;
    unsigned primaries = 0, transfer = 0, matrix = 0, reorder = 99, dec_buffering = 99;
};

sps_info parse_sps(const AVPacket *packet) {
    sps_info info;
    CodedBitstreamContext *ctx = nullptr;
    if (ff_cbs_init(&ctx, AV_CODEC_ID_H264, nullptr) < 0) return info;
    CodedBitstreamFragment frag {};
    if (ff_cbs_read_packet(ctx, &frag, packet) >= 0) {
        const H264RawSPS *sps = static_cast<CodedBitstreamH264Context *>(ctx->priv_data)->active_sps;
        if (sps) {
            const unsigned crop_x = sps->chroma_format_idc == 3 ? 1 : 2;
            const unsigned crop_y = (sps->chroma_format_idc == 1 ? 2 : 1) * (2 - sps->frame_mbs_only_flag);
            info.ok = true;
            info.profile = sps->profile_idc;
            info.level = sps->level_idc;
            info.max_refs = sps->max_num_ref_frames;
            info.width = (sps->pic_width_in_mbs_minus1 + 1) * 16;
            info.height = (sps->pic_height_in_map_units_minus1 + 1) * 16 * (2 - sps->frame_mbs_only_flag);
            if (sps->frame_cropping_flag) {
                info.width -= crop_x * (sps->frame_crop_left_offset + sps->frame_crop_right_offset);
                info.height -= crop_y * (sps->frame_crop_top_offset + sps->frame_crop_bottom_offset);
            }
            info.vui = sps->vui_parameters_present_flag;
            if (info.vui) {
                info.full_range = sps->vui.video_full_range_flag;
                info.primaries = sps->vui.colour_primaries;
                info.transfer = sps->vui.transfer_characteristics;
                info.matrix = sps->vui.matrix_coefficients;
                info.restriction = sps->vui.bitstream_restriction_flag;
                info.reorder = sps->vui.max_num_reorder_frames;
                info.dec_buffering = sps->vui.max_dec_frame_buffering;
            }
        }
    }
    ff_cbs_fragment_free(&frag);
    ff_cbs_close(&ctx);
    return info;
}

void print_sps(const char *label, const sps_info &s) {
    LOG("%s profile=%u level=%u size=%ux%u refs=%u vui=%d\n", label, s.profile, s.level, s.width, s.height, s.max_refs, s.vui);
    if (s.vui)
        LOG("%s vui range=%s primaries=%u transfer=%u matrix=%u restriction=%d reorder=%u dec_buffering=%u\n", label,
            s.full_range ? "full" : "limited", s.primaries, s.transfer, s.matrix, s.restriction, s.reorder, s.dec_buffering);
}

}  // namespace

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/data/prosperosunshine-probe/capture.h264";
    bytes stream;
    const bool from_file = read_file(path, stream);
    if (!from_file) stream = synthetic_stream();
    LOG("input=%s bytes=%zu\n", from_file ? path : "synthetic", stream.size());

    const bytes au = first_access_unit(stream);
    const auto nals = split_nals(au);
    const auto sps_nal = std::find_if(nals.begin(), nals.end(), [&](const nal_span &n) { return nal_type(au, n) == 7; });
    if (sps_nal == nals.end()) {
        LOG("result=FAIL no SPS in the first access unit\n");
        return 1;
    }
    LOG("access_unit bytes=%zu nals=%zu\n", au.size(), nals.size());

    // The codec context cbs.cpp reads: reference count and colour description from the encoder setup.
    context_ptr avctx {avcodec_alloc_context3(nullptr)};
    packet_ptr packet = make_packet(au);
    if (!avctx || !packet) {
        LOG("result=FAIL allocation\n");
        return 1;
    }
    avctx->refs = 1;
    avctx->color_range = AVCOL_RANGE_MPEG;
    avctx->color_primaries = AVCOL_PRI_BT709;
    avctx->color_trc = AVCOL_TRC_BT709;
    avctx->colorspace = AVCOL_SPC_BT709;

    const sps_info before = parse_sps(packet.get());
    if (!before.ok) {
        LOG("result=FAIL SPS not parsed\n");
        return 1;
    }
    print_sps("before", before);
    const H264LevelDescriptor *guess =
        ff_h264_guess_level(static_cast<int>(before.profile), 0, 0, static_cast<int>(before.width), static_cast<int>(before.height), 0);
    LOG("before level_guess=%d (stream says %u)\n", guess ? guess->level_idc : -1, before.level);
    const bool had_vui = cbs::validate_sps(packet.get(), AV_CODEC_ID_H264);

    // Sunshine's rewrite, then its replacement: find the re-serialised original SPS in the
    // packet and substitute the new one.
    const cbs::h264_t rewritten = cbs::make_sps_h264(avctx.get(), packet.get());
    const std::size_t old_size = rewritten.sps.old.size(), new_size = rewritten.sps._new.size();
    LOG("rewrite old_bytes=%zu new_bytes=%zu\n", old_size, new_size);
    if (!old_size || !new_size) {
        LOG("result=FAIL empty rewrite\n");
        return 1;
    }
    const std::uint8_t *old_begin = rewritten.sps.old.begin();
    const auto hit = std::search(au.begin(), au.end(), old_begin, old_begin + old_size);
    const bool exact = hit != au.end();
    LOG("sps_roundtrip=%s\n", exact ? "exact" : "differs (Sunshine's old-bytes search would miss it)");

    // Without an exact match the SPS NAL found by start code is replaced instead, so the rest of
    // the checks still say whether the rewritten SPS itself is sound.
    const auto replace_at = exact ? hit : au.begin() + static_cast<std::ptrdiff_t>(sps_nal->start);
    const auto replace_end = exact ? hit + static_cast<std::ptrdiff_t>(old_size) : au.begin() + static_cast<std::ptrdiff_t>(sps_nal->end);
    bytes patched(au.begin(), replace_at);
    patched.insert(patched.end(), rewritten.sps._new.begin(), rewritten.sps._new.begin() + new_size);
    patched.insert(patched.end(), replace_end, au.end());

    packet_ptr patched_packet = make_packet(patched);
    if (!patched_packet) {
        LOG("result=FAIL allocation\n");
        return 1;
    }
    const sps_info after = parse_sps(patched_packet.get());
    const bool validates = cbs::validate_sps(patched_packet.get(), AV_CODEC_ID_H264);
    if (!after.ok) {
        LOG("result=FAIL rewritten SPS not parsed\n");
        return 1;
    }
    print_sps("after", after);

    int failures = 0;
    auto check = [&](const char *name, bool ok) {
        if (!ok) {
            ++failures;
            LOG("check %s FAILED\n", name);
        }
    };
    check("geometry", after.width == before.width && after.height == before.height);
    check("profile_level", after.profile == before.profile && after.level == before.level);
    check("validate_sps", validates);
    check("vui_present", after.vui);
    check("colour", after.primaries == AVCOL_PRI_BT709 && after.transfer == AVCOL_TRC_BT709 && after.matrix == AVCOL_SPC_BT709);
    check("range", after.full_range == (avctx->color_range == AVCOL_RANGE_JPEG));
    check("restriction", after.restriction && after.reorder == 0 && after.dec_buffering == after.max_refs);
    check("refs", after.max_refs == static_cast<unsigned>(avctx->refs));
    LOG("had_vui=%d validate_after=%d size_before=%zu size_after=%zu\n", had_vui, validates, au.size(), patched.size());
    LOG("result=%s source=%s sps=%ux%u profile=%u level=%u\n", failures ? "FAIL" : "PASS", from_file ? "capture" : "synthetic",
        after.width, after.height, after.profile, after.level);
    return failures ? 1 : 0;
}
