// SPDX-License-Identifier: GPL-3.0-only
// Surround: OpenAudio's encoder type 3 is "PCM, 12 channels, Remote Play only" in the server's validator
// (docs/re/avcap2-exports.md, R2); the host reads type 0, stereo float. This opens video as the host does, then reads 3 s
// of type 0 and of type 3 on the same session. Each packet goes to /data/prosperosunshine/surround_probe/<name>.bin as
// [u32 size][16-byte header][payload]; the log has packet sizes, rates and the first headers. Bytes per second over
// 48 kHz give the bytes per sample frame, so the channel count and sample format follow offline.
// The host must not be streaming: a busy capture is tried again after 5 s, eight times.
#include "platform/ps5/capture.h"
#include "platform/ps5/diag.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
const char *const dir = "/data/prosperosunshine/surround_probe";
std::vector<uint8_t> video(4 * 1024 * 1024);

void drain_video(ps_capture *s) {
    uint8_t desc[0x88];
    while (ps_capture_read(s, video.data(), video.size(), desc) > 0) {}
}

void record(ps_capture *s, uint32_t encoder, const char *name) {
    const int rc = ps_capture_audio_open_ex(s, encoder, 0, 0, 2, 180, 0);
    ps_diag_log("ProsperoSunshine: surround %s: open encoder=%u rc=%#x\n", name, encoder, static_cast<unsigned>(rc));
    if (rc) return;
    char path[128];
    std::snprintf(path, sizeof path, "%s/%s.bin", dir, name);
    FILE *out = std::fopen(path, "wb");
    std::vector<uint8_t> packet(0x40000);
    uint8_t header[0x10];
    int packets = 0, min_size = 1 << 30, max_size = 0, error = 0;
    uint64_t bytes = 0;
    const auto start = std::chrono::steady_clock::now();
    while (!error && std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
        for (int n; (n = ps_capture_audio_read(s, packet.data(), packet.size(), header)) != 0;) {
            if (n < 0) {
                error = n;
                break;
            }
            if (packets < 3) {
                uint64_t h0, h1;
                std::memcpy(&h0, header, 8);
                std::memcpy(&h1, header + 8, 8);
                ps_diag_log("ProsperoSunshine: surround %s: packet %d bytes=%d hdr=%#llx,%#llx\n", name, packets, n,
                            static_cast<unsigned long long>(h0), static_cast<unsigned long long>(h1));
            }
            ++packets;
            bytes += static_cast<uint64_t>(n);
            min_size = std::min(min_size, n);
            max_size = std::max(max_size, n);
            if (out) {
                const uint32_t size = static_cast<uint32_t>(n);
                std::fwrite(&size, 4, 1, out);
                std::fwrite(header, 1, sizeof header, out);
                std::fwrite(packet.data(), 1, static_cast<size_t>(n), out);
            }
        }
        drain_video(s);
        usleep(2000);
    }
    if (out) std::fclose(out);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    ps_diag_log("ProsperoSunshine: surround %s: packets=%d per_s=%.1f bytes_per_s=%.0f bytes_per_frame_at_48k=%.2f "
                "size=%d..%d err=%#x close=%d\n",
                name, packets, packets / seconds, bytes / seconds, bytes / seconds / 48000, packets ? min_size : 0,
                max_size, static_cast<unsigned>(error), ps_capture_audio_close(s));
}
}  // namespace

int main() {
    mkdir(dir, 0777);
    ps_diag_open("/data/prosperosunshine/surround_probe/surround_probe.log", "ProsperoSunshine: surround_probe");
    ps_capture *s = nullptr;
    for (int busy = 0;; busy++) {
        const int rc = ps_capture_open(&s);
        if (rc == 0) break;
        ps_diag_log("ProsperoSunshine: surround video open rc=%d\n", rc);
        if (rc != PS_CAPTURE_BUSY || busy == 7) return 1;
        sleep(5);
    }
    record(s, 0, "pcm_type0");
    record(s, 3, "pcm_type3");
    record(s, 0, "pcm_type0_again");
    ps_diag_log("ProsperoSunshine: surround done close=%d\n", ps_capture_close(s));
    return 0;
}
