// SPDX-License-Identifier: GPL-3.0-only
// R3 follow-up: which runtime bitrates does the encoder accept? 20 Mbit/s (peak 22) killed the
// channel in the first spike. Each step opens a fresh channel, sets avg/peak (peak = avg * 1.12,
// like RemotePlay), requests an IDR to make the encoder work, and reads 180 frames.
#include "platform/ps5/capture.h"
#include <vector>
#include <unistd.h>
extern "C" {
#include <ps5/klog.h>
}

int main() {
    // First run: 8, 10, 12 and 15 Mbit/s worked, 20 failed at once; this narrows the gap.
    const uint32_t steps[] = {16000000, 17000000, 18000000, 19000000};
    std::vector<uint8_t> buffer(4 * 1024 * 1024);
    for (const uint32_t avg : steps) {
        ps_capture *s = nullptr;
        if (const int rc = ps_capture_open(&s)) {
            klog_printf("ProsperoSunshine: bitrate open failed rc=%d\n", rc);
            return 1;
        }
        const int set_rc = ps_capture_set_bitrate(s, avg, avg + avg / 100 * 12);
        int frames = 0, error = 0;
        uint8_t desc[0x70];
        for (int polls = 0; frames < 180 && polls < 20000; ++polls) {
            const int rc = ps_capture_read(s, buffer.data(), buffer.size(), desc);
            if (rc < 0) { error = rc; break; }
            if (rc == 0) { usleep(1000); continue; }
            if (++frames % 60 == 0) ps_capture_request_keyframe(s);
        }
        const int close_rc = ps_capture_close(s);
        klog_printf("ProsperoSunshine: bitrate avg=%u set_rc=%d frames=%d error=%d close_rc=%d\n", avg, set_rc, frames,
                    error, close_rc);
        usleep(500000);
    }
    return 0;
}
