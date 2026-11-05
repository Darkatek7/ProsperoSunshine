// SPDX-License-Identifier: GPL-3.0-only
// What the firmware probe masks before writing a report line (probe_scrub.hpp), and what it must leave alone.
#include "probe_scrub.hpp"
#include <cassert>
#include <cstdio>

int main() {
    using probe::scrub;
    // Masked.
    assert(scrub("console 192.168.1.20 port 9021") == "console <ip> port 9021");
    assert(scrub("10.0.0.1:47989") == "<ip>:47989");
    assert(scrub("from 100.64.0.5.") == "from <ip>.");
    assert(scrub("mac 00:1A:2b:3C:4d:5E end") == "mac <mac> end");
    assert(scrub("mac a0-b1-c2-d3-e4-f5") == "mac <mac>");
    assert(scrub("two 1.2.3.4 and 5.6.7.8") == "two <ip> and <ip>");
    // Left alone: what the report really holds.
    for (const char *kept : {"firmware 0x10200006 (10.20)", "TSC 1.59627 ticks/ns", "host version=0.1.7 commit=ddd51c45",
                             "59.94 fps", "app 01.000.008", "record 2c8020000000000 28000000", "QP at 0xc4/0xc8 20/24",
                             "256.1.1.1", "1.2.3", "1.2.3.4.5", "12:34:56", "deadbeef:00:11:22:33:44"})
        assert(scrub(kept) == kept);
    std::puts("PASS: probe report scrub (IPv4 and MAC masked, versions, offsets and hex kept)");
    return 0;
}
