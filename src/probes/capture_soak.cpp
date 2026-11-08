// SPDX-License-Identifier: GPL-3.0-only
// M1 acceptance run: five start/stop cycles, then a long capture. A PC may connect to TCP port
// 9998 to receive the raw Annex-B stream (tools/diag_receiver.py); without a client frames are
// only counted. Descriptor words of selected frames are logged to work out their meaning.
#include "capture/capture_session.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
extern "C" {
#include <ps5/klog.h>
}

using namespace std::chrono_literals;
using clock_type = std::chrono::steady_clock;
using prospero::CaptureSession;
using prospero::CaptureStatus;

namespace {
constexpr int cycles = 5;
constexpr auto cycle_length = 5s;
constexpr auto soak_length = 10min;
constexpr auto report_every = 30s;
constexpr uint16_t diag_port = 9998;

long long us(clock_type::duration d) { return std::chrono::duration_cast<std::chrono::microseconds>(d).count(); }

int64_t monotonic_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

long max_rss_kb() {
    rusage usage{};
    return getrusage(RUSAGE_SELF, &usage) == 0 ? usage.ru_maxrss : -1;
}

void log_descriptor(const char *what, uint64_t index, const CaptureSession::Descriptor &d) {
    uint64_t w[14];
    std::memcpy(w, d.data(), sizeof w);
    klog_printf("ProsperoSunshine: soak desc %s frame=%" PRIu64 " mono_ns=%" PRId64
                " w1=%#" PRIx64 " w2=%#" PRIx64 " w3=%#" PRIx64 " w4=%#" PRIx64 " w5=%#" PRIx64 " w6=%#" PRIx64
                " w7=%#" PRIx64 " w8=%#" PRIx64 " w9=%#" PRIx64 " w10=%#" PRIx64 " w11=%#" PRIx64 " w12=%#" PRIx64
                " w13=%#" PRIx64 "\n",
                what, index, monotonic_ns(), w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11],
                w[12], w[13]);
}

int listen_socket() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(diag_port);
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) || listen(fd, 1)) {
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    return fd;
}

bool send_all(int fd, const uint8_t *data, size_t size) {
    while (size) {
        const ssize_t n = send(fd, data, size, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        data += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

struct Stats {
    uint64_t frames = 0, bytes = 0, idrs = 0, timeouts = 0;
    long long gap_min = INT64_MAX, gap_max = 0, idr_gap_min = INT64_MAX, idr_gap_max = 0;
    uint64_t last_idr = 0;
    clock_type::time_point previous{};
    void add(const prospero::EncodedFrame &f) {
        if (frames) {
            const long long gap = us(f.read_time - previous);
            gap_min = std::min(gap_min, gap);
            gap_max = std::max(gap_max, gap);
        }
        if (f.idr) {
            if (idrs) {
                const long long d = static_cast<long long>(f.index - last_idr);
                idr_gap_min = std::min(idr_gap_min, d);
                idr_gap_max = std::max(idr_gap_max, d);
            }
            last_idr = f.index;
            ++idrs;
        }
        previous = f.read_time;
        ++frames;
        bytes += f.bytes.size();
    }
};

// One capture run; returns false on any capture failure.
bool run(const char *name, clock_type::duration length, int listener, int &client, bool log_descriptors) {
    CaptureSession session(prospero::make_ps5_capture_backend());
    const auto opened = clock_type::now();
    if (const int rc = session.start()) {
        klog_printf("ProsperoSunshine: soak %s start failed rc=%d\n", name, rc);
        return false;
    }
    klog_printf("ProsperoSunshine: soak %s started open_us=%lld headers=%zu\n", name, us(clock_type::now() - opened),
                session.parameter_sets().size());
    Stats total, window;
    const auto start = clock_type::now();
    auto next_report = start + report_every;
    prospero::EncodedFrame frame;
    CaptureSession::Descriptor desc{};
    bool ok = true;
    while (clock_type::now() - start < length) {
        if (client < 0 && listener >= 0) {
            client = accept(listener, nullptr, nullptr);
            if (client >= 0) {
                // BSD accept() inherits O_NONBLOCK from the listener; the stream wants blocking sends.
                fcntl(client, F_SETFL, fcntl(client, F_GETFL) & ~O_NONBLOCK);
                int one = 1;
                setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                klog_printf("ProsperoSunshine: soak diag client connected\n");
            }
        }
        const auto status = session.read(frame, 500ms, {}, &desc);
        if (status == CaptureStatus::timeout) {
            ++total.timeouts;
            continue;
        }
        if (status != CaptureStatus::frame) {
            klog_printf("ProsperoSunshine: soak %s read status=%d after frames=%" PRIu64 "\n", name,
                        static_cast<int>(status), total.frames);
            ok = false;
            break;
        }
        if (log_descriptors && (total.frames < 3 || (frame.idr && total.idrs < 3) || total.frames % 600 == 0))
            log_descriptor(frame.idr ? "idr" : "p", frame.index, desc);
        total.add(frame);
        window.add(frame);
        if (client >= 0 && !send_all(client, frame.bytes.data(), frame.bytes.size())) {
            klog_printf("ProsperoSunshine: soak diag client gone\n");
            close(client);
            client = -1;
        }
        if (frame.read_time >= next_report) {
            const double seconds = std::chrono::duration<double>(report_every).count();
            klog_printf("ProsperoSunshine: soak %s t=%llds fps=%.1f mbit=%.2f gap_us=%lld..%lld idrs=%" PRIu64
                        " max_rss_kb=%ld\n",
                        name, static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(
                                  frame.read_time - start).count()),
                        window.frames / seconds, window.bytes * 8 / seconds / 1e6, window.gap_min, window.gap_max,
                        window.idrs, max_rss_kb());
            window = Stats{};
            next_report += report_every;
        }
    }
    const auto elapsed = clock_type::now() - start;
    const int close_rc = session.stop();
    const double seconds = std::chrono::duration<double>(elapsed).count();
    klog_printf("ProsperoSunshine: soak %s done ok=%d close_rc=%d frames=%" PRIu64 " fps=%.2f mbit=%.2f"
                " gap_us=%lld..%lld idrs=%" PRIu64 " idr_every=%lld..%lld timeouts=%" PRIu64 " max_rss_kb=%ld\n",
                name, ok, close_rc, total.frames, total.frames / seconds, total.bytes * 8 / seconds / 1e6,
                total.frames > 1 ? total.gap_min : 0, total.gap_max, total.idrs,
                total.idrs > 1 ? total.idr_gap_min : 0, total.idr_gap_max, total.timeouts, max_rss_kb());
    return ok && close_rc == 0 && total.frames > 0;
}
}

int main() {
    const int listener = listen_socket();
    int client = -1;
    klog_printf("ProsperoSunshine: soak begin listener=%d port=%u cycles=%d soak_s=%lld\n", listener >= 0, diag_port,
                cycles, static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(soak_length).count()));
    int cycles_ok = 0;
    for (int i = 0; i < cycles; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, "cycle%d", i + 1);
        cycles_ok += run(name, cycle_length, -1, client, i == 0);
    }
    const bool soak_ok = run("long", soak_length, listener, client, true);
    if (client >= 0) close(client);
    if (listener >= 0) close(listener);
    const bool ok = cycles_ok == cycles && soak_ok;
    klog_printf("ProsperoSunshine: soak result=%s cycles_ok=%d/%d long=%d\n", ok ? "PASS" : "FAIL", cycles_ok, cycles,
                soak_ok);
    return ok ? 0 : 1;
}
