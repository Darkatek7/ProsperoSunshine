// SPDX-License-Identifier: GPL-3.0-only
#include "net/send_retry.hpp"
#include <cassert>
#include <cerrno>
#include <iostream>
#include <vector>
using namespace prospero;

int main() {
    // Every packet leaves: no waiting.
    {
        std::vector<std::size_t> tried;
        int waits = 0;
        const auto r = send_with_retries(64, 100, [&](std::size_t i) { tried.push_back(i); return 0; }, [&] { ++waits; });
        assert(r.sent == 64 && r.dropped == 0 && r.waits == 0 && r.error == 0 && waits == 0);
        for (std::size_t i = 0; i < tried.size(); ++i) assert(tried[i] == i);
    }

    // A full buffer for a few steps: the packet waits and leaves, the batch goes on, each packet once.
    {
        std::vector<int> sent(10, 0);
        int busy = 3, waits = 0;
        const auto r = send_with_retries(
            10, 100,
            [&](std::size_t i) {
                if (i == 4 && busy > 0) {
                    --busy;
                    return ENOBUFS;
                }
                ++sent[i];
                return 0;
            },
            [&] { ++waits; });
        assert(r.sent == 10 && r.dropped == 0 && r.waits == 3 && waits == 3);
        for (const int n : sent) assert(n == 1);
    }

    // A buffer that stays full: at most max_waits steps for the whole batch (10 ms at 100 us), then each packet left gets
    // one try and is dropped. Before, every packet waited 10 ms, and Sunshine then sent the whole batch again.
    {
        int tries = 0, waits = 0;
        const auto r = send_with_retries(64, 100, [&](std::size_t) { ++tries; return EAGAIN; }, [&] { ++waits; });
        assert(r.sent == 0 && r.dropped == 64 && r.waits == 100 && waits == 100 && r.error == 0);
        assert(tries == 100 + 64);
    }
    // ... and once it drains again, the rest of the batch leaves without further waiting.
    {
        int waits = 0;
        const auto r = send_with_retries(
            20, 5, [&](std::size_t i) { return i < 3 ? EWOULDBLOCK : 0; }, [&] { ++waits; });
        assert(r.waits == 5 && r.sent == 17 && r.dropped == 3 && waits == 5);
    }

    // A hard error: the rest of the batch is dropped at once, without waiting, and the error is reported.
    {
        std::vector<std::size_t> tried;
        int waits = 0;
        const auto r = send_with_retries(
            64, 100,
            [&](std::size_t i) {
                tried.push_back(i);
                return i < 10 ? 0 : EHOSTUNREACH;
            },
            [&] { ++waits; });
        assert(r.sent == 10 && r.dropped == 54 && r.error == EHOSTUNREACH && r.waits == 0 && waits == 0);
        assert(tried.size() == 11);
    }

    // Nothing to send.
    {
        const auto r = send_with_retries(0, 100, [](std::size_t) { return EINVAL; }, [] {});
        assert(r.sent == 0 && r.dropped == 0 && r.error == 0);
    }

    assert(send_buffer_full(EAGAIN) && send_buffer_full(ENOBUFS) && !send_buffer_full(EHOSTUNREACH) && !send_buffer_full(0));

    std::cout << "send_retry_test: OK\n";
    return 0;
}
