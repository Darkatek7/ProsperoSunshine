// SPDX-License-Identifier: GPL-3.0-only
#include "video/frame_pacing.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
using namespace prospero;
using namespace std::chrono_literals;
using clock_type = std::chrono::steady_clock;

namespace {
constexpr std::size_t packet = 1400;
const clock_type::time_point t0 = clock_type::time_point{} + 1h;

double ms(clock_type::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

// Sends one frame the way the broadcast thread does: whatever is due, then waits for the next due packet. Returns when
// its last packet left, relative to `start`.
clock_type::duration send_frame(FramePacer &pacer, clock_type::time_point start, clock_type::time_point arrival,
                                std::size_t bytes, bool keyframe, int *wakeups = nullptr) {
    pacer.begin(start, arrival, bytes, keyframe);
    std::size_t sent = 0;
    auto now = start;
    int rounds = 0;
    while (sent < bytes) {
        const auto n = pacer.packets_due(now, sent, packet);
        if (n == 0) {
            const auto next = pacer.due(sent);
            assert(next > now);
            now = next;
            continue;
        }
        sent = std::min(bytes, sent + n * packet);
        ++rounds;
        if (sent < bytes) {
            // The next packet is due later than now, or the loop would have taken it.
            assert(pacer.due(sent) >= now || pacer.packets_due(now, sent, packet) == 0);
        }
    }
    pacer.end(now);
    if (wakeups) *wakeups = rounds;
    return now - start;
}
}

int main() {
    // Ordinary frames of a 3.4 Mbit/s stream at 60 fps (about 7 kB, 8.5 kB with FEC) leave at once, every one of them.
    {
        FramePacer pacer;
        pacer.configure(3400, 60);
        auto at = t0;
        for (int i = 0; i < 120; ++i, at += 16667us) assert(send_frame(pacer, at, at, 8500, false) == 0s);
        // The burst is four average frames, the rate four times the stream's.
        assert(pacer.burst_bytes() > 4 * 8000 && pacer.burst_bytes() < 4 * 9000);
        assert(pacer.rate() > 4 * 8000 * 60 && pacer.rate() < 4 * 9000 * 60);

        // Its keyframe (180 kB on the wire) is spread over 60 to 100 ms instead of leaving in one burst.
        const auto spread = send_frame(pacer, at, at, 180'000, true);
        assert(spread > 60ms && spread <= max_frame_delay);
        // In the first millisecond no more than the burst and a packet or two leave.
        assert(pacer.packets_due(at + 1ms, 0, packet) * packet <= pacer.burst_bytes() + 0.001 * pacer.rate() + 2 * packet);
        // The keyframe didn't change the average it is paced at.
        assert(pacer.burst_bytes() < 4 * 9000);

        // The frame behind it waited for it and follows at the stream's pace: the burst is empty.
        const auto next_arrival = at + 16667us;
        const auto next_start = at + spread;
        const auto behind = send_frame(pacer, next_start, next_arrival, 8500, false);
        assert(behind > 0s && behind < 10ms);
        // After a frame interval without anything to send the burst is full again.
        const auto later = next_start + behind + 16667us;
        assert(send_frame(pacer, later, later, 8500, false) == 0s);
    }

    // Nothing waits longer than 100 ms after it reached the sender: a huge frame goes faster than the pace.
    {
        FramePacer pacer;
        pacer.configure(3400, 60);
        assert(send_frame(pacer, t0, t0, 1'000'000, true) <= max_frame_delay + 1ms);
        // A frame that waited 90 ms behind it has 10 ms left.
        const auto spread = send_frame(pacer, t0 + 100ms, t0 + 10ms, 100'000, false);
        assert(spread > 5ms && spread <= 10ms);
        // One that waited longer than 100 ms goes at once.
        assert(send_frame(pacer, t0 + 300ms, t0 + 150ms, 100'000, false) == 0s);
    }

    // A LAN stream at 50 Mbit/s: a keyframe of six average frames (with FEC 750 kB) leaves within a frame interval, about
    // 2 ms later than the wire limit alone would send it (7.5 ms at 100 MB/s).
    {
        FramePacer pacer;
        pacer.configure(50'000, 60);
        auto at = t0;
        for (int i = 0; i < 60; ++i, at += 16667us) assert(send_frame(pacer, at, at, 125'000, false) == 0s);
        const auto spread = send_frame(pacer, at, at, 750'000, true);
        assert(spread > 0s && spread < 13ms);
        assert(ms(spread) - 750'000 / wire_bytes_per_second * 1000 < 3.0);
    }
    // At 150 Mbit/s the burst holds a keyframe of four average frames (1.2 MB); from 200 Mbit/s the pace is the wire limit.
    {
        FramePacer pacer;
        pacer.configure(150'000, 60);
        assert(send_frame(pacer, t0, t0, 1'200'000, true) == 0s);
        FramePacer fast;
        fast.configure(200'000, 60);
        assert(fast.rate() == wire_bytes_per_second);
    }

    // A stream that carries more than the client asked for (10.x keeps no bitrate): the pace follows what it carries.
    {
        FramePacer pacer;
        pacer.configure(3000, 60);
        const double asked = pacer.rate();
        auto at = t0;
        for (int i = 0; i < 64; ++i, at += 16667us) send_frame(pacer, at, at, 20'000, false);
        assert(pacer.rate() > 2 * asked);
        assert(pacer.burst_bytes() > 4 * 19'000);
        // Keyframes stay out of the average: a loop of them doesn't loosen the pace.
        FramePacer loop;
        loop.configure(3000, 60);
        const double before = loop.rate();
        at = t0;
        for (int i = 0; i < 64; ++i, at += 125ms) send_frame(loop, at, at, 150'000, true);
        assert(loop.rate() == before);
    }

    // 30 fps: the same bitrate makes twice the average frame; the burst and the rate follow.
    {
        FramePacer p30, p60;
        p30.configure(10'000, 30);
        p60.configure(10'000, 60);
        assert(std::abs(p30.burst_bytes() - 2 * p60.burst_bytes()) < 1);
        assert(std::abs(p30.rate() - p60.rate()) < 1);
    }

    // Unknown bitrate: no pacing at all.
    {
        FramePacer pacer;
        pacer.configure(0, 60);
        assert(send_frame(pacer, t0, t0, 500'000, true) == 0s);
    }

    // packets_due counts whole packets, at least one once the next is due, and never beyond the frame.
    {
        FramePacer pacer;
        pacer.configure(3400, 60);
        pacer.begin(t0, t0, 100'000, true);
        const auto first = pacer.packets_due(t0, 0, packet);
        assert(first >= 1 && first * packet >= pacer.burst_bytes() && (first - 1) * packet <= pacer.burst_bytes());
        assert(pacer.packets_due(t0, first * packet, packet) == 0);
        assert(pacer.due(first * packet) > t0);
        assert(pacer.packets_due(pacer.due(first * packet), first * packet, packet) >= 1);
        assert(pacer.packets_due(t0 + 1s, 0, packet) == (100'000 + packet - 1) / packet);
        assert(pacer.packets_due(t0 + 1s, 100'000, packet) == 0);
    }

    // idle_spread: the spread keyframe_policy.hpp expects for a keyframe.
    assert(idle_spread(3400, 60, false, 8000) == 0s);
    assert(idle_spread(3400, 60, false, 180'000) > 60ms && idle_spread(3400, 60, false, 180'000) <= max_frame_delay);
    assert(idle_spread(150'000, 60, false, 1'000'000) == 0s);
    assert(idle_spread(0, 60, false, 1'000'000) == 0s);
    // Over the internet the burst is smaller, so the same keyframe takes a little longer.
    assert(idle_spread(3400, 30, true, 180'000) > idle_spread(3400, 30, false, 180'000));

    // Over the internet (issue 37): no burst above 32 kB, whatever the bitrate, so a path that keeps only the first 42 kB
    // of a burst gets every packet. Ordinary frames up to that size still leave at once.
    {
        assert(remote_packet_size(1024) && remote_packet_size(992) && remote_packet_size(1184) && remote_packet_size(1152));
        assert(!remote_packet_size(1392) && !remote_packet_size(1360) && !remote_packet_size(0));
        for (const std::uint32_t kbps : {1300u, 3428u, 7308u, 8908u, 14988u, 50'000u}) {
            FramePacer pacer;
            pacer.configure(kbps, 30, true);
            auto at = t0;
            for (int i = 0; i < 64; ++i, at += 33333us) send_frame(pacer, at, at, std::min<std::size_t>(30'000, kbps * 1000 / 8 / 30 * 6 / 5), false);
            assert(pacer.burst_bytes() <= remote_burst_bytes);
            // A keyframe of 150 packets: in no millisecond more than the burst and the pace allow.
            pacer.begin(at, at, 156'000, true);
            const std::size_t first = pacer.packets_due(at, 0, 1040);
            assert(first * 1040 <= remote_burst_bytes + 1040);
            for (auto t = at; t < at + max_frame_delay; t += 1ms) {
                const auto by_then = pacer.packets_due(t, 0, 1040);
                const auto a_ms_later = pacer.packets_due(t + 1ms, 0, 1040);
                // At most 42 kB queued at a relay that reads nothing for a millisecond: the burst, then the pace.
                assert(t == at || (a_ms_later - by_then) * 1040 <= 0.001 * std::max(pacer.rate(), 156'000 / 0.1) + 2 * 1040);
            }
        }
        // A frame of 25 kB still leaves at once.
        FramePacer pacer;
        pacer.configure(3428, 30, true);
        assert(send_frame(pacer, t0, t0, 25'000, false) == 0s);
        // A LAN stream at the same bitrate keeps its larger burst.
        FramePacer lan;
        lan.configure(50'000, 60, false);
        assert(lan.burst_bytes() > 300'000);
    }

    // The wire limit: 100 kB at once, then 100 MB/s.
    {
        WireLimiter wire;
        auto now = t0;
        assert(wire.due(now) == now);
        wire.sent(now, 90'000);
        assert(wire.due(now) == now);
        wire.sent(now, 64'000);
        const auto next = wire.due(now);
        assert(next > now && next - now < 1ms);
        // A second later it is full again, not more.
        now += 1s;
        wire.sent(now, 100'000);
        assert(wire.due(now) == now);
        wire.sent(now, 1);
        assert(wire.due(now) > now);
        // 1 MB in batches of 64 kB takes about 10 ms.
        WireLimiter big;
        auto at = t0;
        std::size_t total = 0;
        while (total < 1'000'000) {
            at = big.due(at);
            big.sent(at, 64'000);
            total += 64'000;
        }
        assert(at - t0 > 8ms && at - t0 < 11ms);
    }

    std::cout << "frame_pacing_test: OK\n";
    return 0;
}
