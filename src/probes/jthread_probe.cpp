// SPDX-License-Identifier: GPL-3.0-only
// M0: Sunshine uses std::jthread and stop tokens; prove they build, link and run.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>
extern "C" {
#include <ps5/klog.h>
}

int main() {
    using namespace std::chrono_literals;
    std::atomic<int> iterations{0};
    std::atomic<bool> callback_ran{false};
    std::atomic<bool> wait_interrupted{false};
    std::mutex mutex;
    std::condition_variable_any cv;
    const auto start = std::chrono::steady_clock::now();
    {
        // Polling loop, as in Sunshine's stream threads.
        std::jthread poller([&](std::stop_token token) {
            std::stop_callback callback(token, [&] { callback_ran.store(true); });
            while (!token.stop_requested()) {
                iterations.fetch_add(1);
                std::this_thread::sleep_for(1ms);
            }
        });
        // Blocking wait that only a stop request can end.
        std::jthread waiter([&](std::stop_token token) {
            std::unique_lock lock(mutex);
            const bool ready = cv.wait(lock, token, [] { return false; });
            wait_interrupted.store(!ready && token.stop_requested());
        });
        std::this_thread::sleep_for(50ms);
        // Destructors request stop and join, waiter first.
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    const bool ok = iterations.load() > 0 && callback_ran.load() && wait_interrupted.load();
    klog_printf("ProsperoSunshine: jthread result=%s iterations=%d callback=%d wait_interrupted=%d elapsed_ms=%lld\n",
                ok ? "PASS" : "FAIL", iterations.load(), callback_ran.load(), wait_interrupted.load(),
                static_cast<long long>(elapsed));
    return ok ? 0 : 1;
}
