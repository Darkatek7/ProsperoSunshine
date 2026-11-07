// SPDX-License-Identifier: GPL-3.0-only
#include "sunshine/self_check.hpp"
#include "control/control_server.hpp"
#include "control/listener_watch.hpp"
#include "platform/ps5/mdns_responder.hpp"
#include "src/network.h"
#include "src/nvhttp.h"
#include "src/rtsp.h"
#include "src/stream.h"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <string>
extern "C" {
#include <ps5/kernel.h>
#include "platform/ps5/capture.h"
#include "platform/ps5/diag.h"
}

namespace prospero {
namespace {

intptr_t libkernel_symbol(const char *name) {
    for (const char *library : {"libkernel_web.sprx", "libkernel_sys.sprx", "libkernel.sprx"}) {
        uint32_t handle = 0;
        if (kernel_dynlib_handle(-1, library, &handle) == 0 && handle)
            if (const intptr_t address = kernel_dynlib_dlsym(-1, handle, name)) return address;
    }
    return 0;
}

// Whether a UDP socket could be bound to this port now (Sunshine binds the stream's ports without SO_REUSEADDR).
bool udp_free(uint16_t port) {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    const bool free = bind(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof addr) == 0;
    close(fd);
    return free;
}

}  // namespace

void self_check() {
    const auto started = std::chrono::steady_clock::now();
    int problems = 0;

    const uint32_t firmware = kernel_get_fw_version();
    int pro = -1;
    if (const intptr_t has_trinity = libkernel_symbol("sceKernelHasTrinityMode")) {
        ps_diag_trace("selfcheck sceKernelHasTrinityMode\n");
        pro = reinterpret_cast<int (*)()>(has_trinity)();
    }
    ps_diag_log("ProsperoSunshine: selfcheck firmware=%#x known=%d console=%s\n", firmware, ps_ra_find_known(firmware) != nullptr,
                pro < 0 ? "unknown" : pro ? "PS5-Pro" : "PS5");

    ps_ra_functions ra{};
    const int ra_rc = ps_capture_check_ra(&ra);
    int by_nid = 0, by_table = 0;
    for (int f = 0; f < PS_RA_COUNT; f++) {
        if (ra.nid[f] && ra.offset[f] == ra.nid[f]) by_nid++;
        else if (ra.offset[f]) by_table++;
    }
    ps_diag_log("ProsperoSunshine: selfcheck ra=%s by_nid=%d by_table=%d calls=%d\n", ra_rc ? "refused" : "resolved", by_nid,
                by_table, PS_RA_COUNT);
    problems += ra_rc != 0;

    std::string taken;
    // The web page is on base port + 1 (web/web_server.cpp).
    for (const int offset : {nvhttp::PORT_HTTPS, nvhttp::PORT_HTTP, 1, rtsp_stream::RTSP_SETUP_PORT}) {
        const uint16_t port = net::map_port(offset);
        if (!port_free(port)) taken += " tcp/" + std::to_string(port);
    }
    for (const uint16_t port : {ControlServer::port, ControlServer::report_port})
        if (!port_free(port)) taken += " tcp/" + std::to_string(port);
    for (const int offset : {stream::VIDEO_STREAM_PORT, stream::CONTROL_PORT, stream::AUDIO_STREAM_PORT}) {
        const uint16_t port = net::map_port(offset);
        if (!udp_free(port)) taken += " udp/" + std::to_string(port);
    }
    ps_diag_log("ProsperoSunshine: selfcheck ports=%s\n", taken.empty() ? "free" : ("taken" + taken).c_str());
    problems += !taken.empty();

    const std::string mdns = mdns::check_socket();
    ps_diag_log("ProsperoSunshine: selfcheck mdns=%s\n", mdns.empty() ? "ok" : mdns.c_str());
    problems += !mdns.empty();

    // OpenVideo, then closed: "open only" is the step it stops at on purpose. The codec and mode are the defaults a
    // 1080p60 H.264 client gets.
    if (ra_rc == 0) {
        ps_capture_set_open_only(1);
        ps_capture *session = nullptr;
        const int rc = ps_capture_slot_open(0, &session);
        ps_capture_set_open_only(0);
        if (session) ps_capture_close(session);
        const char *step = "";
        const int raw = ps_capture_last_failure(&step);
        const bool opens = rc != 0 && step && std::strcmp(step, "open only") == 0;
        ps_diag_log("ProsperoSunshine: selfcheck capture=%s mode=1920x1080@60 codec=H.264 step=%s rc=%#x\n",
                    opens ? "opens" : "failed", step ? step : "", static_cast<unsigned>(raw));
        problems += !opens;
    }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    ps_diag_log("ProsperoSunshine: selfcheck done ms=%lld problems=%d\n", static_cast<long long>(ms), problems);
}

}  // namespace prospero
