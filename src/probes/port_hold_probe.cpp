// SPDX-License-Identifier: GPL-3.0-only
// Test for a stream start while another process holds a stream port (GitHub issue 24): binds UDP 47999, the control
// port, on every address for 60 s, the way a second program on the console would. A stream started meanwhile has to end
// with "Couldn't bind Control server" in sunshine.log while the host keeps running.
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
extern "C" {
#include <ps5/klog.h>
}

int main() {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(47999);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (fd < 0 || bind(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof addr) != 0) {
        klog_printf("ProsperoSunshine: port_hold_probe cannot bind UDP 47999: %s\n", std::strerror(errno));
        return 1;
    }
    klog_printf("ProsperoSunshine: port_hold_probe holds UDP 47999 for 60 s\n");
    sleep(60);
    close(fd);
    klog_printf("ProsperoSunshine: port_hold_probe released UDP 47999\n");
    return 0;
}
