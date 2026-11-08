// SPDX-License-Identifier: GPL-3.0-only
// Spike for rumble and trigger effects: logs what the system wants from a virtual controller, whoever added
// it, by polling its handle the way Remote Play polls its own (docs/re/pad-output.md). The handle is the
// number in /data/prosperosunshine/probe_handle. Read-only; it adds no controller and sends no input. Runs
// for ten minutes, so another tool can drive a title meanwhile.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
extern "C" {
#include <ps5/klog.h>
}

namespace {
constexpr unsigned long ioctl_output = 0x80684830UL;

struct Output {
    uint32_t flag_a, flag_b, values[6];
    uint8_t flags[4], motors[4], color[4], triggers[24];
};

int read_output(int fd, int handle, Output &out) {
    out = {};
    const auto ptr = [](void *p) { return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p)); };
    struct {
        uint32_t handle, pad;
        uint64_t fields[12];
    } request = {static_cast<uint32_t>(handle), 0,
                 {ptr(&out.flag_a), ptr(out.flags), ptr(out.motors), ptr(out.color), ptr(out.triggers), ptr(&out.values[0]),
                  ptr(&out.values[1]), ptr(&out.values[2]), ptr(&out.values[3]), ptr(&out.values[4]), ptr(&out.values[5]),
                  ptr(&out.flag_b)}};
    return ioctl(fd, ioctl_output, &request);
}
}

int main() {
    using namespace std::chrono_literals;
    const int fd = open("/dev/hid", O_RDONLY);
    klog_printf("ProsperoSunshine: pad_output start fd=%d\n", fd);
    if (fd < 0) return 1;
    int handle = -1;
    if (FILE *file = std::fopen("/data/prosperosunshine/probe_handle", "r")) {
        std::fscanf(file, "%d", &handle);
        std::fclose(file);
    }
    klog_printf("ProsperoSunshine: pad_output handle=%d\n", handle);
    Output last{};
    bool seen = false;
    const auto end = std::chrono::steady_clock::now() + 600s;
    while (std::chrono::steady_clock::now() < end) {
        Output out;
        if (read_output(fd, handle, out) == 0 && (!seen || std::memcmp(&out, &last, sizeof out))) {
            seen = true;
            last = out;
            klog_printf("ProsperoSunshine: pad_output a=%u flags=%u,%u motors=%u,%u color=%u,%u,%u mode=%u v=%u,%u,%u,%u,%u b=%u\n",
                        out.flag_a, out.flags[0], out.flags[1], out.motors[0], out.motors[1], out.color[0], out.color[1],
                        out.color[2], out.values[0], out.values[1], out.values[2], out.values[3], out.values[4], out.values[5],
                        out.flag_b);
            char hex[2 * sizeof out.triggers + 1] = {};
            for (size_t i = 0; i < sizeof out.triggers; ++i) std::snprintf(hex + 2 * i, 3, "%02x", out.triggers[i]);
            klog_printf("ProsperoSunshine: pad_output triggers=%s\n", hex);
        }
        std::this_thread::sleep_for(10ms);
    }
    close(fd);
    klog_printf("ProsperoSunshine: pad_output done\n");
    return 0;
}
