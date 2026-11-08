// SPDX-License-Identifier: GPL-3.0-only
// Do large video frames survive the way from the console? Waits on UDP 47901 for a hello from
// tools/moonlight/udp_burst.py, then answers with bursts the size of a frame, as fast as the host sends them.
// Packet: burst number, packet number, as u32.
#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

int main() {
    using namespace std::chrono;
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(47901);
    if (fd < 0 || bind(fd, reinterpret_cast<sockaddr *>(&local), sizeof local)) return 1;
    timeval wait{120, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof wait);
    uint32_t request[2]; // packets per burst, bursts
    uint8_t packet[1024] = {};
    for (;;) {
        sockaddr_in peer{};
        socklen_t peer_size = sizeof peer;
        if (recvfrom(fd, request, sizeof request, 0, reinterpret_cast<sockaddr *>(&peer), &peer_size) != sizeof request) break;
        auto next = steady_clock::now();
        for (uint32_t burst = 0; burst < request[1]; ++burst) {
            std::this_thread::sleep_until(next);
            next += microseconds(16667);
            for (uint32_t n = 0; n < request[0]; ++n) {
                std::memcpy(packet, &burst, 4);
                std::memcpy(packet + 4, &n, 4);
                for (int attempt = 0; attempt < 100; ++attempt) {
                    if (sendto(fd, packet, sizeof packet, 0, reinterpret_cast<sockaddr *>(&peer), peer_size) >= 0) break;
                    std::this_thread::sleep_for(100us);
                }
            }
        }
        const uint32_t end[2] = {0xffffffffu, 0};
        std::memcpy(packet, end, sizeof end);
        for (int i = 0; i < 3; ++i) sendto(fd, packet, sizeof packet, 0, reinterpret_cast<sockaddr *>(&peer), peer_size);
    }
    close(fd);
    return 0;
}
