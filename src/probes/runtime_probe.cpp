// SPDX-License-Identifier: GPL-3.0-only
// M0: exercise the native runtime before importing the Sunshine dependency graph.
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
extern "C" {
#include <ps5/kernel.h>
#include <ps5/klog.h>
}

int main() {
    klog_printf("ProsperoSunshine: runtime probe firmware=%#x cplusplus=%ld\n",
                kernel_get_fw_version(), static_cast<long>(__cplusplus));
    std::atomic<int> value{0};
    const auto start = std::chrono::steady_clock::now();
    std::thread worker([&] {
        std::vector<int> values{1, 2, 3};
        value.store(values[0] + values[1] + values[2]);
    });
    worker.join();
    int udp = socket(AF_INET, SOCK_DGRAM, 0);
    int tcp = socket(AF_INET, SOCK_STREAM, 0);
    const bool sockets = udp >= 0 && tcp >= 0;
    if (udp >= 0) close(udp);
    if (tcp >= 0) close(tcp);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    const bool ok = value.load() == 6 && sockets && elapsed >= 0;
    klog_printf("ProsperoSunshine: runtime result=%s threads=%d sockets=%d elapsed_us=%lld\n",
                ok ? "PASS" : "FAIL", value.load(), sockets, static_cast<long long>(elapsed));
    return ok ? 0 : 1;
}
