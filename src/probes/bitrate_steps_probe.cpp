// SPDX-License-Identifier: GPL-3.0-only
// "Lowest common bitrate" for co-op: can H.264 go back up to its opening bitrate after SetVideoBitrate lowered it? Raising
// above the opening value kills the channel (0x80c78000 on the next read, docs/re/hevc.md); HEVC came back from 30 -> 10 -> 30.
// Opens one channel the host's way (ps_capture_open) at the opening rate, reads `seconds`, then sets each step in turn with
// SetVideoBitrate (peak 1.12 times the average for H.264, 1.1 for HEVC) and reads `seconds` again. Logs per second the
// frames, kbit/s and longest gap between frames, every SetVideoBitrate result and the first read error, then a line per
// phase. Log: /data/prosperosunshine/bitrate_steps_probe.log and klog. Runs (1 + steps) * seconds, 20 s with the defaults.
//
// The host must be stopped first (the app's "Stop host"): the probe refuses while 127.0.0.1:47989 or :47995 answers.
// Load the encoder: on the quiet home screen the encoder stays far below any target. The Mario 64 demo moves without input;
// a noise page in the browser card fills every bitrate.
//
// /data/prosperosunshine/bitrate_steps_probe.conf (optional), one setting per line, defaults in brackets:
//   codec h264|hevc [h264]   resolution 720|1080|... [1080]   open <mbit> [30]   steps <mbit> ... [5 30 5]   seconds <n> [5]
#include "platform/ps5/capture.h"
#include "platform/ps5/diag.h"
#include "probe_host_check.hpp"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>
extern "C" {
#include <ps5/kernel.h>
}

#define LOG(...) ps_diag_log("ProsperoSunshine: bitrate_steps " __VA_ARGS__)

namespace {

using steady = std::chrono::steady_clock;
constexpr const char *conf_path = "/data/prosperosunshine/bitrate_steps_probe.conf";

struct settings {
    uint32_t codec = 0, resolution = 4, open_mbit = 30;
    std::vector<uint32_t> steps{5, 30, 5};
    int seconds = 5;
};

uint32_t resolution_of(const char *word) {
    static const char *const heights[] = {"", "360", "540", "720", "1080", "1440", "2160"};
    for (uint32_t i = 1; i < 7; i++)
        if (!std::strcmp(word, heights[i])) return i;
    return 0;
}

bool valid_mbit(long value) { return value >= 1 && value <= 150; }

// Returns false on a line it can't read: better no run than a different one.
bool read_conf(settings &s) {
    FILE *in = std::fopen(conf_path, "r");
    if (!in) {
        LOG("no %s: defaults\n", conf_path);
        return true;
    }
    char line[256];
    int number = 0;
    bool ok = true;
    while (ok && std::fgets(line, sizeof line, in)) {
        number++;
        if (char *hash = std::strchr(line, '#')) *hash = 0;
        char *rest = nullptr;
        const char *key = strtok_r(line, " \t\r\n", &rest);
        if (!key) continue;
        const char *value = strtok_r(nullptr, " \t\r\n", &rest);
        if (!value) {
            ok = false;
        } else if (!std::strcmp(key, "codec")) {
            ok = !std::strcmp(value, "h264") || !std::strcmp(value, "hevc");
            s.codec = !std::strcmp(value, "hevc");
        } else if (!std::strcmp(key, "resolution")) {
            s.resolution = resolution_of(value);
            ok = s.resolution != 0;
        } else if (!std::strcmp(key, "open")) {
            const long mbit = std::strtol(value, nullptr, 10);
            ok = valid_mbit(mbit);
            s.open_mbit = static_cast<uint32_t>(mbit);
        } else if (!std::strcmp(key, "seconds")) {
            s.seconds = static_cast<int>(std::strtol(value, nullptr, 10));
            ok = s.seconds >= 1 && s.seconds <= 60;
        } else if (!std::strcmp(key, "steps")) {
            s.steps.clear();
            for (; value && ok && s.steps.size() < 16; value = strtok_r(nullptr, " \t\r\n", &rest)) {
                const long mbit = std::strtol(value, nullptr, 10);
                ok = valid_mbit(mbit);
                s.steps.push_back(static_cast<uint32_t>(mbit));
            }
        } else {
            ok = false;
        }
    }
    std::fclose(in);
    if (!ok) LOG("refused: %s line %d is not one of codec, resolution, open, steps, seconds with a valid value\n", conf_path, number);
    return ok;
}

uint32_t peak_of(const settings &s, uint32_t average) { return s.codec ? average / 100 * 110 : average / 100 * 112; }

struct phase_stats {
    uint32_t target_mbit = 0;
    int set_rc = 0;
    long frames = 0, max_gap_ms = 0;
    uint64_t bytes = 0, settled_bytes = 0;  // settled: without the phase's first second
};

}  // namespace

int main() {
    char header[128];
    std::snprintf(header, sizeof header, "ProsperoSunshine: bitrate_steps_probe firmware=%#x", kernel_get_fw_version());
    ps_diag_open("/data/prosperosunshine/bitrate_steps_probe.log", header);
    if (probe::host_running()) {
        LOG("refused: the host is running (127.0.0.1:47989 or :47995 answers); stop it first (app: Stop host)\n");
        return 1;
    }
    settings s;
    if (!read_conf(s)) return 1;
    std::string steps;
    for (const uint32_t step : s.steps) steps += " " + std::to_string(step);
    LOG("codec=%s resolution=%u open=%u Mbit/s steps=%s seconds=%d\n", s.codec ? "hevc" : "h264", s.resolution, s.open_mbit,
        steps.c_str(), s.seconds);

    ps_capture_set_codec(s.codec);
    ps_capture_set_video_mode(s.resolution, 1);
    ps_capture_set_opening_bitrate(s.open_mbit * 1000000u, peak_of(s, s.open_mbit * 1000000u));
    ps_capture *session = nullptr;
    if (const int rc = ps_capture_open(&session)) {
        LOG("open failed rc=%d raw=%#x\n", rc, static_cast<unsigned>(ps_capture_last_open_rc()));
        return 1;
    }
    std::vector<uint8_t> buffer(8 * 1024 * 1024);
    LOG("headers bytes=%d\n", ps_capture_headers(session, buffer.data(), buffer.size()));

    std::vector<phase_stats> phases(1 + s.steps.size());
    phases[0].target_mbit = s.open_mbit;
    for (size_t i = 0; i < s.steps.size(); i++) phases[i + 1].target_mbit = s.steps[i];
    const steady::time_point begin = steady::now();
    steady::time_point last_frame = begin;
    int error = 0;
    long error_ms = -1, total_frames = 0;
    for (size_t p = 0; p < phases.size() && !error; p++) {
        phase_stats &phase = phases[p];
        if (p > 0) {
            const uint32_t average = phase.target_mbit * 1000000u;
            phase.set_rc = ps_capture_set_bitrate(session, average, peak_of(s, average));
            LOG("t_ms=%ld SetVideoBitrate %u -> %u Mbit/s rc=%d\n",
                static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(steady::now() - begin).count()),
                phases[p - 1].target_mbit, phase.target_mbit, phase.set_rc);
        }
        const steady::time_point phase_start = steady::now();
        for (int second = 0; second < s.seconds && !error; second++) {
            const steady::time_point second_end = phase_start + std::chrono::seconds(second + 1);
            long frames = 0, gap_ms = 0;
            uint64_t bytes = 0;
            uint8_t descriptor[0x88];
            while (steady::now() < second_end) {
                const int rc = s.codec ? ps_capture_read2(session, buffer.data(), buffer.size(), descriptor)
                                       : ps_capture_read(session, buffer.data(), buffer.size(), descriptor);
                if (rc < 0) {
                    error = rc;
                    error_ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(steady::now() - begin).count());
                    LOG("read error rc=%#x at t_ms=%ld in phase %zu (%u Mbit/s); capture.c logs the raw code for H.264 above\n",
                        static_cast<unsigned>(rc), error_ms, p, phase.target_mbit);
                    break;
                }
                if (rc == 0) {
                    usleep(1000);
                    continue;
                }
                const steady::time_point now = steady::now();
                if (total_frames) {
                    const long gap = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame).count());
                    gap_ms = gap > gap_ms ? gap : gap_ms;
                }
                last_frame = now;
                total_frames++;
                frames++;
                bytes += static_cast<uint64_t>(rc);
            }
            LOG("t=%ld phase=%zu target=%u frames=%ld kbit=%llu max_gap_ms=%ld\n",
                static_cast<long>(p) * s.seconds + second + 1, p, phase.target_mbit, frames,
                static_cast<unsigned long long>(bytes * 8 / 1000), gap_ms);
            phase.frames += frames;
            phase.bytes += bytes;
            if (second > 0) phase.settled_bytes += bytes;
            phase.max_gap_ms = gap_ms > phase.max_gap_ms ? gap_ms : phase.max_gap_ms;
        }
    }
    for (size_t p = 0; p < phases.size(); p++) {
        const phase_stats &phase = phases[p];
        const double settled = s.seconds > 1 ? static_cast<double>(phase.settled_bytes) * 8 / 1000 / (s.seconds - 1) : 0;
        LOG("result phase=%zu target=%u set_rc=%d frames=%ld kbit_avg=%.0f kbit_settled=%.0f max_gap_ms=%ld\n", p, phase.target_mbit,
            phase.set_rc, phase.frames, static_cast<double>(phase.bytes) * 8 / 1000 / s.seconds, settled, phase.max_gap_ms);
    }
    const int close_rc = ps_capture_close(session);
    LOG("done error=%#x error_ms=%ld close_rc=%d\n", static_cast<unsigned>(error), error_ms, close_rc);
    return error ? 1 : 0;
}
