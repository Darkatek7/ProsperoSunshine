// SPDX-License-Identifier: GPL-3.0-only
// The debug build's remote log, off by default: only with `remote_log = <IPv4 address>[:<port>]` in sunshine.conf, an
// address the user typed in (nothing is looked up, no name is accepted). Each line that goes to sunshine.log or
// host_diag.log, after log_filter.hpp, also goes out as one UDP syslog datagram (port 514 unless given), so a small
// server at home keeps the last lines when the console hangs. Sending never blocks; a line the socket doesn't take is
// dropped. Portable, tested in tests/remote_log_test.cpp.
#pragma once
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace prospero::logs {

struct RemoteAddress {
    in_addr address{};
    uint16_t port = 514;
};

// "192.168.1.10" or "192.168.1.10:5514", spaces around allowed. No names, and not 0.0.0.0, a broadcast or a multicast
// address: one host the user chose.
inline std::optional<RemoteAddress> parse_remote(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    const size_t colon = text.find(':');
    const std::string host(text.substr(0, colon));
    RemoteAddress out;
    if (host.empty() || inet_pton(AF_INET, host.c_str(), &out.address) != 1) return std::nullopt;
    const uint32_t value = ntohl(out.address.s_addr);
    if (value == 0 || value == 0xffffffffu || (value >> 28) == 0xe) return std::nullopt;
    if (colon != std::string_view::npos) {
        const std::string_view port = text.substr(colon + 1);
        if (port.empty() || port.size() > 5) return std::nullopt;
        uint32_t number = 0;
        for (const char c : port) {
            if (c < '0' || c > '9') return std::nullopt;
            number = number * 10 + static_cast<uint32_t>(c - '0');
        }
        if (number == 0 || number > 65535) return std::nullopt;
        out.port = static_cast<uint16_t>(number);
    }
    return out;
}

// Sunshine's levels (0 verbose, 1 debug, 2 info, 3 warning, 4 error, 5 fatal) as a syslog priority, facility user (1).
inline int syslog_priority(int level) {
    static constexpr int severity[] = {7, 7, 6, 4, 3, 2};
    return 8 + severity[std::clamp(level, 0, 5)];
}

// One datagram, "<PRI>tag: text" (RFC 3164 without a time stamp: the server adds the time it got it), at most 1024 bytes.
inline std::string syslog_datagram(int level, std::string_view tag, std::string_view text) {
    std::string out = "<" + std::to_string(syslog_priority(level)) + ">" + std::string(tag) + ": " + std::string(text);
    if (out.size() > 1024) out.resize(1024);
    return out;
}

class RemoteLog {
public:
    RemoteLog() = default;
    RemoteLog(const RemoteLog &) = delete;
    RemoteLog &operator=(const RemoteLog &) = delete;
    ~RemoteLog() {
        if (fd_ >= 0) ::close(fd_);
    }

    // Before the first send; returns false when no socket could be set up.
    bool open(const RemoteAddress &to) {
        const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) return false;
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        sockaddr_in peer{};
        peer.sin_family = AF_INET;
        peer.sin_port = htons(to.port);
        peer.sin_addr = to.address;
        if (::connect(fd, reinterpret_cast<const sockaddr *>(&peer), sizeof peer) != 0) {
            ::close(fd);
            return false;
        }
        fd_ = fd;
        return true;
    }

    bool active() const { return fd_ >= 0; }

    // Each line of `text` as its own datagram.
    void send(int level, std::string_view tag, std::string_view text) const {
        if (fd_ < 0) return;
        while (!text.empty()) {
            const size_t end = std::min(text.find('\n'), text.size());
            if (end > 0) {
                const std::string datagram = syslog_datagram(level, tag, text.substr(0, end));
                (void)!::send(fd_, datagram.data(), datagram.size(), 0);
            }
            text.remove_prefix(std::min(end + 1, text.size()));
        }
    }

private:
    int fd_ = -1;
};

}  // namespace prospero::logs
