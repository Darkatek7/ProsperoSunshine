// SPDX-License-Identifier: GPL-3.0-only
// P9.2: runs only the mDNS responder for 90 s under a fixed test name, so discovery can be checked
// while the real host keeps running. Look for "PS5-mdns-test" from a PC or phone.
#include "platform/ps5/mdns_responder.hpp"
#include <chrono>
#include <thread>
extern "C" {
#include <ps5/klog.h>
}

int main() {
    using namespace std::chrono_literals;
    klog_printf("ProsperoSunshine: mdns_probe start (90 s)\n");
    {
        std::jthread responder([](std::stop_token stop) { prospero::mdns::run_responder(stop, "PS5-mdns-test", 47989); });
        std::this_thread::sleep_for(90s);
    }  // jthread requests stop and joins; the goodbye goes out here
    klog_printf("ProsperoSunshine: mdns_probe done\n");
    return 0;
}
