// SPDX-License-Identifier: GPL-3.0-only
// How the firmware probe tells from the host's STATUS line that a stream runs (host_control.hpp): it must not start then.
// No answer while the host listens counts as busy. And send_elf, which the host's PROBE uses, with no ELF to send.
#include "host_control.hpp"
#include <cassert>
#include <cstdio>
#include <string>

int main() {
    using host_control::streaming_in;
    assert(streaming_in("OK running=1 app=Desktop pending=0 address=10.0.0.5 client=Moonlight PC\n"));
    assert(streaming_in("OK running=0 app= pending=0 address=fe80::1 client="));
    assert(!streaming_in("OK running=0 app= pending=0 address= client="));
    assert(!streaming_in("OK running=1 app=Desktop pending=0 address= client=\n"));
    assert(!streaming_in("OK running=0 app= pending=0 address=\r\n"));
    assert(!streaming_in(""));  // no answer: no host, so no stream from it
    assert(!streaming_in("ERR unknown command"));
    // No answer counts as busy while the host listens (it may be streaming), and as free when no host runs.
    using host_control::busy_in;
    assert(busy_in("", true) && !busy_in("", false));
    assert(busy_in("OK running=1 app=Desktop pending=0 address=10.0.0.5 client=Moonlight PC\n", true));
    assert(!busy_in("OK running=0 app= pending=0 address= client=", true));
    std::string detail;
    assert(host_control::send_elf("/nonexistent/firmware_probe.elf", detail) == 0 &&
           detail == "/nonexistent/firmware_probe.elf not found");
    std::puts("PASS: host STATUS read as streaming only with a client address, silence as busy while it listens; send_elf without an ELF");
    return 0;
}
