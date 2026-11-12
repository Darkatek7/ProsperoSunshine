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
    // A remote stream of 3.4 Mbit/s at 60 fps: its ordinary frames (about 6 kB with FEC) leave at once, every one of them.
    {
        FramePacer pacer;
        pacer.configure(3400, 60, true);
        assert(pacer.paces());
        auto at = t0;
        for (int i = 0; i < 120; ++i, at += 16667us) assert(send_frame(pacer, at, at, 6000, false) == 0s);
        // The burst is four average frames (what the client asked for: 7083 bytes) up to 16 kB, the rate twice the stream's.
        assert(pacer.burst_bytes() == remote_burst_bytes);
        assert(std::abs(pacer.rate() - 2 * 3400 * 1000 / 8.0) < 1);

        // Its keyframe (180 kB on the wire) is spread over 60 to 100 ms instead of leaving in one burst.
        const auto spread = send_frame(pacer, at, at, 180'000, true);
        assert(spread > 60ms && spread <= max_frame_delay);
        // In the first millisecond no more than the burst and a packet or two leave.
        assert(pacer.packets_due(at + 1ms, 0, packet) * packet <= pacer.burst_bytes() + 0.001 * pacer.rate() + 2 * packet);
        // The keyframe didn't change the average it is paced at.
        assert(std::abs(pacer.rate() - 2 * 3400 * 1000 / 8.0) < 1);

        // The frame behind it waited for it and follows at the stream's pace: the burst is empty.
        const auto next_arrival = at + 16667us;
        const auto next_start = at + spread;
        const auto behind = send_frame(pacer, next_start, next_arrival, 6000, false);
        assert(behind > 0s && behind < 10ms);
        // After a frame interval without anything to send the burst is full again.
        const auto later = next_start + behind + 16667us;
        assert(send_frame(pacer, later, later, 6000, false) == 0s);
    }

    // Nothing waits longer than 100 ms after it reached the sender: a huge frame goes faster than the pace.
    {
        FramePacer pacer;
        pacer.configure(3400, 60, true);
        assert(send_frame(pacer, t0, t0, 1'000'000, true) <= max_frame_delay + 1ms);
        // A frame that waited 90 ms behind it has 10 ms left.
        const auto spread = send_frame(pacer, t0 + 100ms, t0 + 10ms, 100'000, false);
        assert(spread > 5ms && spread <= 10ms);
        // One that waited longer than 100 ms goes at once.
        assert(send_frame(pacer, t0 + 300ms, t0 + 150ms, 100'000, false) == 0s);
    }

    // A client on the LAN is never paced: every frame leaves at once, as before, and only the wire limit spreads it. The
    // keyframes the repository measured (170 to 240 kB, 210 to 300 kB on the wire) would otherwise leave 9 to 62 ms later
    // at 10 to 20 Mbit/s and 60 fps.
    for (const std::uint32_t kbps : {0u, 3400u, 7650u, 15'300u, 50'000u, 150'000u}) {
        for (const int fps : {30, 60}) {
            FramePacer lan;
            lan.configure(kbps, fps, false);
            assert(!lan.paces());
            auto at = t0;
            for (int i = 0; i < 30; ++i, at += 16667us) assert(send_frame(lan, at, at, kbps * 1000 / 8 / fps + 1, false) == 0s);
            assert(send_frame(lan, at, at, 300'000, true) == 0s);
            // Even right behind it, and with a frame far above the average.
            assert(send_frame(lan, at, at, 1'000'000, false) == 0s);
        }
    }

    // A remote stream that carries more than the client asked for (10.x keeps no bitrate): the pace follows what it
    // carries; the burst stays at most 16 kB.
    {
        FramePacer pacer;
        pacer.configure(3000, 60, true);
        const double asked = pacer.rate();
        auto at = t0;
        for (int i = 0; i < 64; ++i, at += 16667us) send_frame(pacer, at, at, 20'000, false);
        assert(pacer.rate() > 2 * asked);
        assert(pacer.burst_bytes() == remote_burst_bytes);
        // Keyframes stay out of the average: a loop of them doesn't loosen the pace.
        FramePacer loop;
        loop.configure(3000, 60, true);
        const double before = loop.rate();
        at = t0;
        for (int i = 0; i < 64; ++i, at += 125ms) send_frame(loop, at, at, 150'000, true);
        assert(loop.rate() == before);
    }

    // 30 fps: the same bitrate makes twice the average frame; the rate is the same, the burst at least as large.
    {
        FramePacer p30, p60;
        p30.configure(1000, 30, true);
        p60.configure(1000, 60, true);
        assert(std::abs(p30.rate() - p60.rate()) < 1);
        assert(p30.burst_bytes() >= p60.burst_bytes());
        assert(std::abs(p60.burst_bytes() - 4 * 1000 * 1000 / 8.0 / 60) < 1);
        assert(p30.burst_bytes() == remote_burst_bytes);
        // A very slow stream: the burst is at least 8 kB.
        FramePacer slow;
        slow.configure(300, 60, true);
        assert(slow.burst_bytes() == min_burst_bytes);
    }

    // sunshine.conf: internet_pace_percent and internet_burst_kb. 400 and 32 are what 0.6.5 did; out of range keeps the
    // defaults (200 and 16), and a burst set below 8 kB is the burst.
    {
        FramePacer old;
        old.configure(3428, 30, true, PacingSettings{400, 32});
        assert(std::abs(old.rate() - 4 * 3428 * 1000 / 8.0) < 1);
        assert(old.burst_bytes() == 32 * 1024);
        assert(send_frame(old, t0, t0, 25'000, false) == 0s);
        for (const PacingSettings odd : {PacingSettings{0, 0}, PacingSettings{124, 1}, PacingSettings{801, 65}, PacingSettings{-5, 1000}}) {
            const auto checked = odd.checked();
            assert(checked.pace_percent == 200 && checked.burst_kb == 16);
            FramePacer pacer;
            pacer.configure(3428, 30, true, odd);
            assert(std::abs(pacer.rate() - 2 * 3428 * 1000 / 8.0) < 1 && pacer.burst_bytes() == remote_burst_bytes);
        }
        FramePacer edge;
        edge.configure(3428, 30, true, PacingSettings{125, 64});
        assert(std::abs(edge.rate() - 1.25 * 3428 * 1000 / 8.0) < 1 && std::abs(edge.burst_bytes() - 4 * 3428 * 1000 / 8.0 / 30) < 1);
        FramePacer tiny;
        tiny.configure(3428, 30, true, PacingSettings{800, 2});
        assert(std::abs(tiny.rate() - 8 * 3428 * 1000 / 8.0) < 1 && tiny.burst_bytes() == 2 * 1024);
        // A slower pace spreads a keyframe longer, up to the 100 ms every frame gets at most.
        const auto quick = idle_spread(14'988, 60, true, 150'000, PacingSettings{800, 16});
        const auto normal = idle_spread(14'988, 60, true, 150'000);
        const auto slower = idle_spread(14'988, 60, true, 150'000, PacingSettings{125, 16});
        assert(quick < normal && normal < slower && slower <= max_frame_delay);
        // The LAN ignores both.
        FramePacer lan;
        lan.configure(3428, 30, false, PacingSettings{125, 2});
        assert(!lan.paces() && send_frame(lan, t0, t0, 156'000, true) == 0s);
    }

    // Unknown bitrate: no pacing at all.
    {
        FramePacer pacer;
        pacer.configure(0, 60, true);
        assert(!pacer.paces());
        assert(send_frame(pacer, t0, t0, 500'000, true) == 0s);
    }

    // packets_due counts whole packets, at least one once the next is due, and never beyond the frame.
    {
        FramePacer pacer;
        pacer.configure(3400, 60, true);
        pacer.begin(t0, t0, 100'000, true);
        const auto first = pacer.packets_due(t0, 0, packet);
        assert(first >= 1 && first * packet >= pacer.burst_bytes() && (first - 1) * packet <= pacer.burst_bytes());
        assert(pacer.packets_due(t0, first * packet, packet) == 0);
        assert(pacer.due(first * packet) > t0);
        assert(pacer.packets_due(pacer.due(first * packet), first * packet, packet) >= 1);
        assert(pacer.packets_due(t0 + 1s, 0, packet) == (100'000 + packet - 1) / packet);
        assert(pacer.packets_due(t0 + 1s, 100'000, packet) == 0);
    }

    // idle_spread: the spread keyframe_policy.hpp expects for a keyframe. Over the internet the pacer's, on the LAN what the
    // wire limit takes beyond its 100 kB.
    assert(idle_spread(3400, 60, true, 8000) == 0s);
    assert(idle_spread(3400, 60, true, 180'000) > 60ms && idle_spread(3400, 60, true, 180'000) <= max_frame_delay);
    assert(idle_spread(0, 60, true, 1'000'000) == 0s);
    assert(idle_spread(3400, 60, false, 80'000) == 0s);
    assert(ms(idle_spread(3400, 60, false, 180'000)) > 0.79 && ms(idle_spread(3400, 60, false, 180'000)) < 0.81);
    assert(ms(idle_spread(150'000, 60, false, 1'000'000)) > 8.99 && ms(idle_spread(150'000, 60, false, 1'000'000)) < 9.01);
    assert(idle_spread(3400, 30, true, 180'000) > idle_spread(3400, 30, false, 180'000));

    // Over the internet (issue 37): no burst above 16 kB, whatever the bitrate, so a path that keeps only the first 42 kB
    // of a burst gets every packet, with room for what it still holds. Ordinary frames up to that size still leave at once.
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
        // A frame of 15 kB still leaves at once; one of 25 kB is spread over a few ms.
        FramePacer pacer;
        pacer.configure(3428, 30, true);
        assert(send_frame(pacer, t0, t0, 15'000, false) == 0s);
        const auto later = t0 + 1s;
        const auto spread25 = send_frame(pacer, later, later, 25'000, false);
        assert(spread25 > 0s && spread25 < 20ms);
        // A LAN stream at the same bitrate isn't paced.
        FramePacer lan;
        lan.configure(3428, 30, false);
        assert(!lan.paces() && send_frame(lan, t0, t0, 156'000, true) == 0s);
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
