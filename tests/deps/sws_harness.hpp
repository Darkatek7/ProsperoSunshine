// SPDX-License-Identifier: GPL-3.0-only
// Shared by tests/deps/sws_server_test.cpp and the fuzz targets tests/fuzz/sws_*_fuzz.cpp: a Simple-Web-Server HTTP server
// on a loopback port the system picks, configured the way the host's servers are, and a raw socket client that sends it
// whatever bytes the test wants. Built against the patched sources scripts/dep_headers.sh prepares, never the PS5 libraries.
#pragma once
#include "server_http.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <future>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace sws_test {

using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;
using namespace std::chrono_literals;

// Which of the host's servers a test imitates.
enum class Profile {
    // Both GameStream servers in Sunshine's nvhttp.cpp (its patch 0032): head and body limit 64 KiB, no request body at
    // all, a no-op on_error. (The HTTPS one adds TLS with a client certificate in front of the same reader.)
    gamestream,
    // The web page's server (src/web/web_server.cpp): limit 64 KiB, bodies accepted, no on_error. Over TLS in the host.
    page,
    // Nothing set: the library's defaults, as an unpatched Sunshine would run it.
    bare,
};

inline const char *name(Profile p) {
    switch (p) {
        case Profile::gamestream: return "gamestream";
        case Profile::page: return "page";
        case Profile::bare: return "bare";
    }
    return "?";
}

// A server running on its own thread. ended() turns true when start() returned or threw, which no request may cause.
class Instance {
public:
    // The host's timeouts are 5 s for a head (the library's default) and 300 s for a body (the page: 10 s and 60 s); tests
    // pass short ones so that "waits for the rest of the request" takes seconds, not minutes.
    explicit Instance(Profile profile, long request_timeout_s = 1, long content_timeout_s = 3) : profile_(profile) {
        auto &c = server.config;
        c.address = "127.0.0.1";
        c.port = 0;  // the system picks
        c.timeout_request = request_timeout_s;
        c.timeout_content = content_timeout_s;
        if (profile != Profile::bare) c.max_request_streambuf_size = 64 * 1024;
        if (profile == Profile::gamestream) {
            c.accept_content = false;
            server.on_error = [](auto, const SimpleWeb::error_code &) {};
        }
        server.resource["^/serverinfo$"]["GET"] = [this](auto res, auto) {
            ++handled;
            res->write("ok");
        };
        // Answers with what it was sent.
        auto echo = [this](auto res, auto req) {
            ++handled;
            const std::string body = req->content.string();
            {
                std::lock_guard lock(mutex);
                last_content = body;
            }
            res->write(body);
        };
        server.resource["^/echo$"]["GET"] = echo;
        server.resource["^/echo$"]["POST"] = echo;
        // A long answer, for a client that goes away in the middle of reading it.
        server.resource["^/big$"]["GET"] = [this](auto res, auto) {
            ++handled;
            res->write(std::string(4 << 20, 'x'));
        };
        server.resource["^/throw$"]["GET"] = [this](auto, auto) {
            ++handled;
            throw std::runtime_error("handler failed");
        };
        server.default_resource["GET"] = [this](auto res, auto) {
            ++handled;
            res->write(SimpleWeb::StatusCode::client_error_not_found, "none");
        };
        server.default_resource["POST"] = server.default_resource["GET"];
        // Shared: the thread uses it again after start() has returned, long after this constructor has.
        auto bound = std::make_shared<std::promise<unsigned short>>();
        auto port = bound->get_future();
        thread_ = std::thread([this, bound] {
            try {
                server.start([bound](unsigned short p) { bound->set_value(p); });
            } catch (const std::exception &e) {
                std::lock_guard lock(mutex);
                failure_ = e.what();
            } catch (...) {
                std::lock_guard lock(mutex);
                failure_ = "not a std::exception";
            }
            ended_ = true;
            // Only matters if start() ended before the port was known (a bind failure); otherwise this one throws.
            try {
                bound->set_value(0);
            } catch (const std::future_error &) {
            }
        });
        port_ = port.get();
    }
    Instance(const Instance &) = delete;
    Instance &operator=(const Instance &) = delete;
    ~Instance() {
        server.stop();
        thread_.join();
    }

    uint16_t port() const { return port_; }
    Profile profile() const { return profile_; }
    // True once start() has left, by throwing or returning; the reason, if it threw.
    bool ended() const { return ended_; }
    std::string failure() const {
        std::lock_guard lock(mutex);
        return failure_;
    }
    std::string content() const {
        std::lock_guard lock(mutex);
        return last_content;
    }

    HttpServer server;
    std::atomic<int> handled{0};  // requests that got as far as a resource

private:
    Profile profile_;
    std::thread thread_;
    uint16_t port_ = 0;
    std::atomic<bool> ended_{false};
    mutable std::mutex mutex;
    std::string failure_, last_content;
};

// One TCP connection to 127.0.0.1:port with blocking calls and timeouts of its own.
class Client {
public:
    struct Reply {
        std::string data;
        bool eof = false;       // the server closed the connection (FIN)
        bool reset = false;     // ... with a reset
        bool timed_out = false; // still open and silent when the time ran out
    };

    explicit Client(uint16_t port) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return;
        // Without it a second small send waits for the server's delayed ACK (40 ms), which makes a fuzz run crawl.
        const int one = 1;
        ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0) {
            connect_errno_ = errno;
            ::close(fd_);
            fd_ = -1;
        }
    }
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;
    ~Client() {
        if (fd_ >= 0) ::close(fd_);
    }
    bool connected() const { return fd_ >= 0; }
    // Why connect() failed (0 if it didn't): a client machine that ran out of ports or descriptors is not the server's fault.
    int connect_errno() const { return connect_errno_; }

    // False when the server has closed the connection by now (the rest is dropped, as a client would).
    bool send(std::string_view bytes) {
        size_t done = 0;
        while (done < bytes.size() && fd_ >= 0) {
            const ssize_t n = ::send(fd_, bytes.data() + done, bytes.size() - done, MSG_NOSIGNAL);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return false;
            done += static_cast<size_t>(n);
        }
        return fd_ >= 0;
    }
    // The client's side of the connection ends (FIN); the server may still answer.
    void finish() {
        if (fd_ >= 0) ::shutdown(fd_, SHUT_WR);
    }
    // The connection is torn down with a reset, as a client that crashed or a firewall leaves it.
    void reset() {
        if (fd_ < 0) return;
        linger l{1, 0};
        ::setsockopt(fd_, SOL_SOCKET, SO_LINGER, &l, sizeof l);
        ::close(fd_);
        fd_ = -1;
    }
    void close() {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }

    // Reads until the server closes the connection, the time is up, or `limit` bytes have come. With quiet_ms above 0 it also
    // stops after that long without a byte, so "nothing more is coming" doesn't cost the server's whole timeout.
    Reply read(std::chrono::milliseconds total, std::chrono::milliseconds quiet = 0ms, size_t limit = 16u << 20) {
        Reply r;
        if (fd_ < 0) {
            r.eof = true;
            return r;
        }
        const auto end = std::chrono::steady_clock::now() + total;
        char buffer[16384];
        while (true) {
            auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now());
            if (left <= 0ms) {
                r.timed_out = true;
                return r;
            }
            if (quiet > 0ms && quiet < left) left = quiet;
            pollfd p{fd_, POLLIN, 0};
            const int ready = ::poll(&p, 1, static_cast<int>(left.count()));
            if (ready < 0 && errno == EINTR) continue;
            if (ready == 0) {
                r.timed_out = true;
                return r;
            }
            const ssize_t n = ::recv(fd_, buffer, sizeof buffer, 0);
            if (n < 0 && errno == EINTR) continue;
            if (n == 0) {
                r.eof = true;
                return r;
            }
            if (n < 0) {
                r.reset = true;
                return r;
            }
            r.data.append(buffer, static_cast<size_t>(n));
            if (r.data.size() >= limit) return r;
        }
    }

private:
    int fd_ = -1;
    int connect_errno_ = 0;
};

// What a monitoring request looks like; any of the servers answers it with "ok".
inline constexpr std::string_view kPlainRequest = "GET /serverinfo HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";

// A plain request on a connection of its own: true if it is answered with the 200 and its body. The property every test
// ends on.
inline bool answered(uint16_t port, std::chrono::milliseconds wait = 3000ms) {
    Client c(port);
    if (!c.connected() || !c.send(kPlainRequest)) return false;
    const auto r = c.read(wait);
    return r.data.rfind("HTTP/1.1 200 OK", 0) == 0 && r.data.size() >= 2 && r.data.compare(r.data.size() - 2, 2, "ok") == 0;
}

}  // namespace sws_test
