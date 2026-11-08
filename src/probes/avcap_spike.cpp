// SPDX-License-Identifier: GPL-3.0-only
// Live check of the spikes R1-R3 (docs/re/avcap2-exports.md):
//   A  RequestKeyFrame ten frames after a periodic IDR: how many frames until the next IDR?
//   B  SetVideoBitrate down and up: does the byte rate follow?
//   C  OpenAudio as PCM and as Opus: packet size, rate and header; packets go to
//      /data/prosperosunshine-probe/audio_{pcm,opus}.bin as [u32 size][16-byte header][payload].
#include "platform/ps5/capture.h"
#include "video/annex_b.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
extern "C" {
#include <ps5/klog.h>
}

using clock_type = std::chrono::steady_clock;
#define LOG(...) klog_printf("ProsperoSunshine: spike " __VA_ARGS__)

namespace {
std::vector<uint8_t> video(4 * 1024 * 1024);
uint64_t frame_count = 0;

// Next video frame (blocking poll); returns its size, sets idr. -1 on error.
int next_frame(ps_capture *s, bool &idr) {
    uint8_t desc[0x70];
    for (int i = 0; i < 2000; ++i) {
        const int rc = ps_capture_read(s, video.data(), video.size(), desc);
        if (rc < 0) return -1;
        if (rc > 0) {
            idr = prospero::has_idr(std::span(video.data(), static_cast<size_t>(rc)));
            ++frame_count;
            return rc;
        }
        usleep(1000);
    }
    return -1;
}

bool skip_to_idr(ps_capture *s) {
    bool idr = false;
    for (int i = 0; i < 200; ++i)
        if (next_frame(s, idr) < 0) return false;
        else if (idr) return true;
    return false;
}

bool phase_keyframe(ps_capture *s, int &successes) {
    successes = 0;
    for (int trial = 0; trial < 4; ++trial) {
        if (!skip_to_idr(s)) return false;
        bool idr = false;
        for (int i = 0; i < 10; ++i)
            if (next_frame(s, idr) < 0) return false;
        const int rc = ps_capture_request_keyframe(s);
        int distance = -1;
        for (int i = 1; i <= 120; ++i) {
            if (next_frame(s, idr) < 0) return false;
            if (idr) { distance = i; break; }
        }
        LOG("keyframe trial=%d rc=%d frames_to_idr=%d (periodic would be 50)\n", trial, rc, distance);
        successes += rc == 0 && distance > 0 && distance <= 5;
    }
    return true;
}

double measure_mbit(ps_capture *s, int frames) {
    uint64_t bytes = 0;
    bool idr = false;
    const auto start = clock_type::now();
    for (int i = 0; i < frames; ++i) {
        const int n = next_frame(s, idr);
        if (n < 0) return -1;
        bytes += static_cast<uint64_t>(n);
    }
    return bytes * 8 / std::chrono::duration<double>(clock_type::now() - start).count() / 1e6;
}

void phase_bitrate(ps_capture *s, double out[3]) {
    out[0] = measure_mbit(s, 240);
    LOG("bitrate set 300k rc=%d\n", ps_capture_set_bitrate(s, 300000, 350000));
    measure_mbit(s, 60);  // let rate control settle
    out[1] = measure_mbit(s, 240);
    // 20 Mbit/s (peak 22) stopped the scan-in in the first run; restore the opening value instead.
    LOG("bitrate set 6M rc=%d\n", ps_capture_set_bitrate(s, 6000000, 6720000));
    measure_mbit(s, 60);
    out[2] = measure_mbit(s, 240);
    LOG("bitrate mbit baseline=%.2f at_300k=%.2f back_at_6M=%.2f\n", out[0], out[1], out[2]);
}

bool phase_audio(ps_capture *s, uint32_t encoder, uint32_t bitrate, uint32_t frame_index, const char *path) {
    if (ps_capture_audio_open(s, encoder, bitrate, frame_index)) {
        LOG("audio encoder=%u open failed\n", encoder);
        return false;
    }
    FILE *out = std::fopen(path, "wb");
    std::vector<uint8_t> packet(0xC000);
    uint8_t header[0x10];
    int packets = 0, logged = 0;
    uint64_t bytes = 0;
    int min_size = 1 << 30, max_size = 0;
    const auto start = clock_type::now();
    bool ok = true, idr = false;
    while (clock_type::now() - start < std::chrono::seconds(3)) {
        for (int rc; (rc = ps_capture_audio_read(s, packet.data(), packet.size(), header)) != 0;) {
            if (rc < 0) { ok = false; break; }
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
            if (logged++ < 4) {
                uint64_t h0, h1;
                std::memcpy(&h0, header, 8);
                std::memcpy(&h1, header + 8, 8);
                LOG("audio encoder=%u packet=%d bytes=%d hdr=%#llx,%#llx first=%02x %02x %02x %02x\n", encoder, packets,
                    rc, static_cast<unsigned long long>(h0), static_cast<unsigned long long>(h1), packet[0], packet[1],
                    packet[2], packet[3]);
            }
        }
        if (!ok || next_frame(s, idr) < 0) { ok = false; break; }  // keep the video ring drained
    }
    const double seconds = std::chrono::duration<double>(clock_type::now() - start).count();
    if (out) std::fclose(out);
    LOG("audio encoder=%u ok=%d packets=%d per_s=%.1f bytes_per_s=%.0f size=%d..%d\n", encoder, ok, packets,
        packets / seconds, bytes / seconds, packets ? min_size : 0, max_size);
    return ps_capture_audio_close(s) == 0 && ok && packets > 0;
}
}

int main() {
    mkdir("/data/prosperosunshine-probe", 0755);
    ps_capture *s = nullptr;
    if (const int rc = ps_capture_open(&s)) {
        LOG("open failed rc=%d\n", rc);
        return 1;
    }
    int keyframes = 0;
    const bool a = phase_keyframe(s, keyframes);
    double rates[3] = {};
    phase_bitrate(s, rates);
    const bool b = rates[0] > 0 && rates[1] > 0 && rates[1] < rates[0] * 0.7;
    const bool pcm = phase_audio(s, 0, 0, 0, "/data/prosperosunshine-probe/audio_pcm.bin");
    const bool opus = phase_audio(s, 2, 64000, 1, "/data/prosperosunshine-probe/audio_opus.bin");
    const int close_rc = ps_capture_close(s);
    LOG("result R1_keyframe=%d/4 R3_bitrate=%d R2_pcm=%d R2_opus=%d frames=%llu close_rc=%d phaseA=%d\n", keyframes, b,
        pcm, opus, static_cast<unsigned long long>(frame_count), close_rc, a);
    return 0;
}
