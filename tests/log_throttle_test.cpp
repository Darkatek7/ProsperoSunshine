// SPDX-License-Identifier: GPL-3.0-only
#include "net/log_throttle.hpp"
#include <cassert>
#include <iostream>
#include <string>
using namespace prospero;
using namespace std::chrono_literals;

int main() {
    const LogThrottle::clock::time_point t0{};
    std::size_t held = 99;

    // The first line per key is due, repeats within the interval are not, and the next due line counts them.
    LogThrottle throttle(10min, 3);
    assert(throttle.due("a", t0, held) && held == 0);
    assert(!throttle.due("a", t0 + 5s, held) && held == 0);
    assert(!throttle.due("a", t0 + 9min, held));
    assert(throttle.due("b", t0 + 9min, held) && held == 0);
    assert(throttle.due("a", t0 + 10min, held) && held == 2);
    assert(!throttle.due("a", t0 + 15min, held));
    assert(throttle.due("a", t0 + 20min, held) && held == 1);
    assert(throttle.due("a", t0 + 40min, held) && held == 0);

    // At capacity a new key evicts the one seen longest ago ("b"), not the one logged longest ago.
    assert(throttle.due("c", t0 + 41min, held));
    assert(throttle.size() == 3);
    assert(!throttle.due("a", t0 + 42min, held));
    assert(!throttle.due("c", t0 + 42min, held));
    assert(throttle.due("d", t0 + 43min, held));
    assert(throttle.size() == 3);
    assert(!throttle.due("a", t0 + 43min, held));
    assert(!throttle.due("c", t0 + 43min, held));
    assert(throttle.due("b", t0 + 43min, held) && held == 0); // forgotten, so new again
    assert(throttle.size() == 3);

    // Many keys never grow it past its capacity.
    LogThrottle small(10min, 64);
    for (int i = 0; i < 1000; ++i) assert(small.due(std::to_string(i), t0 + std::chrono::seconds(i), held));
    assert(small.size() == 64);
    assert(!small.due("999", t0 + 1000s, held));
    assert(small.due("0", t0 + 1000s, held));

    // No capacity: everything is due.
    LogThrottle none(10min, 0);
    assert(none.due("a", t0, held) && none.due("a", t0, held) && none.size() == 0);

    std::cout << "PASS: log throttle (first line per key, then one per interval, bounded keys)\n";
}
