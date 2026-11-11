// SPDX-License-Identifier: GPL-3.0-only
// Host only: the counters diag/thread_load.hpp works on, read from the console. One sysctl lists every thread of this process
// (KERN_PROC_INC_THREAD) with its name, CPU time and counters. Firmware 13.00 refuses that sysctl to a payload (kernel log
// "kern.proc.pid_td is not approved", measured 2026-11-11): the snapshot then has the whole process as one entry, from
// getrusage, so the line still says what the host costs, without which thread. False only when that fails too; the caller
// logs it once. Its own file: <sys/user.h> declares `struct proc`, which Sunshine's `namespace proc` cannot share a
// translation unit with.
#include "platform/ps5/thread_counters.hpp"
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>
#include <cstring>

namespace prospero::load {

namespace {
// The process as a whole: its CPU time, voluntary context switches and page faults, and its peak resident size (the
// current one isn't in rusage; a leak still shows, as the peak then keeps rising).
bool read_process(Snapshot &out) {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return false;
    ThreadCounters all;
    all.id = 0;
    all.name = "all threads (rss is the peak)";
    all.cpu_us = static_cast<uint64_t>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1'000'000 +
                 static_cast<uint64_t>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec);
    all.wakeups = static_cast<uint64_t>(usage.ru_nvcsw);
    all.faults = static_cast<uint64_t>(usage.ru_minflt);
    out.threads.clear();
    out.threads.push_back(std::move(all));
    out.rss_kb = usage.ru_maxrss;  // in kilobytes on FreeBSD
    return true;
}
}  // namespace

bool read_snapshot(Snapshot &out) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID | KERN_PROC_INC_THREAD, static_cast<int>(getpid())};
    size_t size = 0;
    if (sysctl(mib, 4, nullptr, &size, nullptr, 0) != 0 || size < sizeof(kinfo_proc)) return read_process(out);
    size += size / 4 + sizeof(kinfo_proc);  // threads may start between the two calls
    std::vector<char> buffer(size);
    if (sysctl(mib, 4, buffer.data(), &size, nullptr, 0) != 0 || size < sizeof(kinfo_proc)) return read_process(out);
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
