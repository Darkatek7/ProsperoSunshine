// SPDX-License-Identifier: GPL-3.0-only
// What the firmware probe masks before writing a report line (probe_scrub.hpp, which uses the masker of every posted file,
// app/src/psapp/mask.hpp; tests/mask_test.cpp covers the masker itself), and what it must leave alone.
#include "probe_scrub.hpp"
#include <cassert>
#include <cstdio>

int main() {
    using probe::scrub;
    // Masked: the tag says what it was, and one address has one tag for the whole run of the probe.
    assert(scrub("console 192.168.1.20 port 9021") == "console <lan-1> port 9021");
    assert(scrub("10.0.0.1:47989") == "<lan-2>:47989");
    assert(scrub("from 100.64.0.5.") == "from <tailscale-1>.");
    assert(scrub("mac 00:1A:2b:3C:4d:5E end") == "mac <mac-1> end");
    assert(scrub("mac a0-b1-c2-d3-e4-f5") == "mac <mac-2>");
    assert(scrub("two 203.0.113.4 and 198.51.100.8") == "two <public-1> and <public-2>");
    assert(scrub("again 192.168.1.20") == "again <lan-1>");
    assert(scrub("client ::ffff:192.168.1.5 asked") == "client <lan-3> asked");
    assert(scrub("[fd7a:115c:a1e0::1234]:47989") == "[<tailscale6-1>]:47989");
    assert(scrub("fe80::1%em0") == "<link6-1>%em0");
    assert(scrub("at 2001:db8:0:0:0:0:2:1.") == "at <public6-1>.");
    // Left alone: what the report really holds, and what names nobody (loopback, "any").
    for (const char *kept : {"firmware 0x10200006 (10.20)", "TSC 1.59627 ticks/ns", "host version=0.1.7 commit=ddd51c45",
                             "59.94 fps", "app 01.000.008", "record 2c8020000000000 28000000", "QP at 0xc4/0xc8 20/24",
                             "256.1.1.1", "1.2.3", "1.2.3.4.5", "12:34:56", "deadbeef:00:11:22:33:44",
                             "[2026-11-06 12:00:00.123]: Info: x", "std::string", "boost::asio", "payload :: v=0",
                             "a:b:c", "12345::6", "127.0.0.1", "::1", "0.0.0.0"})
        assert(scrub(kept) == kept);
    std::puts("PASS: probe report scrub (IPv4, IPv6 and MAC masked with their kind, versions, offsets, times and hex kept)");
    return 0;
}
