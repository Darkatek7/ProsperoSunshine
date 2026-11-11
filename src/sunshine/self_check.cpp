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
#include <openssl/evp.h>
#include <rs.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>
extern "C" {
#include <ps5/kernel.h>
#include "platform/ps5/capture.h"
#include "platform/ps5/diag.h"
// Not in OpenSSL's public headers; libcrypto defines it: the capability words OPENSSL_cpuid_setup filled in at start.
extern unsigned int OPENSSL_ia32cap_P[4];
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

double seconds_since(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
}

// Whether the fast paths are in use. A 150 kB IDR at 20 % FEC is 107 data and 22 parity shards of 1408 bytes: with AVX2 the
// Reed-Solomon encode takes a few tenths of a millisecond (mac_mb_s in the thousands), the scalar fallback several ms (under
// 1000). AES-128-GCM over video-sized packets with one key, as the stream does it: AES-NI and PCLMUL give about 1 to 3 us a
// packet (gcm_mb_s in the thousands), table code 10 to 30. ia32cap shows what OpenSSL decided (AES-NI is bit 25 of the
// second word, PCLMULQDQ bit 1, AVX bit 28).
void perf_check() {
    constexpr int data = 107, parity = 22, block = 1408, rounds = 200;
    std::vector<uint8_t> memory(static_cast<size_t>(data + parity) * block, 7);
    std::vector<uint8_t *> shards(data + parity);
    for (int i = 0; i < data + parity; i++) shards[i] = memory.data() + static_cast<size_t>(i) * block;
    if (reed_solomon *rs = reed_solomon_new(data, parity)) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < rounds; i++) reed_solomon_encode(rs, shards.data(), data + parity, block);
        const double took = seconds_since(start);
        reed_solomon_release(rs);
        ps_diag_log("ProsperoSunshine: perfcheck fec_us_per_idr=%.0f mac_mb_s=%.0f\n", took / rounds * 1e6,
                    static_cast<double>(data) * parity * block * rounds / took / 1e6);
    }
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    uint8_t key[16] = {1}, iv[12] = {2}, tag[16];
    std::vector<uint8_t> packet(block, 3);
    int out = 0;
    constexpr int packets = 20000;
    EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr);
    EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, iv);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < packets; i++) {
        iv[0] = static_cast<uint8_t>(i);
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, nullptr, iv);
        EVP_EncryptUpdate(ctx, packet.data(), &out, packet.data(), static_cast<int>(packet.size()));
        EVP_EncryptFinal_ex(ctx, packet.data() + out, &out);
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);
    }
    const double took = seconds_since(start);
    EVP_CIPHER_CTX_free(ctx);
    ps_diag_log("ProsperoSunshine: perfcheck gcm_us_per_packet=%.2f gcm_mb_s=%.0f ia32cap=%08x:%08x\n", took / packets * 1e6,
                packets * static_cast<double>(block) / took / 1e6, OPENSSL_ia32cap_P[0], OPENSSL_ia32cap_P[1]);
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

    perf_check();

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    ps_diag_log("ProsperoSunshine: selfcheck done ms=%lld problems=%d\n", static_cast<long long>(ms), problems);
}

}  // namespace prospero
