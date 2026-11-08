// SPDX-License-Identifier: GPL-3.0-only
// P9.3 on other firmware: modes below 1080p. On 13.00 360p, 540p and 720p don't start (mode_spike, STATUS.md). This tries
// 640x360 (1), 960x540 (2), 1280x720 (3) and 1920x1080 (4) for H.264, HEVC 8 bit and HEVC 10 bit at 60 and 30 fps, each on
// a fresh channel with the host's own record at 16 Mbit/s. It reports open/Start, frames, bit/s, the sizes of IDR and
// other frames and slices per frame to /data/prosperosunshine/small_probe/small_probe.log, and keeps each stream with its
// parameter sets next to it, to read the real dimensions on the PC. It asks for an IDR every 500 ms, so that each mode
// has several IDR sizes of the same screen.
// A busy answer is tried again after 5 s: after an odd OpenVideo failure the arbitrator lists the resource for 20-30 s
// (seen by firmware_probe). Eight in a row stop the probe: a stream or another probe has the capture.
#include "platform/ps5/capture.h"
#include "platform/ps5/diag.h"
#include <sys/stat.h>
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

const char *const dir = "/data/prosperosunshine/small_probe";

// Slices per frame: H.264 NAL types 1 and 5, HEVC VCL types 0..31. IDR: H.264 type 5, HEVC 16..21.
void scan(const uint8_t *data, int size, bool hevc, int &slices, bool &idr) {
    slices = 0;
    idr = false;
    for (int i = 0; i + 3 < size; i++)
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            const int type = hevc ? (data[i + 3] >> 1) & 0x3f : data[i + 3] & 0x1f;
            if (hevc ? type < 32 : type == 1 || type == 5) ++slices;
            if (hevc ? type >= 16 && type <= 21 : type == 5) idr = true;
            i += 2;
        }
}

const char *mode_name(uint32_t resolution) {
    return resolution == 1 ? "360p" : resolution == 2 ? "540p" : resolution == 3 ? "720p" : "1080p";
}

// Returns the open's result.
int run(std::vector<uint8_t> &buffer, uint32_t codec, uint32_t ten_bit, uint32_t resolution, uint32_t timing) {
    const bool hevc = codec == 1;
    char label[64];
    snprintf(label, sizeof label, "%s %s%d", hevc ? (ten_bit ? "hevc10" : "hevc8") : "h264", mode_name(resolution),
             timing ? 60 : 30);
    ps_capture_set_codec(codec);
    ps_capture_set_ten_bit(ten_bit);
    ps_capture_set_video_mode(resolution, timing);
    ps_capture *s = nullptr;
    const int open_rc = ps_capture_open(&s);
    if (open_rc) {
        ps_diag_log("ProsperoSunshine: small_probe %s: failed %d raw=%#x\n", label, open_rc,
                    static_cast<unsigned>(ps_capture_last_open_rc()));
        usleep(500000);
        return open_rc;
    }
    char path[128];
    snprintf(path, sizeof path, "%s/%s_%s%d.%s", dir, hevc ? (ten_bit ? "hevc10" : "hevc8") : "h264", mode_name(resolution),
             timing ? 60 : 30, hevc ? "h265" : "h264");
    FILE *out = fopen(path, "wb");
    const int header_size = ps_capture_headers(s, buffer.data(), buffer.size());
    if (out && header_size > 0) fwrite(buffer.data(), 1, static_cast<size_t>(header_size), out);
    uint8_t desc[0x88];
    int frames = 0, idrs = 0, slice_min = 1 << 30, slice_max = 0, error = 0, requests = 0;
    long bytes = 0, idr_bytes = 0, idr_max = 0, other_bytes = 0, other_max = 0;
    const auto begin = std::chrono::steady_clock::now();
    auto next_idr = begin;
    while (std::chrono::steady_clock::now() - begin < std::chrono::seconds(3)) {
        if (std::chrono::steady_clock::now() >= next_idr) {
            if (ps_capture_request_keyframe(s) == 0) ++requests;
            next_idr += std::chrono::milliseconds(500);
        }
        const int rc = hevc ? ps_capture_read2(s, buffer.data(), buffer.size(), desc)
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
        bytes += rc;
        if (out) fwrite(buffer.data(), 1, static_cast<size_t>(rc), out);
        int n;
        bool idr;
        scan(buffer.data(), rc, hevc, n, idr);
        if (n < slice_min) slice_min = n;
        if (n > slice_max) slice_max = n;
        if (idr) {
            ++idrs;
            idr_bytes += rc;
            if (rc > idr_max) idr_max = rc;
        } else {
            other_bytes += rc;
            if (rc > other_max) other_max = rc;
        }
    }
    if (out) fclose(out);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    const int close_rc = ps_capture_close(s);
    ps_diag_log("ProsperoSunshine: small_probe %s: ok frames=%d fps=%.1f kbit/s=%.0f slices=%d..%d idr=%d (asked %d) "
                "idr_avg=%ld idr_max=%ld other_avg=%ld other_max=%ld headers=%d err=%#x close=%d file=%s\n",
                label, frames, frames / seconds, bytes * 8 / seconds / 1000, frames ? slice_min : 0, slice_max, idrs,
                requests, idrs ? idr_bytes / idrs : 0, idr_max, frames > idrs ? other_bytes / (frames - idrs) : 0,
                other_max, header_size, static_cast<unsigned>(error), close_rc, path);
    usleep(500000);
    return 0;
}

}  // namespace

int main() {
    mkdir(dir, 0777);
    char header[96];
    snprintf(header, sizeof header, "ProsperoSunshine: small_probe firmware=%#x", kernel_get_fw_version());
    ps_diag_open("/data/prosperosunshine/small_probe/small_probe.log", header);
    // The host's peak for HEVC: average * 1.10.
    ps_capture_set_opening_bitrate(16000000, 17600000);
    std::vector<uint8_t> buffer(16 * 1024 * 1024);
    static const struct { uint32_t codec, ten_bit; } codecs[] = {{0, 0}, {1, 0}, {1, 1}};
    static const uint32_t resolutions[] = {4, 3, 2, 1};
    for (uint32_t timing : {1u, 0u})
        for (const auto &c : codecs)
            for (uint32_t resolution : resolutions)
                for (int busy = 0; run(buffer, c.codec, c.ten_bit, resolution, timing) == PS_CAPTURE_BUSY;) {
                    if (++busy == 8) {
                        ps_diag_log("ProsperoSunshine: small_probe capture busy eight times, stopping\n");
                        return 1;
                    }
                    sleep(5);
                }
    ps_diag_log("ProsperoSunshine: small_probe done\n");
    return 0;
}
