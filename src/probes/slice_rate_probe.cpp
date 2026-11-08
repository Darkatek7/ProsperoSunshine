// SPDX-License-Identifier: GPL-3.0-only
// Two open questions about the encoder record, each run on a fresh channel for 3 s:
// - Slices: on 10.x H.264 comes with one slice per macroblock row (34 at 1080p), more than the 32 the hardware decoders on
//   Windows take. Remote Play sets cfg+0x40 = 1, cfg+0x44 = slice count and cfg+0x48 = 1 for H.264 (SceRemotePlay
//   0x6f9df..0x6fa32); the host leaves them at 0. On 13.00 (1 slice per frame) this shows whether 0x44 sets the count.
// - 120 fps: cfg+0x1c is 0 for 30 and 1 for 60 fps; the validator takes 0..2 (docs/re/avcap2-exports.md). Value 2 is tried
//   for H.264 and HEVC, and 3 as a control that should be refused. Best run with a title that sets 120 Hz output.
// Each line in /data/prosperosunshine/slice_rate_probe/slice_rate_probe.log gives frames per second, kbit/s, slices per
// frame and the TSC interval between frames (median and spread). The host must be stopped: a busy capture is tried again
// after 5 s, eight times.
#include "platform/ps5/capture.h"
#include "platform/ps5/diag.h"
#include <sys/stat.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <vector>
extern "C" {
#include <ps5/kernel.h>
}

namespace {

const char *const dir = "/data/prosperosunshine/slice_rate_probe";

struct Field {
    size_t offset;
    uint32_t value;
};

struct Variant {
    const char *name;
    uint32_t codec, ten_bit, resolution;
    uint32_t timing;  // written to cfg+0x1c after the record, 0 = leave the host's (60 fps)
    Field fields[3];  // offset 0 = unused
};

// Slices per frame: H.264 NAL types 1 and 5, HEVC VCL types 0..31.
int slices_in(const uint8_t *data, int size, bool hevc) {
    int slices = 0;
    for (int i = 0; i + 3 < size; i++)
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            const int type = hevc ? (data[i + 3] >> 1) & 0x3f : data[i + 3] & 0x1f;
            if (hevc ? type < 32 : type == 1 || type == 5) ++slices;
            i += 2;
        }
    return slices;
}

uint64_t read_u64(const uint8_t *p) {
    uint64_t v;
    memcpy(&v, p, sizeof v);
    return v;
}

int run(std::vector<uint8_t> &buffer, const Variant &v) {
    const bool hevc = v.codec == 1;
    ps_capture_set_codec(v.codec);
    ps_capture_set_ten_bit(v.ten_bit);
    ps_capture_set_video_mode(v.resolution, 1);
    ps_capture_override_config(v.timing ? 0x1c : 0, v.timing);
    for (int i = 0; i < 3; i++) ps_capture_override_extra(i, v.fields[i].offset, v.fields[i].value);
    ps_capture *s = nullptr;
    const int open_rc = ps_capture_open(&s);
    ps_capture_override_config(0, 0);
    for (int i = 0; i < 3; i++) ps_capture_override_extra(i, 0, 0);
    if (open_rc) {
        ps_diag_log("ProsperoSunshine: slice_rate %s: failed %d raw=%#x\n", v.name, open_rc,
                    static_cast<unsigned>(ps_capture_last_open_rc()));
        usleep(500000);
        return open_rc;
    }
    char path[160];
    snprintf(path, sizeof path, "%s/%s.%s", dir, v.name, hevc ? "h265" : "h264");
    FILE *out = fopen(path, "wb");
    const int header_size = ps_capture_headers(s, buffer.data(), buffer.size());
    if (out && header_size > 0) fwrite(buffer.data(), 1, static_cast<size_t>(header_size), out);
    uint8_t desc[0x88];
    int frames = 0, slice_min = 1 << 30, slice_max = 0, error = 0;
    long bytes = 0;
    std::vector<uint64_t> stamps;
    const auto begin = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - begin < std::chrono::seconds(3)) {
        const int rc = hevc ? ps_capture_read2(s, buffer.data(), buffer.size(), desc)
                            : ps_capture_read(s, buffer.data(), buffer.size(), desc);
        if (rc < 0) {
            error = rc;
            break;
        }
        if (rc == 0) {
            usleep(500);
            continue;
        }
        ++frames;
        bytes += rc;
        if (out) fwrite(buffer.data(), 1, static_cast<size_t>(rc), out);
        const int n = slices_in(buffer.data(), rc, hevc);
        slice_min = std::min(slice_min, n);
        slice_max = std::max(slice_max, n);
        // The frame's TSC stamp (docs/avcap2-descriptor.md): +0x58 with ReadVideo2, +0x48 with ReadVideo.
        stamps.push_back(read_u64(desc + (hevc ? 0x58 : 0x48)));
    }
    if (out) fclose(out);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    std::vector<uint64_t> gaps;
    for (size_t i = 1; i < stamps.size(); i++)
        if (stamps[i] > stamps[i - 1]) gaps.push_back(stamps[i] - stamps[i - 1]);
    std::sort(gaps.begin(), gaps.end());
    const uint64_t median = gaps.empty() ? 0 : gaps[gaps.size() / 2];
    const uint64_t low = gaps.empty() ? 0 : gaps[gaps.size() / 10], high = gaps.empty() ? 0 : gaps[gaps.size() * 9 / 10];
    const int close_rc = ps_capture_close(s);
    ps_diag_log("ProsperoSunshine: slice_rate %s: ok frames=%d fps=%.1f kbit/s=%.0f slices=%d..%d tsc_gap median=%llu "
                "p10=%llu p90=%llu err=%#x close=%d file=%s\n",
                v.name, frames, frames / seconds, bytes * 8 / seconds / 1000, frames ? slice_min : 0, slice_max,
                static_cast<unsigned long long>(median), static_cast<unsigned long long>(low),
                static_cast<unsigned long long>(high), static_cast<unsigned>(error), close_rc, path);
    usleep(500000);
    return 0;
}

}  // namespace

int main() {
    mkdir(dir, 0777);
    char header[96];
    snprintf(header, sizeof header, "ProsperoSunshine: slice_rate_probe firmware=%#x", kernel_get_fw_version());
    ps_diag_open("/data/prosperosunshine/slice_rate_probe/slice_rate_probe.log", header);
    ps_capture_set_opening_bitrate(16000000, 17600000);
    std::vector<uint8_t> buffer(16 * 1024 * 1024);
    static const Variant variants[] = {
        {"h264_1080_base", 0, 0, 4, 0, {}},
        {"h264_1080_s1", 0, 0, 4, 0, {{0x40, 1}, {0x44, 1}, {0x48, 1}}},
        {"h264_1080_s4", 0, 0, 4, 0, {{0x40, 1}, {0x44, 4}, {0x48, 1}}},
        {"h264_1080_s16", 0, 0, 4, 0, {{0x40, 1}, {0x44, 16}, {0x48, 1}}},
        {"h264_1080_s32", 0, 0, 4, 0, {{0x40, 1}, {0x44, 32}, {0x48, 1}}},
        {"h264_1080_s0", 0, 0, 4, 0, {{0x40, 1}, {0x44, 0}, {0x48, 1}}},
        {"h264_1080_count16_only", 0, 0, 4, 0, {{0x44, 16}}},
        {"h264_720_s16", 0, 0, 3, 0, {{0x40, 1}, {0x44, 16}, {0x48, 1}}},
        {"h264_1080_t2", 0, 0, 4, 2, {}},
        {"hevc_1080_t2", 1, 0, 4, 2, {}},
        {"h264_720_t2", 0, 0, 3, 2, {}},
        {"h264_1440_t2", 0, 0, 5, 2, {}},
        {"h264_1080_t3", 0, 0, 4, 3, {}},
        {"h264_1080_base_again", 0, 0, 4, 0, {}},
    };
    for (const auto &v : variants)
        for (int busy = 0; run(buffer, v) == PS_CAPTURE_BUSY;) {
            if (++busy == 8) {
                ps_diag_log("ProsperoSunshine: slice_rate capture busy eight times, stopping\n");
                return 1;
            }
            sleep(5);
        }
    ps_diag_log("ProsperoSunshine: slice_rate done\n");
    return 0;
}
