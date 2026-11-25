// SPDX-License-Identifier: GPL-3.0-only
#include "input/text_typist.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
using namespace prospero;
using namespace std::chrono_literals;

namespace {
struct Step {
    long long at_ms;
    uint8_t usage;
    bool shift;
};

// Ticks every 10 ms for `ms`, as the host does, and notes every change of the key that is down.
std::vector<Step> run(TextTypist &typist, TextTypist::clock::time_point &now, long long ms, long long &clock_ms) {
    std::vector<Step> steps;
    for (long long t = 0; t < ms; t += 10, now += 10ms, clock_ms += 10)
        if (typist.tick(now)) steps.push_back({clock_ms, typist.usage(), typist.shift()});
    return steps;
}
}  // namespace

int main() {
    TextTypist::clock::time_point now{1000s};
    long long clock_ms = 0;

    // Nothing queued: nothing happens.
    TextTypist typist;
    assert(typist.idle() && !typist.tick(now) && typist.usage() == 0 && !typist.shift());

    // "aB": a down, up, B down with Shift, up; each state lasts at least the hold.
    auto pushed = typist.push("aB");
    assert(pushed.queued == 2 && pushed.without_key == 0 && pushed.over == 0 && !typist.idle());
    auto steps = run(typist, now, 500, clock_ms);
    assert(steps.size() == 4);
    assert(steps[0].usage == 0x04 && !steps[0].shift);
    assert(steps[1].usage == 0 && !steps[1].shift);
    assert(steps[2].usage == 0x05 && steps[2].shift);
    assert(steps[3].usage == 0);
    for (std::size_t i = 1; i < steps.size(); ++i) assert(steps[i].at_ms - steps[i - 1].at_ms >= TextTypist::hold.count());
    assert(typist.idle());

    // The same key twice comes up in between, or the console would see one long press.
    typist.push("aa");
    steps = run(typist, now, 500, clock_ms);
    assert(steps.size() == 4 && steps[0].usage == 0x04 && steps[1].usage == 0 && steps[2].usage == 0x04 && steps[3].usage == 0);

    // A hundred characters take about 6 to 8 s at a 10 ms tick and all arrive in order.
    std::string text;
    for (int i = 0; i < 10; ++i) text += "abcdefghij";
    assert(typist.push(text).queued == 100);
    steps = run(typist, now, 10000, clock_ms);
    assert(steps.size() == 200 && typist.idle());
    for (int i = 0; i < 100; ++i) {
        assert(steps[2 * i].usage == 0x04 + i % 10);
        assert(steps[2 * i + 1].usage == 0);
    }
    const long long took = steps.back().at_ms - steps.front().at_ms;
    assert(took >= 199 * TextTypist::hold.count() && took <= 8000);

    // Characters without a key are counted and left out; a UTF-8 sequence counts once.
    pushed = typist.push("a\xC3\xA9"
                         "b\xE2\x82\xAC\x01");
    assert(pushed.queued == 2 && pushed.without_key == 3 && pushed.over == 0);
    steps = run(typist, now, 500, clock_ms);
    assert(steps.size() == 4 && steps[0].usage == 0x04 && steps[2].usage == 0x05);

    // A full queue drops the rest and says how much.
    pushed = typist.push(std::string(TextTypist::max_pending + 7, 'x'));
    assert(pushed.queued == static_cast<int>(TextTypist::max_pending) && pushed.over == 7);
    assert(typist.pending() == TextTypist::max_pending);
    assert(typist.push("y").over == 1);

    // clear() forgets the text and the key that is down.
    assert(typist.tick(now) && typist.usage() == 0x1B);
    typist.clear();
    assert(typist.idle() && typist.usage() == 0 && !typist.tick(now));

    // not_before: the first key waits for a keyboard that was just added, and an earlier time doesn't shorten the wait.
    typist.push("a");
    typist.not_before(now + 1000ms);
    typist.not_before(now + 200ms);
    const long long asked = clock_ms;
    steps = run(typist, now, 1500, clock_ms);
    assert(steps.size() == 2 && steps[0].at_ms - asked >= 1000 && steps[0].at_ms - asked < 1020);

    // A text pushed while another is typed goes behind it.
    typist.push("ab");
    assert(typist.tick(now) && typist.usage() == 0x04);
    typist.push("c");
    now += 10ms;
    clock_ms += 10;
    steps = run(typist, now, 500, clock_ms);
    assert(steps.size() == 5 && steps[0].usage == 0 && steps[1].usage == 0x05 && steps[3].usage == 0x06 && typist.idle());

    std::cout << "PASS text_typist_test\n";
    return 0;
}
