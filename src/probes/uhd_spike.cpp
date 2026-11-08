// SPDX-License-Identifier: GPL-3.0-only
// Milestone 0.4: does another ResourceArbitrator profile allow 1440p or 2160p? Resource 180 grants scanin[2K] only
// (docs/re/avcap2-exports.md). Tries each profile with each mode and logs the result to klog; a stream that opens goes
// to /data/prosperosunshine/spike_uhd_<n>.bin, so that ffprobe on the PC can check its real size. Result on 13.00: H.264 with
// 177 and 182; the host uses 182. HEVC needs another purpose as well (hevc_memory_spike).
#include "platform/ps5/capture.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <unistd.h>
#include <vector>
extern "C" {
#include <ps5/klog.h>
}

namespace {

constexpr size_t max_saved = 8 * 1024 * 1024;

void run(int resource, uint32_t codec, uint32_t mode, int &saved, std::vector<uint8_t> &buffer) {
    ps_capture_set_codec(codec);
    ps_capture_set_video_mode(mode, 1);
    ps_capture_set_resource(resource);
    ps_capture_set_opening_bitrate(40000000, codec ? 44000000 : 44800000);
    ps_capture *s = nullptr;
    const int open_rc = ps_capture_open(&s);
    if (open_rc) {
        klog_printf("ProsperoSunshine: uhd_spike resource=%d codec=%u mode=%u open=%d raw=%#x\n", resource, codec, mode, open_rc,
                    static_cast<unsigned>(ps_capture_last_open_rc()));
        return;
    }
    char path[96];
    snprintf(path, sizeof path, "/data/prosperosunshine/spike_uhd_%d.bin", saved);
    FILE *out = fopen(path, "wb");
    size_t bytes = 0;
    const int header_size = ps_capture_headers(s, buffer.data(), buffer.size());
    if (header_size > 0 && out) fwrite(buffer.data(), 1, static_cast<size_t>(header_size), out);
    int frames = 0, largest = 0, error = 0;
    uint8_t desc[0x88];
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < end) {
        const int rc = codec ? ps_capture_read2(s, buffer.data(), buffer.size(), desc)
                             : ps_capture_read(s, buffer.data(), buffer.size(), desc);
        if (rc < 0) {
            error = rc;
            break;
        }
        if (rc == 0) {
            usleep(1000);
            continue;
        }
        ++frames;
        largest = rc > largest ? rc : largest;
        if (out && bytes < max_saved) fwrite(buffer.data(), 1, static_cast<size_t>(rc), out);
        bytes += static_cast<size_t>(rc);
    }
    if (out) fclose(out);
    klog_printf("ProsperoSunshine: uhd_spike resource=%d codec=%u mode=%u opened file=%d frames=%d bytes=%zu largest=%d headers=%d error=%#x close=%d\n",
                resource, codec, mode, saved, frames, bytes, largest, header_size, static_cast<unsigned>(error), ps_capture_close(s));
    ++saved;
}

}  // namespace

int main() {
    std::vector<uint8_t> buffer(8 * 1024 * 1024);
    int saved = 0;
    for (const int resource : {180, 177, 185, 175, 182})
        for (const uint32_t codec : {0u, 1u})
            for (const uint32_t mode : {5u, 6u}) {  // 1440p, 2160p
                run(resource, codec, mode, saved, buffer);
                sleep(1);
            }
    ps_capture_set_resource(180);
    klog_printf("ProsperoSunshine: uhd_spike done\n");
    return 0;
}
