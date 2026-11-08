// SPDX-License-Identifier: GPL-3.0-only
// R2 follow-up: OpenAudio answered 0x81950008 with the RemotePlay-like config. Try the open
// parameters the static analysis left open (purpose, config word 0x1C, open flag), each with the
// ~40 ms retry RemotePlay uses, while video keeps running. On the first success, read 3 s of
// packets into /data/prosperosunshine-probe/audio.bin ([u32 size][16-byte header][payload]).
#include "platform/ps5/capture.h"
#include "platform/ps5/pad.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
extern "C" {
#include <ps5/klog.h>
}

#define LOG(...) klog_printf("ProsperoSunshine: audio " __VA_ARGS__)
using clock_type = std::chrono::steady_clock;

namespace {
std::vector<uint8_t> video(4 * 1024 * 1024);

void drain_video(ps_capture *s) {
    uint8_t desc[0x70];
    while (ps_capture_read(s, video.data(), video.size(), desc) > 0) {}
}

bool record(ps_capture *s, uint32_t encoder, const char *path) {
    FILE *out = std::fopen(path, "wb");
    std::vector<uint8_t> packet(0xC000);
    uint8_t header[0x10];
    int packets = 0, min_size = 1 << 30, max_size = 0;
    uint64_t bytes = 0;
    const auto start = clock_type::now();
    while (clock_type::now() - start < std::chrono::seconds(3)) {
        for (int rc; (rc = ps_capture_audio_read(s, packet.data(), packet.size(), header)) != 0;) {
            if (rc < 0) {
                LOG("read error %d after %d packets\n", rc, packets);
                if (out) std::fclose(out);
                return false;
            }
            if (packets < 4) {
                uint64_t h0, h1;
                std::memcpy(&h0, header, 8);
                std::memcpy(&h1, header + 8, 8);
                LOG("packet=%d bytes=%d hdr=%#llx,%#llx first=%02x %02x %02x %02x\n", packets, rc,
                    static_cast<unsigned long long>(h0), static_cast<unsigned long long>(h1), packet[0], packet[1],
                    packet[2], packet[3]);
            }
            ++packets;
            bytes += static_cast<uint64_t>(rc);
            min_size = std::min(min_size, rc);
            max_size = std::max(max_size, rc);
            if (out) {
                const uint32_t size = static_cast<uint32_t>(rc);
                std::fwrite(&size, 4, 1, out);
                std::fwrite(header, 1, sizeof header, out);
                std::fwrite(packet.data(), 1, static_cast<size_t>(rc), out);
            }
        }
        drain_video(s);
        usleep(2000);
    }
    if (out) std::fclose(out);
    const double seconds = std::chrono::duration<double>(clock_type::now() - start).count();
    LOG("encoder=%u packets=%d per_s=%.1f bytes_per_s=%.0f size=%d..%d\n", encoder, packets, packets / seconds,
        bytes / seconds, packets ? min_size : 0, max_size);
    return packets > 0;
}
}

int main() {
    mkdir("/data/prosperosunshine-probe", 0755);
    ps_capture *s = nullptr;
    if (const int rc = ps_capture_open(&s)) {
        LOG("video open failed rc=%d\n", rc);
        return 1;
    }
    // cfg+0x1C must be the active ResourceArbitrator id (180), see the R2 follow-up analysis.
    struct Variant { uint32_t encoder, bitrate, frame_index; const char *file; };
    const Variant variants[] = {
        {2, 64000, 0, "/data/prosperosunshine-probe/audio_opus20.bin"},
        {2, 64000, 1, "/data/prosperosunshine-probe/audio_opus10.bin"},
        {0, 0, 0, "/data/prosperosunshine-probe/audio_pcm.bin"},
    };
    int passed = 0;
    for (const auto &v : variants) {
        const int rc = ps_capture_audio_open_ex(s, v.encoder, v.bitrate, v.frame_index, 2, 180, 0);
        LOG("open encoder=%u frame_index=%u rc=%#x\n", v.encoder, v.frame_index, static_cast<unsigned>(rc));
        if (rc) continue;
        passed += record(s, v.encoder, v.file);
        ps_capture_audio_close(s);
    }
    const int close_rc = ps_capture_close(s);
    LOG("result=%s variants=%d/3 close_rc=%d\n", passed == 3 ? "PASS" : "FAIL", passed, close_rc);
    return passed == 3 ? 0 : 1;
}
