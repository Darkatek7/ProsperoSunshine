// SPDX-License-Identifier: GPL-3.0-only
// Sunshine's platf:: layer for the PS5, what is neither audio nor input: the host's name and addresses, sending on the
// stream sockets, the timer and thread names. Audio is in audio_ps5.cpp, input in input_ps5.cpp and the input_*_ps5.cpp
// beside it.
#include "net/send_retry.hpp"
#include "src/logging.h"
#include "src/platform/common.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <pthread_np.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <boost/asio/ip/address.hpp>

using namespace std::literals;

extern "C" int sceNetGetMacAddress(uint8_t address[6], int flags);  // libSceNet, linked by default
extern "C" {
#include "platform/ps5/diag.h"
}

namespace platf {

std::unique_ptr<deinit_t> init() { return std::make_unique<deinit_t>(); }

namespace publish {
// Upstream calls this from main.cpp, which the PS5 host replaces; mDNS runs from main_ps5.cpp
// (src/platform/ps5/mdns_responder.cpp, ROADMAP P9.2).
std::unique_ptr<deinit_t> start() { return nullptr; }
}

std::filesystem::path appdata() { return "/data/prosperosunshine"; }

std::string get_host_name() {
    char name[256] = {};
    if (gethostname(name, sizeof name - 1) == 0 && name[0]) return name;
    return "PS5";
}

std::string get_mac_address(const std::string_view &address) {
    ifaddrs *list = nullptr;
    if (getifaddrs(&list) != 0) return "00:00:00:00:00:00";
    std::string interface_name, mac = "00:00:00:00:00:00";
    for (auto *a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || (a->ifa_addr->sa_family != AF_INET && a->ifa_addr->sa_family != AF_INET6)) continue;
        if (from_sockaddr(a->ifa_addr) == address) interface_name = a->ifa_name;
    }
    for (auto *a = list; a && !interface_name.empty(); a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_LINK || interface_name != a->ifa_name) continue;
        const auto *dl = reinterpret_cast<const sockaddr_dl *>(a->ifa_addr);
        if (dl->sdl_alen != 6) continue;
        const auto *b = reinterpret_cast<const unsigned char *>(LLADDR(dl));
        char buf[18];
        std::snprintf(buf, sizeof buf, "%02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
        mac = buf;
    }
    freeifaddrs(list);
    // Without a match above, ask the network library.
    if (mac == "00:00:00:00:00:00") {
        uint8_t b[6] = {};
        if (sceNetGetMacAddress(b, 0) == 0) {
            char buf[18];
            std::snprintf(buf, sizeof buf, "%02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
            mac = buf;
        }
    }
    return mac;
}

std::string from_sockaddr(const sockaddr *const addr) { return from_sockaddr_ex(addr).second; }

std::pair<std::uint16_t, std::string> from_sockaddr_ex(const sockaddr *const addr) {
    char text[INET6_ADDRSTRLEN] = {};
    std::uint16_t port = 0;
    if (addr->sa_family == AF_INET6) {
        const auto *in6 = reinterpret_cast<const sockaddr_in6 *>(addr);
        inet_ntop(AF_INET6, &in6->sin6_addr, text, sizeof text);
        port = ntohs(in6->sin6_port);
    } else if (addr->sa_family == AF_INET) {
        const auto *in = reinterpret_cast<const sockaddr_in *>(addr);
        inet_ntop(AF_INET, &in->sin_addr, text, sizeof text);
        port = ntohs(in->sin_port);
    }
    return {port, text};
}

void set_thread_name(std::string_view name) {
    const std::string copy(name.substr(0, 15));
    pthread_set_name_np(pthread_self(), copy.c_str());
}

void adjust_thread_priority(thread_priority_e) {}
void streaming_will_start() {}
void streaming_will_stop() {}
void open_url(const std::string &) {}
// Patch 0024 calls this right before binding a stream port; the debug build writes it to host_diag.log first.
void trace_call(const std::string &what) { ps_diag_trace("%s\n", what.c_str()); }

std::unique_ptr<deinit_t> enable_socket_qos(uintptr_t, boost::asio::ip::address &, uint16_t, qos_data_type_e, bool) {
    return nullptr;
}

namespace {
socklen_t to_sockaddr(const boost::asio::ip::address &address, uint16_t port, sockaddr_storage &out) {
    std::memset(&out, 0, sizeof out);
    if (address.is_v6()) {
        auto *in6 = reinterpret_cast<sockaddr_in6 *>(&out);
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(port);
        const auto bytes = address.to_v6().to_bytes();
        std::memcpy(&in6->sin6_addr, bytes.data(), bytes.size());
        return sizeof(sockaddr_in6);
    }
    auto *in = reinterpret_cast<sockaddr_in *>(&out);
    in->sin_family = AF_INET;
    in->sin_port = htons(port);
    const auto bytes = address.to_v4().to_bytes();
    std::memcpy(&in->sin_addr, bytes.data(), bytes.size());
    return sizeof(sockaddr_in);
}

// One try: 0 when the datagram left, else the errno.
int send_once(int fd, const sockaddr_storage &to, socklen_t to_len, const char *header, size_t header_size,
              const char *payload, size_t payload_size) {
    iovec iov[2];
    int count = 0;
    if (header_size) iov[count++] = {const_cast<char *>(header), header_size};
    iov[count++] = {const_cast<char *>(payload), payload_size};
    msghdr msg{};
    msg.msg_name = const_cast<sockaddr_storage *>(&to);
    msg.msg_namelen = to_len;
    msg.msg_iov = iov;
    msg.msg_iovlen = count;
    return sendmsg(fd, &msg, 0) >= 0 ? 0 : errno;
}

// A full send buffer on a nonblocking socket: give the NIC a moment instead of dropping, 100 steps of 100 us per batch
// (net/send_retry.hpp).
constexpr int send_max_waits = 100;
void send_wait() { std::this_thread::sleep_for(100us); }

// What was dropped, in one line at most every 10 s.
void note_dropped(const prospero::SendBatchResult &result) {
    static std::mutex mutex;
    static std::chrono::steady_clock::time_point logged{};
    static std::size_t dropped = 0, batches = 0;
    static int error = 0;
    std::lock_guard lock(mutex);
    dropped += result.dropped;
    ++batches;
    if (result.error) error = result.error;
    const auto now = std::chrono::steady_clock::now();
    if (now - logged < 10s) return;
    logged = now;
    BOOST_LOG(warning) << "PS5 network: "sv << dropped << " packets in "sv << batches << " sends dropped"sv
                       << (error ? ", last error: "s + std::strerror(error) : ", the send buffer stayed full"s);
    dropped = batches = 0;
    error = 0;
}
}

// Always true: Sunshine reads false as "no batched send here" and would send the whole batch again packet by packet, a
// second copy of what already left, each with its own retries, on the one thread that sends every session's video.
bool send_batch(const batched_send_info_t &info) {
    sockaddr_storage to;
    const socklen_t to_len = to_sockaddr(info.target_address, info.target_port, to);
    const auto result = prospero::send_with_retries(
        info.block_count, send_max_waits,
        [&](size_t i) {
            const auto block = info.buffer_for_payload_offset((info.block_offset + i) * info.payload_size);
            const size_t size = std::min(info.payload_size, block.size);
            return send_once(static_cast<int>(info.native_socket), to, to_len,
                             info.headers ? info.headers + (info.block_offset + i) * info.header_size : nullptr,
                             info.headers ? info.header_size : 0, block.buffer, size);
        },
        send_wait);
    if (result.dropped) note_dropped(result);
    return true;
}

bool send(send_info_t &info) {
    sockaddr_storage to;
    const socklen_t to_len = to_sockaddr(info.target_address, info.target_port, to);
    const auto result = prospero::send_with_retries(
        1, send_max_waits,
        [&](size_t) {
            return send_once(static_cast<int>(info.native_socket), to, to_len, info.header, info.header_size,
                             info.payload, info.payload_size);
        },
        send_wait);
    if (result.dropped) note_dropped(result);
    return result.sent == 1;
}

namespace {
class sleep_timer final : public high_precision_timer {
public:
    void sleep_for(const std::chrono::nanoseconds &duration) override { std::this_thread::sleep_for(duration); }
    explicit operator bool() override { return true; }
};
}

std::unique_ptr<high_precision_timer> create_high_precision_timer() { return std::make_unique<sleep_timer>(); }

}
