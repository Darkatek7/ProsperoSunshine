// SPDX-License-Identifier: GPL-3.0-only
// What the host costs the console while a game runs next to it: from two snapshots of every thread's counters (CPU time,
// voluntary context switches = wakeups, minor page faults), the share of a core each kind of thread used and how often it
// woke up in between. Only the arithmetic and the log line are here, so they are tested without a console
// (tests/thread_load_test.cpp); reading the counters is the host's job (a sysctl over the process's threads is the candidate,
// or a clock per thread, which the console has to confirm first).
//
// Threads are grouped by name ("session::video" is one line however many streams run), CPU is in percent of ONE core
// like top shows it, and a thread that ended between two snapshots has no counters left to read, so its last interval is
// missing: a short-lived thread is under-counted, the long-lived ones this is for are not.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace prospero::load {

struct ThreadCounters {
    uint64_t id = 0;        // the system's thread id; a new thread may reuse one
    std::string name;       // empty when the thread has none
    uint64_t cpu_us = 0;    // user and system time so far
    uint64_t wakeups = 0;   // voluntary context switches so far: each sleep or wait that ended
    uint64_t faults = 0;    // minor page faults so far
};

struct Snapshot {
    std::vector<ThreadCounters> threads;
    long rss_kb = -1;       // -1 when unknown
};

struct Group {
    std::string name;
    int threads = 0;
    double cpu_percent = 0;
    double wakeups_per_s = 0;
    double faults_per_s = 0;
};

struct Interval {
    double seconds = 0;
    int threads = 0;
    long rss_kb = -1;
    double cpu_percent = 0;     // of one core, all threads together
    double wakeups_per_s = 0;
    double faults_per_s = 0;
    std::vector<Group> groups;  // most CPU first, then most wakeups
};

class ThreadLoad {
public:
    using Clock = std::chrono::steady_clock;

    // The interval since the previous call; the first call only remembers, and so does one that comes less than 10 ms
    // after the last (the rates would be noise).
    std::optional<Interval> update(const Snapshot &now, Clock::time_point at) {
        if (!have_) {
            remember(now, at);
            return std::nullopt;
        }
        const double seconds = std::chrono::duration<double>(at - at_).count();
        if (seconds < 0.01) return std::nullopt;
        Interval result;
        result.seconds = seconds;
        result.threads = static_cast<int>(now.threads.size());
        result.rss_kb = now.rss_kb;
        std::map<std::string, Group> groups;
        for (const ThreadCounters &thread : now.threads) {
            uint64_t cpu = thread.cpu_us, wakeups = thread.wakeups, faults = thread.faults;
            // A thread seen before is measured against its last values; one that is new, or whose id was reused by a new
            // thread (counters went back), has run since it started, so everything it shows counts.
            if (const auto before = last_.find(thread.id);
                before != last_.end() && thread.cpu_us >= before->second.cpu_us && thread.wakeups >= before->second.wakeups &&
                thread.faults >= before->second.faults) {
                cpu -= before->second.cpu_us;
                wakeups -= before->second.wakeups;
                faults -= before->second.faults;
            }
            Group &group = groups[thread.name.empty() ? "unnamed" : thread.name];
            group.name = thread.name.empty() ? "unnamed" : thread.name;
            group.threads++;
            group.cpu_percent += static_cast<double>(cpu) / (seconds * 1e6) * 100.0;
            group.wakeups_per_s += static_cast<double>(wakeups) / seconds;
            group.faults_per_s += static_cast<double>(faults) / seconds;
        }
        for (auto &[name, group] : groups) {
            result.cpu_percent += group.cpu_percent;
            result.wakeups_per_s += group.wakeups_per_s;
            result.faults_per_s += group.faults_per_s;
            result.groups.push_back(std::move(group));
        }
        std::sort(result.groups.begin(), result.groups.end(), [](const Group &a, const Group &b) {
            return a.cpu_percent != b.cpu_percent ? a.cpu_percent > b.cpu_percent : a.wakeups_per_s > b.wakeups_per_s;
        });
        remember(now, at);
        return result;
    }

private:
    void remember(const Snapshot &now, Clock::time_point at) {
        last_.clear();
        for (const ThreadCounters &thread : now.threads) last_[thread.id] = thread;
        at_ = at;
        have_ = true;
    }

    std::unordered_map<uint64_t, ThreadCounters> last_;
    Clock::time_point at_{};
    bool have_ = false;
};

// "Host load: secs=5.0 cpu=14.2% threads=24 rss_kb=23456 wakeups/s=3120 faults/s=12 top: session::video 2.1%/950w x2, ..."
// The groups that used under 0.1 % of a core and woke under 5 times a second are left out; `top` caps the rest.
inline std::string load_line(const Interval &interval, size_t top = 8) {
    char head[192];
    std::snprintf(head, sizeof head, "Host load: secs=%.1f cpu=%.1f%% threads=%d rss_kb=%ld wakeups/s=%.0f faults/s=%.0f", interval.seconds,
                  interval.cpu_percent, interval.threads, interval.rss_kb, interval.wakeups_per_s, interval.faults_per_s);
    std::string line = head;
    size_t shown = 0;
    for (const Group &group : interval.groups) {
        if (shown == top) break;
        if (group.cpu_percent < 0.1 && group.wakeups_per_s < 5) continue;
        char item[160];
        std::snprintf(item, sizeof item, "%s %s %.1f%%/%.0fw", shown ? "," : " top:", group.name.c_str(), group.cpu_percent,
                      group.wakeups_per_s);
        line += item;
        if (group.threads > 1) line += " x" + std::to_string(group.threads);
        shown++;
    }
    return line;
}

}  // namespace prospero::load
