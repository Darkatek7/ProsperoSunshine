// SPDX-License-Identifier: GPL-3.0-only
// For a firmware where OpenVideo fails with 0x81950008: tries the resources Remote Play uses on 13.00, and our resource
// at other places in the config. Writes to host_diag.log, so the app's report shows the result. Not during a stream.
#include "platform/ps5/capture.h"
#include "platform/ps5/diag.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <unistd.h>
#include <vector>
extern "C" {
#include <ps5/kernel.h>
}

namespace {

struct variant {
    const char *name;
    int resource;
    size_t field;  // config field that also gets the resource, 0 = none
};

int read_frames(ps_capture *s, std::vector<uint8_t> &buffer) {
    uint8_t desc[0x70];
    int frames = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < end) {
        const int rc = ps_capture_read(s, buffer.data(), buffer.size(), desc);
        if (rc < 0) return frames ? frames : rc;
        if (rc > 0) ++frames;
        else usleep(1000);
    }
    return frames;
}

}  // namespace

int main() {
    char header[96];
    snprintf(header, sizeof header, "ProsperoSunshine: open_probe firmware=%#x", kernel_get_fw_version());
    ps_diag_open("/data/prosperosunshine/host_diag.log", header);
    static const variant variants[] = {
        {"resource 180", 180, 0},       {"resource 177", 177, 0},       {"resource 185", 185, 0},
        {"resource 175", 175, 0},       {"resource 182", 182, 0},       {"180 also at 0x20", 180, 0x20},
        {"180 also at 0x2c", 180, 0x2c},
    };
    std::vector<uint8_t> buffer(8 * 1024 * 1024);
    ps_capture_set_opening_bitrate(6000000, 6720000);
    for (const variant &v : variants) {
        ps_diag_log("ProsperoSunshine: open_probe try %s\n", v.name);
        ps_capture_set_resource(v.resource);
        ps_capture_override_config(v.field, static_cast<uint32_t>(v.resource));
        ps_capture *s = nullptr;
        const int open_rc = ps_capture_open(&s);
        if (open_rc) {
            ps_diag_log("ProsperoSunshine: open_probe result %s: open failed %d raw=%#x\n", v.name, open_rc,
                        static_cast<unsigned>(ps_capture_last_open_rc()));
        } else {
            const int frames = read_frames(s, buffer);
            ps_diag_log("ProsperoSunshine: open_probe result %s: opened, frames=%d close=%d\n", v.name, frames, ps_capture_close(s));
        }
        sleep(1);
    }
    ps_diag_log("ProsperoSunshine: open_probe done\n");
    return 0;
}
