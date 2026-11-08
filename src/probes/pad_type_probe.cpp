// SPDX-License-Identifier: GPL-3.0-only
// Issue 41: which virtual pad the system takes for a Remote Play one. Adds one pad per line of
// /data/prosperosunshine/pad_type_probe.txt ("<class> <subtype> <id in hex> [seconds]", decimal class and subtype as
// libScePad's type table gives them, docs/re/pad-input.md), holds it neutral and removes it, one after the other. The kernel
// log then says how the login manager took it ("IsRemotePlay:", "CecOneTouchPlay"). Presses nothing.
#include <cerrno>
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
constexpr unsigned long ioctl_add = 0xC018482AUL, ioctl_insert = 0x8018482CUL, ioctl_delete = 0x80104850UL;

int add(int fd, unsigned cls, unsigned subtype, uint32_t id) {
    uint8_t desc[0xA8] = {};
    desc[0] = static_cast<uint8_t>(cls);
    std::memcpy(desc + 0x1C, &id, 4);
    desc[0x20] = static_cast<uint8_t>(subtype);
    int handle = -1;
    struct { uint32_t op, pad; uint64_t desc, handle; } request = {3, 0, reinterpret_cast<uintptr_t>(desc), reinterpret_cast<uintptr_t>(&handle)};
    if (ioctl(fd, ioctl_add, &request) != 0) return -errno;
    return handle;
}

int neutral(int fd, int handle) {
    uint8_t block[0xA8] = {};
    std::memset(block + 0x10, 128, 4);
    block[0x17] = 1;
    const float orientation[4] = {0, 0, 0, 1}, gravity[3] = {0, 1, 0};
    std::memcpy(block + 0x78, orientation, sizeof orientation);
    std::memcpy(block + 0x94, gravity, sizeof gravity);
    struct { uint32_t handle, pad; uint64_t state; uint8_t zero, pad2[7]; } request = {static_cast<uint32_t>(handle), 0, reinterpret_cast<uintptr_t>(block), 0, {}};
    return ioctl(fd, ioctl_insert, &request) == 0 ? 0 : -errno;
}

int remove(int fd, int handle) {
    struct { uint8_t op, pad[7]; uint32_t handle, pad2; } request = {5, {}, static_cast<uint32_t>(handle), 0};
    return ioctl(fd, ioctl_delete, &request) == 0 ? 0 : -errno;
}
}

int main() {
    using namespace std::chrono;
    const int fd = open("/dev/hid", O_RDONLY);
    FILE *file = std::fopen("/data/prosperosunshine/pad_type_probe.txt", "r");
    klog_printf("ProsperoSunshine: pad_type start fd=%d list=%s\n", fd, file ? "yes" : "missing");
    if (fd < 0 || !file) return 1;
    char line[128];
    while (std::fgets(line, sizeof line, file)) {
        unsigned cls = 0, subtype = 0, hold = 10;
        uint32_t id = 0;
        if (std::sscanf(line, "%u %u %x %u", &cls, &subtype, &id, &hold) < 3) continue;
        klog_printf("ProsperoSunshine: pad_type adding class=%u subtype=%u id=%#x for %u s\n", cls, subtype, id, hold);
        const int handle = add(fd, cls, subtype, id);
        klog_printf("ProsperoSunshine: pad_type add -> %d (%#x)\n", handle, static_cast<unsigned>(handle));
        if (handle < 0) continue;
        int errors = 0;
        const auto end = steady_clock::now() + seconds(hold);
        while (steady_clock::now() < end) {
            errors += neutral(fd, handle) != 0;
            std::this_thread::sleep_for(16ms);
        }
        klog_printf("ProsperoSunshine: pad_type removed rc=%d, insert errors %d\n", remove(fd, handle), errors);
        std::this_thread::sleep_for(3s);
    }
    std::fclose(file);
    close(fd);
    klog_printf("ProsperoSunshine: pad_type done\n");
    return 0;
}
