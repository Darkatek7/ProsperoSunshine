// SPDX-License-Identifier: GPL-3.0-only
#include "control/listener_watch.hpp"
#include <array>
#include <cassert>
#include <iostream>
using namespace prospero;

int main() {
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    assert(listener >= 0);
    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr = loopback_address(0);
    assert(bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof addr) == 0);
    assert(listen(listener, 4) == 0);
    socklen_t size = sizeof addr;
    assert(getsockname(listener, reinterpret_cast<sockaddr *>(&addr), &size) == 0);
    const uint16_t port = ntohs(addr.sin_port);

    assert(!port_refused(port));
    assert(!port_free(port));
    close(listener);
    assert(port_refused(port));
    assert(port_free(port));
    // The ports a check is true for, by name, for host_diag.log: here those nothing listens on.
    const std::array<ServicePort, 2> services{{{port, "HTTP"}, {static_cast<uint16_t>(port + 1), "control"}}};
    assert(ports_where(services, [](uint16_t) { return false; }).empty());
    assert(ports_where(services, [](uint16_t) { return true; }) ==
           "HTTP TCP " + std::to_string(port) + ", control TCP " + std::to_string(port + 1));
    assert(ports_where(services, [&](uint16_t p) { return p != port; }) == "control TCP " + std::to_string(port + 1));
    const int again = socket(AF_INET, SOCK_STREAM, 0);
    assert(again >= 0);
    setsockopt(again, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    addr = loopback_address(port);
    assert(bind(again, reinterpret_cast<sockaddr *>(&addr), sizeof addr) == 0 && listen(again, 4) == 0);
    const std::array<ServicePort, 1> http{{{port, "HTTP"}}};
    assert(ports_where(http, port_refused).empty());
    close(again);
    assert(ports_where(http, port_refused) == "HTTP TCP " + std::to_string(port));

    std::cout << "PASS: listener watch (refused only without a listener, free only without one, ports named)\n";
}
