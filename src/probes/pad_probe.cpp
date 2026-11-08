// SPDX-License-Identifier: GPL-3.0-only
// R5: native virtual pad without Mono. Adds a controller, feeds neutral states at 60 Hz for a while
// (long enough to look for the "Who's using this controller?" dialog), then removes it again.
// It presses no buttons, so it cannot navigate or confirm anything on the console.
#include "platform/ps5/pad.h"
#include <chrono>
#include <thread>
extern "C" {
#include <ps5/klog.h>
}

int main() {
    using namespace std::chrono_literals;
    const uint32_t user = ps_pad_default_user();
    ps_pad *pad = nullptr;
    const int open_rc = ps_pad_open(user, &pad);
    klog_printf("ProsperoSunshine: pad user=%#x open_rc=%d\n", user, open_rc);
    if (open_rc) return 1;
    int send_errors = 0, sends = 0;
    const auto end = std::chrono::steady_clock::now() + 20s;
    while (std::chrono::steady_clock::now() < end) {
        send_errors += ps_pad_neutral(pad) != 0;
        ++sends;
        std::this_thread::sleep_for(16ms);
    }
    const int close_rc = ps_pad_close(pad);
    const bool ok = send_errors == 0 && close_rc == 0;
    klog_printf("ProsperoSunshine: pad result=%s sends=%d send_errors=%d close_rc=%d\n", ok ? "PASS" : "FAIL", sends,
                send_errors, close_rc);
    return ok ? 0 : 1;
}
