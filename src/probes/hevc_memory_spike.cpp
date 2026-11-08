// SPDX-License-Identifier: GPL-3.0-only
// HEVC above 1080p: the encoder memory fails with 0x81950003 for purpose 2 at mode 5 or 6, because the service then maps two
// pools it failed to set up (SceAvCapture 0x17e0, 0xa7a0). Other purposes allocate per request. Compares the ones that open:
// frames, bitrate, gaps and delay, and writes each stream to /data/prosperosunshine/spike_purpose_<purpose>_<mode>.bin for ffprobe
// on the PC. Logs to klog. Not during a stream. Result on 13.00: purposes 5, 6 and 7 work, the host uses 6 (docs/re/hevc.md).
#include "platform/ps5/capture.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <vector>
#include <x86intrin.h>
extern "C" {
#include <ps5/klog.h>
}

namespace {

constexpr double ticks_per_ms = 1596260.0;
constexpr size_t max_saved = 48 * 1024 * 1024;
int run_seconds = 6;

void run(uint32_t purpose, uint32_t mode, std::vector<uint8_t> &buffer) {
    ps_capture_set_codec(1);
    ps_capture_set_video_mode(mode, 1);
    ps_capture_set_opening_bitrate(30000000, 33000000);
    ps_capture_override_config(0x14, purpose);
    ps_capture *s = nullptr;
    if (ps_capture_open(&s)) {
        klog_printf("ProsperoSunshine: hevc_purpose purpose=%u mode=%u failed raw=%#x\n", purpose, mode, static_cast<unsigned>(ps_capture_last_open_rc()));
        return;
    }
    char path[96];
    snprintf(path, sizeof path, "/data/prosperosunshine/spike_purpose_%u_%u.bin", purpose, mode);
    FILE *out = fopen(path, "wb");
    const int header_size = ps_capture_headers(s, buffer.data(), buffer.size());
    if (header_size > 0 && out) fwrite(buffer.data(), 1, static_cast<size_t>(header_size), out);

    uint8_t desc[0x88];
    size_t bytes = 0;
    int frames = 0, idr = 0, error = 0, width = 0, height = 0;
    uint64_t last_stamp = 0, longest_gap = 0, delay_sum = 0;
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
        const uint64_t now = __rdtsc();
        uint64_t stamp = 0;
        int type = 0;
        memcpy(&stamp, desc + 0x58, sizeof stamp);
        memcpy(&type, desc + 0x2c, sizeof type);
        memcpy(&width, desc + 0x4c, sizeof width);
        memcpy(&height, desc + 0x50, sizeof height);
        if (last_stamp && stamp > last_stamp && stamp - last_stamp > longest_gap) longest_gap = stamp - last_stamp;
        last_stamp = stamp;
        if (now > stamp) delay_sum += now - stamp;
        ++frames;
        idr += type == 1;
        if (out && bytes < max_saved) fwrite(buffer.data(), 1, static_cast<size_t>(rc), out);
        bytes += static_cast<size_t>(rc);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (out) fclose(out);
    klog_printf("ProsperoSunshine: hevc_purpose purpose=%u mode=%u size=%dx%d frames=%d idr=%d mbit=%d gap_ms=%d delay_ms=%d error=%#x close=%d\n", purpose, mode,
                width, height, frames, idr, static_cast<int>(static_cast<double>(bytes) * 8 / seconds / 1000000),
                static_cast<int>(static_cast<double>(longest_gap) / ticks_per_ms),
                frames ? static_cast<int>(static_cast<double>(delay_sum) / frames / ticks_per_ms) : 0, static_cast<unsigned>(error), ps_capture_close(s));
    sleep(1);
}

}  // namespace

int main() {
    std::vector<uint8_t> buffer(8 * 1024 * 1024);
    unsigned purpose = 2, mode = 4;
    if (FILE *choice = fopen("/data/prosperosunshine/spike_choice.txt", "r")) {
        if (fscanf(choice, "%u %u %d", &purpose, &mode, &run_seconds) != 3) klog_printf("ProsperoSunshine: hevc_purpose bad spike_choice.txt\n");
        fclose(choice);
    }
    run(purpose, mode, buffer);
    ps_capture_override_config(0, 0);
    klog_printf("ProsperoSunshine: hevc_purpose done\n");
    return 0;
}
