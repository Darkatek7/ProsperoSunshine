// SPDX-License-Identifier: GPL-3.0-only
// Keyboard and mouse: does the system take a virtual keyboard and mouse the way it takes the virtual pad?
// libSceKeyboard and libSceMouse export sceKeyboardVirtualDeviceAddDevice / InsertData and the mouse's pair; the
// requests are built directly, as pad.c does for the pad (docs/re/keyboard-mouse.md). First, how soon a new keyboard
// takes a key: six rounds each add one, wait 0 to 1000 ms, press Right or Left once and remove it. Then both devices
// stay for 8 s, the keyboard presses Right three times and Left once (the focus ends two tiles on), the mouse moves
// without a button, and both go again. It presses nothing that confirms. Calling sceKeyboardInit from a payload
// crashed it, so nothing is read back; the shell's log and the screen show the result. Results go to klog and
// /data/prosperosunshine/kbm_probe.txt.
#include "platform/ps5/pad.h"
#include <cerrno>
#include <chrono>
#include <cstdarg>
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

constexpr unsigned long ioctl_add = 0xC018482AUL, ioctl_delete = 0x80104850UL;
constexpr unsigned long ioctl_keyboard = 0x80104874UL, ioctl_mouse = 0x80104875UL;
constexpr uint8_t hid_right = 0x4F, hid_left = 0x50;

FILE *report;

void say(const char *format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    klog_printf("ProsperoSunshine: kbm %s\n", line);
    if (report) {
        std::fprintf(report, "%s\n", line);
        std::fflush(report);
    }
}

// The add request as sceKeyboardVirtualDeviceAddDevice (op 1, subtype 0xa) and the mouse's (op 0, subtype 0xb) build it.
int add(int fd, uint32_t op, uint8_t subtype, uint32_t user) {
    uint8_t desc[0xA8] = {};
    desc[0] = 2;
    std::memcpy(desc + 0x1C, &user, 4);
    desc[0x20] = subtype;
    int handle = -1;
    struct { uint32_t op, pad; uint64_t desc, handle; } request = {op, 0, reinterpret_cast<uintptr_t>(desc),
                                                                  reinterpret_cast<uintptr_t>(&handle)};
    errno = 0;
    const int rc = ioctl(fd, ioctl_add, &request);
    say("add op=%u subtype=%#x rc=%d errno=%d handle=%d", op, subtype, rc, errno, handle);
    return rc == 0 ? handle : -1;
}

void remove(int fd, int handle) {
    struct { uint8_t op, pad[7]; uint32_t handle, pad2; } request = {5, {}, static_cast<uint32_t>(handle), 0};
    errno = 0;
    const int rc = ioctl(fd, ioctl_delete, &request);
    say("delete handle=%d rc=%d errno=%d", handle, rc, errno);
}

int insert(int fd, unsigned long code, int handle, uint8_t *block) {
    struct { uint32_t handle, pad; uint64_t block; } request = {static_cast<uint32_t>(handle), 0,
                                                              reinterpret_cast<uintptr_t>(block)};
    errno = 0;
    return ioctl(fd, code, &request) == 0 ? 0 : -errno;
}

// The 0x20-byte keyboard block: +9 modifiers, +0xa LEDs, +0xb key count, +0xc connected, +0x10 HID usages.
int keys(int fd, int handle, const uint8_t *usages, uint8_t count) {
    uint8_t block[0x20] = {};
    block[0xb] = count;
    block[0xc] = 1;
    if (count) std::memcpy(block + 0x10, usages, count);
    return insert(fd, ioctl_keyboard, handle, block);
}

// The 0x50-byte mouse block as sceMouseVirtualDeviceInsertData fills it.
int mouse(int fd, int handle, int16_t x, int16_t y, uint8_t buttons) {
    uint8_t block[0x50] = {};
    block[0x10] = 1;
    std::memcpy(block + 0x16, &x, 2);
    std::memcpy(block + 0x18, &y, 2);
    block[0x1a] = buttons;
    const uint32_t four = 4;
    std::memcpy(block + 0x24, &four, 4);
    return insert(fd, ioctl_mouse, handle, block);
}

}  // namespace

int main() {
    using namespace std::chrono_literals;
    report = std::fopen("/data/prosperosunshine/kbm_probe.txt", "w");
    const uint32_t user = ps_pad_default_user();
    say("user=%#x", user);
    const int fd = open("/dev/hid", O_RDONLY);
    say("open /dev/hid fd=%d errno=%d", fd, fd < 0 ? errno : 0);
    if (fd < 0) return 1;
    // How soon after being added does a keyboard take a key? Each round adds one, waits, presses Right or Left once,
    // and removes it; whether the focus moved shows in the shell's log after each "press" line.
    int round = 0;
    for (const auto wait : {100ms, 250ms, 500ms, 1000ms}) {
        const int added = add(fd, 1, 0xa, user);
        if (added < 0) break;
        std::this_thread::sleep_for(wait);
        const uint8_t key = round++ % 2 ? hid_left : hid_right;
        say("press %s %lld ms after adding", key == hid_right ? "right" : "left", static_cast<long long>(wait.count()));
        keys(fd, added, &key, 1);
        std::this_thread::sleep_for(120ms);
        keys(fd, added, nullptr, 0);
        std::this_thread::sleep_for(1200ms);
        remove(fd, added);
        std::this_thread::sleep_for(12s);
    }

    const int keyboard = add(fd, 1, 0xa, user);
    const int pointer = add(fd, 0, 0xb, user);
    if (keyboard >= 0) say("keyboard idle rc=%d", keys(fd, keyboard, nullptr, 0));
    if (pointer >= 0) say("mouse idle rc=%d", mouse(fd, pointer, 0, 0, 0));
    std::this_thread::sleep_for(8s);

    if (keyboard >= 0) {
        for (const uint8_t key : {hid_right, hid_right, hid_right, hid_left}) {
            const int down = keys(fd, keyboard, &key, 1);
            std::this_thread::sleep_for(150ms);
            const int up = keys(fd, keyboard, nullptr, 0);
            say("key %#x down=%d up=%d", key, down, up);
            std::this_thread::sleep_for(800ms);
        }
    }
    if (pointer >= 0) {
        int errors = 0;
        for (int i = 0; i < 30; i++) {
            errors += mouse(fd, pointer, 10, 0, 0) != 0;
            std::this_thread::sleep_for(16ms);
        }
        say("mouse moves=30 errors=%d", errors);
    }
    std::this_thread::sleep_for(2s);
    if (keyboard >= 0) remove(fd, keyboard);
    if (pointer >= 0) remove(fd, pointer);
    close(fd);
    say("done");
    if (report) std::fclose(report);
    return 0;
}
