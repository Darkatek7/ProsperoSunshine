// SPDX-License-Identifier: GPL-3.0-only
// For the capture probes: is the host running in another process? It may hold the capture now or open it as soon as a
// client connects, and two processes on one ResourceArbitrator profile get in each other's way. The probes refuse to run
// then: stop the host first (the app's "Stop host", or QUIT on the control socket).
#pragma once
#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace probe {

// Whether something accepts TCP connections on 127.0.0.1:port within `wait_ms`. Connects and closes, sends nothing.
inline bool local_port_open(uint16_t port, int wait_ms = 300) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool open = false;
    if (connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof address) == 0) {
        open = true;
    } else if (errno == EINPROGRESS) {
        pollfd waiting{fd, POLLOUT, 0};
        if (poll(&waiting, 1, wait_ms) == 1) {
            int error = 0;
            socklen_t size = sizeof error;
            open = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0;
        }
    }
    close(fd);
    return open;
}

// The host's HTTP port (47989) or its control socket (47995) answers.
inline bool host_running() { return local_port_open(47989) || local_port_open(47995); }

}  // namespace probe
