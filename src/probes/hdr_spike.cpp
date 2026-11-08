// SPDX-License-Identifier: GPL-3.0-only
// 10-bit HEVC at 1080p60 (docs/re/hdr.md, steps 3 and 4): 8 bit as the reference, then 10 bit as the host would open it. If that
// fails, one change at a time until one starts. Each stream goes to /data/prosperosunshine/spike_hdr_<bits>_<name>.bin for ffprobe
// on the PC: pix_fmt, colour primaries and transfer. Logs to klog, and the capture logs the video out mode of the current output.
// Not during a stream.
#include "platform/ps5/capture.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <vector>
extern "C" {
#include <ps5/klog.h>
}

namespace {

constexpr int run_seconds = 5;
constexpr uint32_t average = 30000000, peak = 33000000;

struct Variant {
    const char *name;
    uint32_t average, peak;
    int resource;  // 0: as for the mode
    size_t field;  // 0: none
    uint32_t value;
};

constexpr Variant as_host = {"host", average, peak, 0, 0, 0};
constexpr Variant retries[] = {
    // Bytes per frame, as Remote Play computes them; the host's value is only measured for 8 bit.
    {"budget", average, peak, 0, 0x10c, static_cast<uint32_t>(uint64_t{peak} * 1001 / (60 * 1000) / 8)},
    {"15mbit", 15000000, 16500000, 0, 0, 0},
    {"profile182", average, peak, 182, 0, 0},
    {"purpose6", average, peak, 0, 0x14, 6},
};

bool run(uint32_t ten_bit, const Variant &variant, std::vector<uint8_t> &buffer) {
    ps_capture_set_codec(1);
    ps_capture_set_video_mode(4, 1);
    ps_capture_set_ten_bit(ten_bit);
    ps_capture_set_opening_bitrate(variant.average, variant.peak);
    if (variant.resource) ps_capture_set_resource(variant.resource);
    ps_capture_override_config(variant.field, variant.value);
    ps_capture *s = nullptr;
    const int open = ps_capture_open(&s);
    ps_capture_override_config(0, 0);
    if (open) {
        klog_printf("ProsperoSunshine: hdr_spike bits=%u %s failed rc=%d raw=%#x\n", ten_bit ? 10 : 8, variant.name, open,
                    static_cast<unsigned>(ps_capture_last_open_rc()));
        sleep(1);
        return false;
    }
    char path[96];
    snprintf(path, sizeof path, "/data/prosperosunshine/spike_hdr_%u_%s.bin", ten_bit ? 10 : 8, variant.name);
    FILE *out = fopen(path, "wb");
    const int header_size = ps_capture_headers(s, buffer.data(), buffer.size());
    if (header_size > 0 && out) fwrite(buffer.data(), 1, static_cast<size_t>(header_size), out);

    uint8_t desc[0x88];
    size_t bytes = 0;
    int frames = 0, idr = 0, error = 0;
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(run_seconds)) {
        const int rc = ps_capture_read2(s, buffer.data(), buffer.size(), desc);
        if (rc < 0) {
            error = rc;
            break;
        }
        if (rc == 0) {
            usleep(500);
            continue;
        }
        int type = 0;
        memcpy(&type, desc + 0x2c, sizeof type);
        ++frames;
        idr += type == 1;
        if (out) fwrite(buffer.data(), 1, static_cast<size_t>(rc), out);
        bytes += static_cast<size_t>(rc);
    }
    if (out) fclose(out);
    klog_printf("ProsperoSunshine: hdr_spike bits=%u %s headers=%d frames=%d idr=%d mbit=%d error=%#x close=%d\n", ten_bit ? 10 : 8,
                variant.name, header_size, frames, idr, static_cast<int>(bytes * 8 / run_seconds / 1000000),
                static_cast<unsigned>(error), ps_capture_close(s));
    sleep(1);
    return frames > 0;
}

}  // namespace

int main() {
    std::vector<uint8_t> buffer(8 * 1024 * 1024);
    run(0, as_host, buffer);
    if (!run(1, as_host, buffer)) {
        for (const auto &variant : retries) {
            if (run(1, variant, buffer)) break;
        }
    }
    ps_capture_set_ten_bit(0);
    klog_printf("ProsperoSunshine: hdr_spike done\n");
    return 0;
}
