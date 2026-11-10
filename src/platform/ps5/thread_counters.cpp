// SPDX-License-Identifier: GPL-3.0-only
// Host only: the counters diag/thread_load.hpp works on, read from the console. One sysctl lists every thread of this process
// (KERN_PROC_INC_THREAD) with its name, CPU time and counters. False when the console refuses it or has nothing to say; the
// caller then logs that once. Which of the fields the console really keeps per thread is for the first run
// on it to show (the report's step list), so a counter that stays 0 for every thread means it doesn't. Its own file: <sys/user.h>
// declares `struct proc`, which Sunshine's `namespace proc` cannot share a translation unit with.
#include "platform/ps5/thread_counters.hpp"
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>
#include <cstring>

namespace prospero::load {

bool read_snapshot(Snapshot &out) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID | KERN_PROC_INC_THREAD, static_cast<int>(getpid())};
    size_t size = 0;
    if (sysctl(mib, 4, nullptr, &size, nullptr, 0) != 0 || size < sizeof(kinfo_proc)) return false;
    size += size / 4 + sizeof(kinfo_proc);  // threads may start between the two calls
    std::vector<char> buffer(size);
    if (sysctl(mib, 4, buffer.data(), &size, nullptr, 0) != 0 || size < sizeof(kinfo_proc)) return false;
    out.threads.clear();
    for (size_t at = 0; at + sizeof(kinfo_proc) <= size; at += sizeof(kinfo_proc)) {
        kinfo_proc info;
        std::memcpy(&info, buffer.data() + at, sizeof info);
        ThreadCounters thread;
        thread.id = static_cast<uint64_t>(info.ki_tid);
        thread.name = info.ki_tdname;
        thread.cpu_us = info.ki_runtime;
        thread.wakeups = static_cast<uint64_t>(info.ki_rusage.ru_nvcsw);
        thread.faults = static_cast<uint64_t>(info.ki_rusage.ru_minflt);
        out.threads.push_back(std::move(thread));
    }
    kinfo_proc first;  // every entry carries the process's resident size, in pages
    std::memcpy(&first, buffer.data(), sizeof first);
    out.rss_kb = static_cast<long>(first.ki_rssize) * (getpagesize() / 1024);
    return true;
}

}  // namespace prospero::load
