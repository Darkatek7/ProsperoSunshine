// SPDX-License-Identifier: GPL-3.0-only
#include "platform/ps5/capture.h"
#include "video/annex_b.hpp"
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
extern "C" {
#include <ps5/klog.h>
}

static bool write_all(int fd, const uint8_t *data, size_t size) {
    while (size) {
        ssize_t n = write(fd, data, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        data += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

int main() {
    ps_capture *capture = nullptr;
    int rc = ps_capture_open(&capture);
    if (rc) { klog_printf("ProsperoSunshine: capture open failed=%d\n", rc); return 1; }
    // An in-process second open must not touch the live session.
    ps_capture *second = nullptr;
    if (ps_capture_open(&second) != PS_CAPTURE_BUSY) {
        ps_capture_close(second);
        ps_capture_close(capture);
        return 1;
    }
    const char *path = "/data/prosperosunshine-capture.h264.part";
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    bool ok = fd >= 0;
    std::vector<uint8_t> buffer(4 * 1024 * 1024);
    if (ok) {
        rc = ps_capture_headers(capture, buffer.data(), buffer.size());
        ok = rc > 0 && write_all(fd, buffer.data(), static_cast<size_t>(rc));
    }
    const auto start = std::chrono::steady_clock::now();
    size_t frames = 0, bytes = 0, idrs = 0;
    auto previous = start;
    int64_t min_us = INT64_MAX, max_us = 0;
    while (ok && std::chrono::steady_clock::now() - start < std::chrono::seconds(10)) {
        uint8_t descriptor[0x70];
        rc = ps_capture_read(capture, buffer.data(), buffer.size(), descriptor);
        if (rc < 0) { ok = false; break; }
        if (!rc) { usleep(1000); continue; }
        const auto now = std::chrono::steady_clock::now();
        if (frames) {
            const auto interval = std::chrono::duration_cast<std::chrono::microseconds>(now - previous).count();
            if (interval < min_us) min_us = interval;
            if (interval > max_us) max_us = interval;
        }
        previous = now;
        frames++;
        bytes += static_cast<size_t>(rc);
        idrs += prospero::has_idr(std::span(buffer.data(), static_cast<size_t>(rc)));
        ok = write_all(fd, buffer.data(), static_cast<size_t>(rc));
    }
    if (fd >= 0 && close(fd)) ok = false;
    if (ps_capture_close(capture)) ok = false;
    if (ok && frames) {
        ok = rename(path, "/data/prosperosunshine-capture.h264") == 0;
    } else {
        unlink(path);
        ok = false;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    klog_printf("ProsperoSunshine: capture result=%s frames=%lu bytes=%lu idrs=%lu duration_ms=%lld read_gap_us=%lld..%lld\n",
                ok ? "PASS" : "FAIL", frames, bytes, idrs, static_cast<long long>(elapsed),
                static_cast<long long>(frames > 1 ? min_us : 0), static_cast<long long>(max_us));
    return ok ? 0 : 1;
}
