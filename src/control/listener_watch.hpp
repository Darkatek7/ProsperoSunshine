// SPDX-License-Identifier: GPL-3.0-only
// Rest mode takes the host's listening sockets away while the process runs on (STATUS.md). These checks let
// main_ps5.cpp notice that and wait until it can listen again.
#pragma once
#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace prospero {

inline sockaddr_in loopback_address(uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return addr;
}

// True only for a definite refusal. A timeout or a network that is not back yet says nothing about the port.
inline bool port_refused(uint16_t port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    const sockaddr_in addr = loopback_address(port);
    int error = 0;
    if (connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof addr) != 0) {
        error = errno;
        if (error == EINPROGRESS) {
            pollfd pfd{fd, POLLOUT, 0};
            socklen_t size = sizeof error;
            error = 0;
            if (poll(&pfd, 1, 2000) > 0) getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size);
        }
    }
    close(fd);
    return error == ECONNREFUSED;
}

// A port the host's services listen on, with the name host_diag.log gives it.
struct ServicePort {
    uint16_t port;
    const char *name;
};

// "HTTP TCP 47989, control TCP 47995" for the ports `check` is true for, empty when it is true for none. A bind that fails
// is otherwise only in sunshine.log, and the app only sees that the host didn't come up (main_ps5.cpp).
template <typename Ports, typename Check>
std::string ports_where(const Ports &ports, Check check) {
    std::string out;
    for (const ServicePort &service : ports) {
        if (!check(service.port)) continue;
        if (!out.empty()) out += ", ";
        out += std::string(service.name) + " TCP " + std::to_string(service.port);
    }
    return out;
}

// Whether a server could bind this port now, with the options the host's servers use.
inline bool port_free(uint16_t port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr = loopback_address(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    const bool free = bind(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof addr) == 0 && listen(fd, 1) == 0;
    close(fd);
    return free;
}

}
