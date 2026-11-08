// SPDX-License-Identifier: GPL-3.0-only
// Test for the host's recovery from rest mode without rest mode: asks it over the local control socket to start its
// services anew, the way main_ps5.cpp does when the ports are lost. The socket only answers on the console itself.
#include "control/listener_watch.hpp"
#include <cstring>
extern "C" {
#include <ps5/klog.h>
}

int main() {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    const sockaddr_in addr = prospero::loopback_address(47995);
    if (fd < 0 || connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof addr) != 0) {
        klog_printf("ProsperoSunshine: relisten_probe cannot reach the control socket\n");
        return 1;
    }
    constexpr char request[] = "RELISTEN\n";
    send(fd, request, std::strlen(request), 0);
    char answer[64] = {};
    recv(fd, answer, sizeof answer - 1, 0);
    klog_printf("ProsperoSunshine: relisten_probe answer: %s", answer);
    close(fd);
    return 0;
}
