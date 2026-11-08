// SPDX-License-Identifier: GPL-3.0-only
// Waking from Moonlight (GitHub issue 10): does a payload keep running in rest mode, and does a Wake-on-LAN magic packet
// reach it there? Listens on UDP 9 and 7 (Moonlight sends its magic packets to 9, among others) and writes a line every
// second; a jump in the wall clock between two lines means the process was stopped. Every packet is logged with whether it
// is a magic packet. Ends after /data/prosperosunshine/rest_probe.seconds (default 300) or when rest_probe.quit appears.
// Writes /data/prosperosunshine/rest_probe.log and klog.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
extern "C" {
#include <ps5/klog.h>
}

namespace {

FILE *log_file = nullptr;

void say(const char *format, ...) {
    char line[256];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof line, format, args);
    va_end(args);
    const auto now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%H:%M:%S", std::localtime(&now));
    klog_printf("ProsperoSunshine: rest_probe %s\n", line);
    if (log_file) {
        fprintf(log_file, "%s %s\n", stamp, line);
        fflush(log_file);
    }
}

int listen_udp(uint16_t port) {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    const int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0) {
        close(fd);
        return -2;
    }
    return fd;
}

// Six bytes of 0xFF, then one MAC sixteen times. Returns the MAC as text, or empty.
std::string magic_mac(const uint8_t *data, size_t size) {
    for (size_t start = 0; start + 102 <= size; ++start) {
        bool sync = true;
        for (size_t i = 0; i < 6 && sync; ++i) sync = data[start + i] == 0xFF;
        if (!sync) continue;
        const uint8_t *mac = data + start + 6;
        bool repeated = true;
        for (size_t r = 1; r < 16 && repeated; ++r) repeated = std::memcmp(mac, mac + r * 6, 6) == 0;
        if (!repeated) continue;
        char text[18];
        snprintf(text, sizeof text, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return text;
    }
    return {};
}

bool exists(const char *path) { return access(path, F_OK) == 0; }

}  // namespace

int main() {
    using namespace std::chrono;
    log_file = fopen("/data/prosperosunshine/rest_probe.log", "w");
    int seconds = 300;
    {
        std::ifstream in("/data/prosperosunshine/rest_probe.seconds");
        in >> seconds;
    }
    unlink("/data/prosperosunshine/rest_probe.quit");
    const uint16_t ports[] = {9, 7};
    pollfd fds[2];
    for (int i = 0; i < 2; ++i) {
        fds[i] = {listen_udp(ports[i]), POLLIN, 0};
        say("listen udp %u -> %d", ports[i], fds[i].fd);
    }
    const auto end = steady_clock::now() + std::chrono::seconds(seconds);
    auto last_wall = system_clock::now();
    auto last_tick = steady_clock::now();
    int tick = 0;
    while (steady_clock::now() < end && !exists("/data/prosperosunshine/rest_probe.quit")) {
        const int ready = poll(fds, 2, 1000);
        const auto wall = system_clock::now();
        const auto gap = duration_cast<milliseconds>(wall - last_wall).count();
        if (gap > 3000) say("gap: %lld ms of wall clock between two turns (stopped?)", static_cast<long long>(gap));
        last_wall = wall;
        if (ready < 0) say("poll error %d", errno);
        for (int i = 0; i < 2 && ready > 0; ++i) {
            if (!(fds[i].revents & POLLIN)) {
                if (fds[i].revents) say("udp %u revents %#x", ports[i], fds[i].revents);
                continue;
            }
            uint8_t data[512];
            sockaddr_in from{};
            socklen_t from_size = sizeof from;
            const ssize_t size = recvfrom(fds[i].fd, data, sizeof data, 0, reinterpret_cast<sockaddr *>(&from), &from_size);
            char ip[INET_ADDRSTRLEN] = "?";
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
            if (size < 0) {
                say("udp %u recv error %d", ports[i], errno);
                continue;
            }
            const auto mac = magic_mac(data, static_cast<size_t>(size));
            say("udp %u: %zd bytes from %s:%u%s%s", ports[i], size, ip, ntohs(from.sin_port),
                mac.empty() ? "" : ", magic packet for ", mac.c_str());
        }
        if (steady_clock::now() - last_tick >= std::chrono::seconds(10)) {
            last_tick = steady_clock::now();
            say("tick %d", ++tick);
        }
    }
    for (auto &fd : fds)
        if (fd.fd >= 0) close(fd.fd);
    say("done");
    if (log_file) fclose(log_file);
    return 0;
}
