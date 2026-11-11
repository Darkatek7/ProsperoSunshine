// SPDX-License-Identifier: GPL-3.0-only
// The per-thread load figures (src/diag/thread_load.hpp): rates from two snapshots, grouping by name, threads that come and
// go, ids that are reused, and the log line.
#include "diag/thread_load.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace prospero::load;
using namespace std::chrono_literals;

namespace {
bool near(double a, double b) { return std::fabs(a - b) < 0.01; }

const Group *find(const Interval &interval, const char *name) {
    for (const Group &group : interval.groups)
        if (group.name == name) return &group;
    return nullptr;
}
}  // namespace

int main() {
    const auto t0 = ThreadLoad::Clock::now();
    ThreadLoad load;

    // The first snapshot only remembers.
    Snapshot first;
    first.rss_kb = 20000;
    first.threads = {{1, "session::video", 1'000'000, 100, 10}, {2, "session::video", 500'000, 50, 5},
                     {3, "stream::videoBr", 2'000'000, 400, 20}, {4, "", 0, 0, 0}, {5, "pad_output", 100'000, 1000, 0}};
    assert(!load.update(first, t0));

    // Five seconds later: thread 1 used 0.5 s and woke 4750 times, thread 2 used 0.25 s, the broadcast thread 0.3 s and
    // 600 times, the pad thread 0.05 s and 500 times; thread 6 is new, thread 4 stayed idle.
    Snapshot second;
    second.rss_kb = 21000;
    second.threads = {{1, "session::video", 1'500'000, 4850, 60}, {2, "session::video", 750'000, 4800, 30},
                      {3, "stream::videoBr", 2'300'000, 1000, 20}, {4, "", 0, 0, 0}, {5, "pad_output", 150'000, 1500, 0},
                      {6, "audio::encode", 200'000, 1000, 100}};
    const auto interval = load.update(second, t0 + 5s);
    assert(interval);
    assert(near(interval->seconds, 5.0));
    assert(interval->threads == 6);
    assert(interval->rss_kb == 21000);

    // Both session::video threads make one group: 0.75 s of CPU in 5 s is 15 % of a core, 9500 wakeups are 1900 a second.
    const Group *video = find(*interval, "session::video");
    assert(video && video->threads == 2);
    assert(near(video->cpu_percent, 15.0));
    assert(near(video->wakeups_per_s, 1900.0));
    assert(near(video->faults_per_s, 15.0));  // (50 + 25) / 5
    const Group *broadcast = find(*interval, "stream::videoBr");
    assert(broadcast && near(broadcast->cpu_percent, 6.0) && near(broadcast->wakeups_per_s, 120.0));
    // A thread that started inside the interval counts whole: 0.2 s in 5 s is 4 %.
    const Group *encode = find(*interval, "audio::encode");
    assert(encode && near(encode->cpu_percent, 4.0) && near(encode->wakeups_per_s, 200.0) && near(encode->faults_per_s, 20.0));
    // A thread without a name is "unnamed", and idle.
    const Group *unnamed = find(*interval, "unnamed");
    assert(unnamed && near(unnamed->cpu_percent, 0.0));
    // The totals add the groups up; the groups are ordered by CPU.
    assert(near(interval->cpu_percent, 15.0 + 6.0 + 4.0 + 1.0));
    assert(near(interval->wakeups_per_s, 1900.0 + 120.0 + 200.0 + 100.0));
    assert(interval->groups.front().name == "session::video");
    assert(interval->groups.back().name == "unnamed");

    // A thread that ended is simply not there any more; a reused id with smaller counters counts as a new thread.
    Snapshot third;
    third.rss_kb = 21000;
    third.threads = {{1, "session::audio", 100'000, 10, 1}, {3, "stream::videoBr", 2'600'000, 1100, 20}};
    const auto later = load.update(third, t0 + 10s);
    assert(later && later->threads == 2);
    const Group *audio = find(*later, "session::audio");
    assert(audio && near(audio->cpu_percent, 2.0) && near(audio->wakeups_per_s, 2.0));
    assert(!find(*later, "session::video"));
    assert(near(find(*later, "stream::videoBr")->cpu_percent, 6.0));

    // Two snapshots in the same instant give nothing, and do not reset the baseline.
    assert(!load.update(third, t0 + 10s));
    assert(load.update(third, t0 + 11s));

    // The line: the quiet groups are left out, the rest follow in order, a group of several threads says how many.
    const std::string line = load_line(*interval);
    assert(line.starts_with("Host load: secs=5.0 cpu=26.0% threads=6 rss_kb=21000 wakeups/s=2320 faults/s=35 top: "));
    assert(line.find("session::video 15.0%/1900w x2") != std::string::npos);
    assert(line.find("stream::videoBr 6.0%/120w") != std::string::npos);
    assert(line.find("unnamed") == std::string::npos);
    assert(line.find("pad_output 1.0%/100w") != std::string::npos);
    // `top` caps the list.
    const std::string two = load_line(*interval, 2);
    assert(two.find("stream::videoBr") != std::string::npos && two.find("audio::encode") == std::string::npos);
    // Nothing worth listing: the head only.
    Interval quiet;
    quiet.seconds = 5;
    quiet.groups = {{"idle", 1, 0.0, 1.0, 0.0}};
    assert(load_line(quiet).find("top:") == std::string::npos);

    std::puts("thread_load_test ok");
    return 0;
}
