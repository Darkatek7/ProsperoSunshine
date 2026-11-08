// SPDX-License-Identifier: GPL-3.0-only
// Restarts the host without the app. Sent to the ELF loader (port 9021), it asks a running host to quit on its control
// socket, then starts /data/prosperosunshine/prospero_sunshine.elf, or the ELF named in
// /data/prosperosunshine/host_restart.next (a test build; "none" only stops). The outcome goes to
// /data/prosperosunshine/host_restart.log and to klog.
#include "host_control.hpp"
#include <cstdio>
#include <ctime>
extern "C" {
#include <ps5/klog.h>
}

int main() {
    FILE *log = fopen("/data/prosperosunshine/host_restart.log", "w");
    auto line = [&](const std::string &text) {
        klog_printf("ProsperoSunshine: host_restart %s\n", text.c_str());
        if (log) {
            fprintf(log, "%s\n", text.c_str());
            fflush(log);
        }
    };
    std::string detail;
    const bool stopped = host_control::stop(detail);
    line("stop: " + detail);
    int rc = stopped ? 0 : 1;
    const std::string next = host_control::next_elf();
    if (!stopped) {
        line("start: skipped, the old host still runs");
    } else if (next.empty()) {
        line("start: none asked");
    } else {
        rc = host_control::start(next, detail) ? 0 : 2;
        line("start: " + detail);
    }
    line(rc == 0 ? "done" : "failed");
    if (log) fclose(log);
    return rc;
}
