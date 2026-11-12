// SPDX-License-Identifier: GPL-3.0-only
// How the video sender spreads a frame that is much larger than the stream's average (a keyframe) over time instead of
// sending it as one burst, for a client that streams over the internet. Portable, so that the tests can run it: Sunshine's
// video broadcast thread uses it (patches/sunshine/0026-paced-video.patch), and keyframe_policy.hpp uses idle_spread() to
// tell how long a keyframe is on its way.
//
// Why: Sunshine sends every frame at up to 800 Mbit/s (80% of gigabit). A keyframe of a 3 Mbit/s stream is 100 to 200 kB,
// 20 to 30 average frames, and leaves the console in about 2 ms. A path that carries 3 to 10 Mbit/s (mobile, a relay, a
// shaped uplink) has to queue all of it at once or drop its tail, and a dropped tail makes the client ask for the next
// keyframe (issue 37). Small frames still go out at once: spreading them would only add latency.
//
// Only a client that streams over the internet is paced: Moonlight then asks for packets of 1024 bytes (1184 over IPv6,
// 32 less with video encryption) instead of 1392. A client on the LAN gets every frame as before, at Sunshine's wire limit
// alone (WireLimiter): there the pace would only delay its keyframes. Keyframes the repository measured are 55 to 70 kB on
// the home screen (13.00, 1080p), 169 kB in GTA III (10.20, HEVC at the QP for 35 to 47 Mbit/s) and up to 239 kB in the
// Mario 64 demo (HEVC asked for 100 Mbit/s); with FEC and headers about a quarter more. Paced like a remote stream, a
// 170 to 240 kB keyframe at 10 Mbit/s and 60 fps would leave its last byte 39 to 62 ms later than at the wire limit (2 to
// 3 ms), at 20 Mbit/s 9 to 19 ms later: more than a frame interval, for every LAN client at every keyframe.
//
// The plan for a remote stream, a token bucket:
//  - Average frame A: the larger of what the client asked for (bitrate / fps) and what the stream's frames other than
//    keyframes carried lately (a moving average over about 16 frames). On 10.x the encoder keeps no bitrate, so the
//    request alone can be too small; keyframes stay out of it, or a keyframe loop would raise the rate it is paced at.
//  - Burst D = 4 A, at least 8 kB and at most 16 kB (internet_burst_kb): a frame up to that size leaves at once, as
//    before. Issue 37's debug logs show a path keeping the first 39 or 40 packets of a larger frame and losing all the
//    rest: 4257 of the 5475 frames the client reported lost, in streams from 1.3 to 15 Mbit/s. 39 datagrams of 1040 bytes
//    with the kernel's bookkeeping fill 42 kB, FreeBSD's default UDP receive buffer: most likely a relay on the way
//    (ps5-tailscale runs on the console and forwards its clients from there) that a burst fills faster than it reads.
//    0.6.5 let 32 kB go at once; the reporter's next log (2026-11-11, 0.6.5's code over Tailscale) still showed short
//    freezes in fast scenes at 720p, and at 1080p a 32 kB burst is a single ordinary frame. 16 kB leaves room for what the
//    relay still holds from the frame before.
//  - The rest of a larger frame leaves at R = 2 x the stream's rate (A x fps; internet_pace_percent, 200 by default).
//    0.6.5 used four times: about 14 Mbit/s for a 3.4 Mbit/s stream, which the reporter's path took, but 60 Mbit/s for a
//    1080p stream at 15 Mbit/s, more than the console's 45 Mbit/s upload, and that stream lost frames in waves (up to 33
//    requests for a new picture in 10 s). At 8 to 10 Mbit/s the same path froze much less. Twice the rate still sends a
//    keyframe well below wire speed and gives the frames queued behind it room to catch up.
//  - No frame leaves its last byte later than 100 ms after it reached the sender: a frame that would take longer, or that
//    waited behind the stream's previous frames, goes faster. This bounds the latency the pacer can add, and with it the
//    queue, also when the stream carries more than the client asked for.
//  - The burst refills at R while the stream has nothing to send. After a keyframe it is empty, so the frames queued
//    behind it follow at R until it has refilled (R gives 2 A per frame interval).
// Both numbers can be set in sunshine.conf for a path that needs it (PacingSettings): a slower relay or uplink wants a
// lower pace and a smaller burst, a fast one may take more.
// A frame keeps its packets in order, and a stream's frames go out in order; other streams have pacers of their own.
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace prospero {

inline constexpr double burst_frames = 4.0;
inline constexpr double min_burst_bytes = 8.0 * 1024;

// What sunshine.conf may set for clients over the internet: how fast the part of a large frame beyond the burst leaves,
// in percent of the stream's rate (internet_pace_percent), and the largest burst in kB (internet_burst_kb). A value out
// of range keeps the default.
struct PacingSettings {
    static constexpr int default_pace_percent = 200;
    static constexpr int min_pace_percent = 125;
    static constexpr int max_pace_percent = 800;
    static constexpr int default_burst_kb = 16;
    static constexpr int min_burst_kb = 2;
    static constexpr int max_burst_kb = 64;
    int pace_percent = default_pace_percent;
    int burst_kb = default_burst_kb;

    // Out-of-range values replaced by the defaults.
    PacingSettings checked() const {
        PacingSettings s = *this;
        if (s.pace_percent < min_pace_percent || s.pace_percent > max_pace_percent) s.pace_percent = default_pace_percent;
        if (s.burst_kb < min_burst_kb || s.burst_kb > max_burst_kb) s.burst_kb = default_burst_kb;
        return s;
    }
    double pace_factor() const { return checked().pace_percent / 100.0; }
    double burst_cap_bytes() const { return checked().burst_kb * 1024.0; }
};
inline constexpr double remote_burst_bytes = PacingSettings::default_burst_kb * 1024.0;
// Moonlight's packet size on a LAN is 1392 (1360 with video encryption), over the internet 1024 or 1184 (less 32).
inline constexpr bool remote_packet_size(int packet_size) { return packet_size > 0 && packet_size < 1300; }
inline constexpr std::chrono::milliseconds max_frame_delay{100};
// Sunshine's own limit for all streams together: 80% of 1 Gbit/s, in steps of about 1 ms (stream.cpp).
inline constexpr double wire_bytes_per_second = 100e6;
inline constexpr double wire_burst_bytes = 100e3;

namespace pacing_detail {
inline double seconds(std::chrono::steady_clock::duration d) { return std::chrono::duration<double>(d).count(); }
// Rounded up, so that a packet is due no earlier than its byte's time.
inline std::chrono::steady_clock::duration duration_of(double seconds) {
    return std::chrono::ceil<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
}
}

class FramePacer {
public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;

    // The stream as the client asked for it: kbit/s, frames per second, and whether it streams over the internet, with
    // the pace and burst sunshine.conf sets for such a stream. Only a remote stream is paced.
    void configure(std::uint32_t kbps, int fps, bool remote = false, PacingSettings settings = {}) {
        fps_ = fps > 0 ? fps : 60;
        requested_frame_ = kbps > 0 ? kbps * 1000.0 / 8.0 / fps_ : 0.0;
        remote_ = remote;
        pace_factor_ = settings.pace_factor();
        burst_cap_ = settings.burst_cap_bytes();
    }

    double average_frame() const { return std::max(requested_frame_, measured_frame_); }
    // Whether frames above the burst are spread: a remote stream whose bitrate is known.
    bool paces() const { return remote_ && rate() > 0; }
    double burst_bytes() const {
        if (!remote_) return std::numeric_limits<double>::infinity();
        return std::clamp(burst_frames * average_frame(), std::min(min_burst_bytes, burst_cap_), burst_cap_);
    }
    // Bytes per second for the part of a frame beyond the burst; 0 before the stream is known (no pacing then).
    double rate() const {
        const double r = pace_factor_ * average_frame() * fps_;
        return r > 0 ? std::min(wire_bytes_per_second, r) : 0.0;
    }

    // A frame of `bytes` on the wire (FEC and headers included), which reached the sender at `arrival`, starts now at
    // `start`: no earlier than its arrival, and after the stream's previous frame went out.
    void begin(time_point start, time_point arrival, std::size_t bytes, bool keyframe) {
        bytes_ = static_cast<double>(bytes);
        start_ = start;
        if (!paces()) {
            // All at once, as before.
            burst_ = bytes_;
            frame_rate_ = std::numeric_limits<double>::infinity();
            end_ = idle_from_ = start;
            if (!keyframe) measured_frame_ += (bytes_ - measured_frame_) / 16;
            return;
        }
        const double depth = burst_bytes();
        const double r = rate();
        if (!started_) {
            credit_ = depth;
            started_ = true;
        } else if (start > idle_from_) {
            credit_ += pacing_detail::seconds(start - idle_from_) * r;
        }
        credit_ = std::min(credit_, depth);
        burst_ = std::min(bytes_, credit_);
        const double rest = bytes_ - burst_;
        frame_rate_ = r;
        if (rest <= 0) {
            credit_ -= bytes_;
        } else {
            credit_ = 0;
            const auto left = arrival + max_frame_delay - start;
            if (r <= 0 || left <= clock::duration::zero())
                frame_rate_ = std::numeric_limits<double>::infinity();
            else
                frame_rate_ = std::max(r, rest / pacing_detail::seconds(left));
        }
        end_ = std::isinf(frame_rate_) || rest <= 0 ? start : start + pacing_detail::duration_of(rest / frame_rate_);
        idle_from_ = end_;
        // Afterwards, so that an unusually large frame doesn't widen its own burst. It starts from nothing, so the request
        // holds until about 16 frames have shown what the stream carries.
        if (!keyframe) measured_frame_ += (bytes_ - measured_frame_) / 16;
    }

    // When the frame's byte at `offset` (from 0) may leave. A packet goes when its first byte is due.
    time_point due(std::size_t offset) const {
        const double o = static_cast<double>(offset);
        if (o <= burst_ || std::isinf(frame_rate_)) return start_;
        return start_ + pacing_detail::duration_of((o - burst_) / frame_rate_);
    }

    // How many packets of `packet_bytes`, from byte `sent` on, are due at `now`: at least 1 once the next one is due.
    std::size_t packets_due(time_point now, std::size_t sent, std::size_t packet_bytes) const {
        if (packet_bytes == 0) return 0;
        const double s = static_cast<double>(sent);
        if (s >= bytes_) return 0;
        const double left = bytes_ - s;
        const auto all = static_cast<std::size_t>((left + packet_bytes - 1) / packet_bytes);
        if (std::isinf(frame_rate_)) return all;
        // Half a byte of slack for rounding: a packet is due at the time due() gives for it.
        const double allowed = burst_ + std::max(0.0, pacing_detail::seconds(now - start_)) * frame_rate_ + 0.5;
        if (s > allowed) return 0;
        return std::min(all, static_cast<std::size_t>((allowed - s) / packet_bytes) + 1);
    }

    // The frame went out completely at `now`; the burst refills from then on (or from the planned end, if later).
    void end(time_point now) { idle_from_ = std::max(idle_from_, now); }

    // How long the current frame is spread: from its start until its last byte is due.
    clock::duration spread() const { return end_ - start_; }

private:
    int fps_ = 60;
    bool remote_ = false;
    double pace_factor_ = PacingSettings{}.pace_factor();
    double burst_cap_ = PacingSettings{}.burst_cap_bytes();
    double requested_frame_ = 0;
    double measured_frame_ = 0;
    bool started_ = false;
    double credit_ = 0;
    time_point idle_from_{};
    time_point start_{};
    time_point end_{};
    double bytes_ = 0;
    double burst_ = 0;
    double frame_rate_ = 0;
};

// How long a frame of `bytes` on the wire is spread when the stream had nothing queued and frames of the size it asked
// for: what keyframe_policy.hpp needs to know about a keyframe it just sent. A LAN stream's keyframe takes what the wire
// limit gives it.
inline std::chrono::steady_clock::duration idle_spread(std::uint32_t kbps, int fps, bool remote, std::size_t bytes,
                                                      PacingSettings settings = {}) {
    if (!remote)
        return pacing_detail::duration_of(std::max(0.0, static_cast<double>(bytes) - wire_burst_bytes) / wire_bytes_per_second);
    FramePacer pacer;
    pacer.configure(kbps, fps, remote, settings);
    const auto now = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
    pacer.begin(now, now, bytes, true);
    return pacer.spread();
}

// Sunshine's limit for all streams together: wire_bytes_per_second, sent in steps of about 1 ms. A batch may go once the
// previous ones are paid for; it may overdraw.
class WireLimiter {
public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;

    time_point due(time_point now) const {
        const double tokens = tokens_at(now);
        return tokens >= 0 ? now : now + pacing_detail::duration_of(-tokens / wire_bytes_per_second);
    }
    void sent(time_point now, std::size_t bytes) {
        tokens_ = tokens_at(now) - static_cast<double>(bytes);
        at_ = now;
    }

private:
    double tokens_at(time_point now) const {
        if (at_ == time_point{}) return wire_burst_bytes;
        return std::min(wire_burst_bytes, tokens_ + std::max(0.0, pacing_detail::seconds(now - at_)) * wire_bytes_per_second);
    }
    double tokens_ = wire_burst_bytes;
    time_point at_{};
};

}
