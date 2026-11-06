// SPDX-License-Identifier: GPL-3.0-only
#include "platform/ps5/mdns_responder.hpp"
#include "net/mdns.hpp"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <optional>
#include <string>
extern "C" {
#include <ps5/klog.h>
#include "platform/ps5/diag.h"
}

namespace prospero::mdns {
namespace {

using namespace std::chrono_literals;
constexpr uint16_t mdns_port = 5353;

bool usable(in_addr a) {
    const auto v = ntohl(a.s_addr);
    return v != 0 && (v >> 24) != 127 && (v >> 16) != 0xa9fe;  // not any, loopback or link-local
}

// LAN address: the one the default route would use (a connected UDP socket sends nothing), else the
// first usable interface address.
std::optional<in_addr> lan_address() {
    if (const int s = socket(AF_INET, SOCK_DGRAM, 0); s >= 0) {
        sockaddr_in peer{};
        peer.sin_family = AF_INET;
        peer.sin_port = htons(9);
        inet_pton(AF_INET, "192.0.2.1", &peer.sin_addr);  // TEST-NET-1, never contacted
        sockaddr_in local{};
        socklen_t length = sizeof local;
        const bool ok = connect(s, reinterpret_cast<sockaddr *>(&peer), sizeof peer) == 0 &&
                        getsockname(s, reinterpret_cast<sockaddr *>(&local), &length) == 0 && usable(local.sin_addr);
        close(s);
        if (ok) return local.sin_addr;
    }
    std::optional<in_addr> found;
    ifaddrs *list = nullptr;
    if (getifaddrs(&list) != 0) return found;
    for (auto *a = list; a && !found; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        const auto address = reinterpret_cast<const sockaddr_in *>(a->ifa_addr)->sin_addr;
        if (usable(address)) found = address;
    }
    freeifaddrs(list);
    return found;
}

class Socket {
public:
    ~Socket() {
        if (fd_ >= 0) close(fd_);
    }
    bool open(in_addr lan) {
        fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0) return fail("socket");
        const int one = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
        sockaddr_in bind_to{};
        bind_to.sin_family = AF_INET;
        bind_to.sin_port = htons(mdns_port);
        ps_diag_trace("bind the mDNS port UDP %u\n", static_cast<unsigned>(mdns_port));
        if (bind(fd_, reinterpret_cast<sockaddr *>(&bind_to), sizeof bind_to) != 0) return fail("bind 5353");
        ip_mreq group{};
        inet_pton(AF_INET, "224.0.0.251", &group.imr_multiaddr);
        group.imr_interface = lan;
        if (setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &group, sizeof group) != 0) return fail("join group");
        if (setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_IF, &lan, sizeof lan) != 0) return fail("multicast interface");
        const int ttl = 255;
        setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
        return true;
    }
    void send(const std::vector<uint8_t> &packet) const {
        if (packet.empty()) return;
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(mdns_port);
        inet_pton(AF_INET, "224.0.0.251", &to.sin_addr);
        if (sendto(fd_, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr *>(&to), sizeof to) < 0)
            klog_printf("ProsperoSunshine: mdns send failed errno=%d\n", errno);
    }
    // Waits up to `timeout` for a packet; returns its size or 0.
    ssize_t receive(uint8_t *buffer, size_t size, std::chrono::milliseconds timeout) const {
        pollfd p{fd_, POLLIN, 0};
        if (poll(&p, 1, static_cast<int>(timeout.count())) <= 0 || !(p.revents & POLLIN)) return 0;
        return recv(fd_, buffer, size, 0);
    }

    const std::string &error() const { return error_; }

private:
    bool fail(const char *what) {
        klog_printf("ProsperoSunshine: mdns %s failed errno=%d, discovery disabled\n", what, errno);
        error_ = std::string(what) + " errno=" + std::to_string(errno);
        return false;
    }
    int fd_ = -1;
    std::string error_;
};

}

void run_responder(std::stop_token stop, const std::string &name, uint16_t port) {
    const auto lan = lan_address();
    if (!lan) {
        klog_printf("ProsperoSunshine: mdns found no LAN address, discovery disabled\n");
        return;
    }
    Socket socket;
    if (!socket.open(*lan)) return;

    Service service;
    service.instance = name;
    service.host = host_label(name);
    service.port = port;
    std::memcpy(service.ipv4.data(), &lan->s_addr, 4);
    char text[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &*lan, text, sizeof text);
    klog_printf("ProsperoSunshine: mdns advertising \"%s\" host=%s.local %s:%u\n", name.c_str(), service.host.c_str(),
                text, port);

    const auto announcement = build_announcement(service, false);
    auto next_announce = std::chrono::steady_clock::now();
    int announced = 0;
    std::array<uint8_t, 1500> buffer;
    while (!stop.stop_requested()) {
        if (announced < 3 && std::chrono::steady_clock::now() >= next_announce) {
            socket.send(announcement);
            ++announced;
            next_announce += 1s;
        }
        const auto size = socket.receive(buffer.data(), buffer.size(), 200ms);
        if (size <= 0) continue;
        const auto questions = parse_questions({buffer.data(), static_cast<size_t>(size)});
        socket.send(build_response(service, questions));
    }
    socket.send(build_announcement(service, true));
    klog_printf("ProsperoSunshine: mdns stopped, goodbye sent\n");
}

std::string check_socket() {
    const auto lan = lan_address();
    if (!lan) return "no LAN address";
    Socket socket;
    return socket.open(*lan) ? std::string() : socket.error();
}

}
