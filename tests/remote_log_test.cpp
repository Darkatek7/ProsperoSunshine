// SPDX-License-Identifier: GPL-3.0-only
// The debug build's remote log (src/log/remote_log.hpp): which addresses it takes, and what one line looks like on the
// wire, sent to a socket on this machine.
#include "log/remote_log.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <poll.h>

using namespace prospero::logs;

int main() {
    const auto parsed = parse_remote(" 192.168.1.10:5514 ");
    assert(parsed && parsed->port == 5514 && ntohl(parsed->address.s_addr) == 0xc0a8010au);
    assert(parse_remote("10.0.0.2") && parse_remote("10.0.0.2")->port == 514);
    for (const char *refused : {"", "server.local", "192.168.1", "192.168.1.10:", "192.168.1.10:0", "192.168.1.10:65536",
                                "192.168.1.10:51a", "0.0.0.0", "255.255.255.255", "224.0.0.251", "fe80::1"})
        assert(!parse_remote(refused));

    assert(syslog_priority(0) == 15 && syslog_priority(1) == 15 && syslog_priority(2) == 14);
    assert(syslog_priority(3) == 12 && syslog_priority(4) == 11 && syslog_priority(5) == 10 && syslog_priority(9) == 10);
    assert(syslog_datagram(3, "prospero_sunshine", "Ping Timeout") == "<12>prospero_sunshine: Ping Timeout");
    assert(syslog_datagram(2, "t", std::string(2000, 'x')).size() == 1024);

    // Through a socket: each line of a record is one datagram, empty lines send nothing.
    const int receiver = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in here{};
    here.sin_family = AF_INET;
    here.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(receiver, reinterpret_cast<sockaddr *>(&here), sizeof here) == 0);
    socklen_t size = sizeof here;
    assert(getsockname(receiver, reinterpret_cast<sockaddr *>(&here), &size) == 0);
    RemoteLog log;
    assert(!log.active());
    log.send(2, "t", "nothing goes out before open");
    assert(log.open({here.sin_addr, ntohs(here.sin_port)}) && log.active());
    log.send(4, "prospero_sunshine", "first\n\nsecond\n");
    char buffer[2048];
    for (const char *expected : {"<11>prospero_sunshine: first", "<11>prospero_sunshine: second"}) {
        pollfd p{receiver, POLLIN, 0};
        assert(poll(&p, 1, 1000) == 1);
        const ssize_t n = recv(receiver, buffer, sizeof buffer, 0);
        assert(n == static_cast<ssize_t>(strlen(expected)) && memcmp(buffer, expected, static_cast<size_t>(n)) == 0);
    }
    pollfd p{receiver, POLLIN, 0};
    assert(poll(&p, 1, 100) == 0);
    close(receiver);
    std::puts("PASS: remote log (an IPv4 address the user typed, one syslog datagram per line)");
    return 0;
}
