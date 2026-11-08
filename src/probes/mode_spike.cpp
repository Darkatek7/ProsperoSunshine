// SPDX-License-Identifier: GPL-3.0-only
// P9.3: does Avcap2 deliver the modes below 1080p? SceRemotePlay 0x6f8d5 maps 640x360, 960x540 and 1280x720 to
// modes 1, 2 and 3. Each mode opens a fresh channel, keeps the stream for three seconds in
// /data/prosperosunshine/spike_<mode>_<codec>_<budget>.bin and reports frames and bytes to spike_modes.txt next to it.
// ffprobe on the PC reads the real dimensions.
#include "platform/ps5/capture.h"
#include <chrono>
#include <cstdio>
#include <unistd.h>
#include <vector>

int main() {
    using clock = std::chrono::steady_clock;
    // HEVC: cfg+0x10c, the frame budget, has a largest value per mode above which Start fails (docs/re/hevc.md).
    const struct { uint32_t resolution, codec, budget; } modes[] = {
        {3, 0, 0},       {2, 0, 0},       {1, 0, 0},       {3, 1, 0x40000}, {3, 1, 0x30000}, {3, 1, 0x20000}, {3, 1, 0x10000},
        {2, 1, 0x30000}, {2, 1, 0x20000}, {2, 1, 0x10000}, {2, 1, 0x8000},  {1, 1, 0x20000}, {1, 1, 0x10000}, {1, 1, 0x8000},
        {1, 1, 0x4000}};
    std::vector<uint8_t> buffer(4 * 1024 * 1024);
    FILE *report = fopen("/data/prosperosunshine/spike_modes.txt", "w");
    if (!report) return 1;
    for (const auto &mode : modes) {
        ps_capture_set_video_mode(mode.resolution, 1);
        ps_capture_set_codec(mode.codec);
        ps_capture_set_opening_bitrate(3000000, 3000000);
        ps_capture_override_config(mode.budget ? 0x10c : 0, mode.budget);
        ps_capture *s = nullptr;
        const int open_rc = ps_capture_open(&s);
        if (open_rc) {
            fprintf(report, "mode=%u codec=%u open_rc=%d raw=%#x\n", mode.resolution, mode.codec, open_rc,
                    static_cast<unsigned>(ps_capture_last_open_rc()));
            fflush(report);
            usleep(1000000);
            continue;
        }
        char path[96];
        snprintf(path, sizeof path, "/data/prosperosunshine/spike_%u_%u_%x.bin", mode.resolution, mode.codec, mode.budget);
        FILE *out = fopen(path, "wb");
        const int header_size = ps_capture_headers(s, buffer.data(), buffer.size());
        if (out && header_size > 0) fwrite(buffer.data(), 1, static_cast<size_t>(header_size), out);
        int frames = 0, error = 0;
        size_t bytes = 0;
        uint8_t desc[0x70];
        const auto start = clock::now();
        while (clock::now() - start < std::chrono::seconds(3)) {
            const int rc = ps_capture_read(s, buffer.data(), buffer.size(), desc);
            if (rc < 0) { error = rc; break; }
            if (rc == 0) { usleep(1000); continue; }
            ++frames;
            bytes += static_cast<size_t>(rc);
            if (out) fwrite(buffer.data(), 1, static_cast<size_t>(rc), out);
        }
        if (out) fclose(out);
        const int close_rc = ps_capture_close(s);
        fprintf(report, "mode=%u codec=%u frames=%d bytes=%zu headers=%d error=%#x close_rc=%d\n", mode.resolution, mode.codec,
                frames, bytes, header_size, static_cast<unsigned>(error), close_rc);
        fflush(report);
        usleep(1000000);
    }
    fclose(report);
    return 0;
}
