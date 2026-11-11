// SPDX-License-Identifier: GPL-3.0-only
// Which of the clients' keyframe (IDR) requests the capture passes on to the encoder, and when. Portable, so that the
// tests can run it; video_ps5.cpp keeps one per capture channel, under captures.mutex, for every stream on the channel.
//
// What the client does (moonlight-common-c, read in the pinned source): the host announces no reference frame
// invalidation (video_ps5.cpp), so after any loss Moonlight drops every frame until an IDR arrives. It asks for one once it
// received the next complete frame (asking while everything is lost would only add to the congestion; a frame it finds
// corrupt makes it ask at once), asks again after every further loss followed by a complete frame while it waits, and on
// its own after it dropped 120 frames it received (2 s at 60 fps while frames arrive, 4 s at 30). It sends the request
// reliably over ENet, so a request lost on the way comes again after ENet's retransmission timeout. Avcap2 sends the IDR 2
// to 3 frames after the host asks (R1). An IDR is many average frames, and on a channel that several devices share
// (joined streams) it goes to all of them.
// On a lossy path this feeds itself. Issue 37: 831 requests in 3.5 minutes, nine in ten of them 100 to 300 ms apart, and
// the host sent 8 to 10 Mbit/s for a 3.4 Mbit/s stream; every IDR lost its tail on the way (frame_pacing.hpp), and the
// client asked again. While an IDR is on its way, every new loss also brings a request that the IDR already answers.
// The rules:
//
//  1. One IDR on its way answers every request before it. After the host asked the encoder, requests merge until an IDR
//     is read. After 250 ms without one (3 frames at 30 fps are 100 ms) the host asks again.
//  2. A request that reaches the host within W after the last IDR was read probably left the client before it could have
//     that IDR ("blind"), and isn't answered at once. W = the IDR's spread (frame_pacing.hpp) + 3/4 of the client's round
//     trip as ENet has it, at most 250 ms of it (ENet starts at 500 ms before its first measurement); without a round trip
//     W is the spread alone. A request because that IDR was lost leaves the client once the frame after it is complete, so
//     it reaches the host no earlier than the true round trip + the larger of the spread and a frame interval after the
//     read, and the capture loop sees it up to a frame later. But the round trip is a weak guess: the host's ENet measures
//     it only from acknowledgements of its own reliable messages (rumble, light bar, HDR), and Moonlight's reliable ping
//     every 100 ms keeps ENet from sending pings of its own, so in a game without rumble the value can be the one from the
//     connection's start, or from a delay spike, for minutes. After one late acknowledgement of S ms on a true round trip
//     T, ENet's value stays more than a third above T (where W swallows a real request) while (S - T) / 8 x (7/8)^n > T / 3:
//     a 500 ms spike on 50 ms keeps it there for 9 samples. A request that only looks blind may be the client asking for
//     the IDR it lost, and the client shows nothing until it gets one: dropped, it would wait for its 120 frames (2 to 4 s).
//     So a blind request is answered blind_hold (250 ms) after it, unless an IDR goes out before (one asked for another
//     reason, a later request of the same client, the encoder's own): the encoder then needs 2 to 3 frames, so a real
//     request gets its IDR read at most about 370 ms after it reached the host (30 fps), and one that crossed an IDR that
//     arrived costs one IDR more. In a simulation (Moonlight's rules, the capture loop once per frame) of a LAN at 20 Mbit/s
//     with a loss every 0.4 s, of Wi-Fi with delay spikes and ENet's value stuck at 40 or 80 ms, and of issue 37's path,
//     IDRs per minute stayed within 15 % of dropping blind requests (a third fewer where every IDR failed), while a real
//     request that rule 2 took for blind waited up to 367 ms instead of up to 4.1 s.
//  3. A device that joins a running channel needs an IDR to start: forced, only rule 1 applies.
//  4. When asking the encoder fails, the request is dropped as before: the encoder's own IDR follows within 10 s.
//
// What isn't here: a hold-off for a client whose IDRs keep failing. One was built (after two failed IDRs in a row the
// next IDR for that client followed the last one no sooner than twice the IDR's airtime at the client's bitrate) and
// measured on the console (2026-11-11, firmware 13.00, 3.4 Mbit/s at 30 fps, 1024-byte packets, 10 % of the packets lost in
// bursts of 8, runs of 30 s, the longest time between two frames at the client, median of six runs, the builds taking
// turns): without a hold-off 561 and 558 ms (two rounds), 0.6.0, which answered every request at once, 619 ms; with the
// wait doubling for each further pair of failures up to 500 ms, 1025 ms; without the doubling, 1113 ms; with the doubling up
// to 2 s, 4.5 s in single runs, an IDR that was held back for 1.9 s being lost as well. On a path that loses packets a new
// try is what brings the picture back, and every wait adds to the time it stands; a few waits of 130 to 430 ms in 10 s
// were enough to double the longest gap. What a hold-off saves is bitrate where no IDR gets through at all: through a
// bottleneck of 2.5 Mbit/s for that stream the host sent 4.8 to 4.9 Mbit/s without it and 3.6 to 4.1 with it (0.6.0: 5.8 to
// 6.4), and no build got a picture through. So rules 1 and 2 are the only limit: at worst, while every IDR fails and the
// client asks as fast as Moonlight can, one IDR per round trip + blind window + the encoder's 2 to 3 frames, 6 a second
// for issue 37's stream (3.4 Mbit/s, about 160 kB per IDR on the wire) in tests/keyframe_policy_test.cpp. Issue 37's loop
// itself ended with the pacing (frame_pacing.hpp): the same stream through a buffer of 39 packets asked for one IDR in 30 s.
#pragma once
#include "video/frame_pacing.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>

namespace prospero {

class KeyframePolicy {
public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;
    using ms = std::chrono::milliseconds;

    static constexpr ms encoder_patience{250};
    static constexpr ms rtt_cap{250};
    static constexpr ms blind_hold{250};
    static constexpr ms forget_after{60'000};

    // The client that asks: who it is (any number that tells the streams of the channel apart), its control
    // connection's smoothed round-trip time (0 while unknown), and its stream's bitrate, frame rate and whether it
    // streams over the internet, which give an IDR's spread.
    struct Client {
        std::uintptr_t id = 0;
        ms rtt{0};
        std::uint32_t kbps = 0;
        int fps = 60;
        bool remote = false;
    };

    enum class Outcome {
        now,     // the encoder is asked at the next poll
        merged,  // an IDR is already wanted or on its way from the encoder (rule 1)
        blind,   // probably sent before the client could have the last IDR: answered blind_hold later unless one comes (rule 2)
    };

    // For the log: what happened to the requests since the last take_counts().
    struct Counts {
        int requests = 0, merged = 0, blind = 0;
        int asked = 0;  // times the encoder was asked
    };

    Outcome request(time_point now, const Client &client) {
        ++counts_.requests;
        forget_old(now);
        State &state = clients_[client.id];
        state.seen = now;
        state.waiting = true;
        if (asked_at_) {
            ++counts_.merged;
            return Outcome::merged;
        }
        const bool blind = last_keyframe_ && now < *last_keyframe_ + blind_window(client);
        const time_point due = blind ? now + blind_hold : now;
        const bool merged = wanted_.has_value() && *wanted_ <= due;
        want(due);
        if (merged) {
            ++counts_.merged;
            return Outcome::merged;
        }
        if (blind) {
            ++counts_.blind;
            return Outcome::blind;
        }
        return Outcome::now;
    }

    // A stream needs an IDR to start.
    void force() { forced_ = true; }

    // Whether to ask the encoder for an IDR now. Once it says so, it waits for the IDR (keyframe_sent) or encoder_patience.
    bool poll(time_point now) {
        if (!wanted_ && !forced_) return false;
        if (asked_at_ && now - *asked_at_ < encoder_patience) return false;
        if (!forced_ && now < *wanted_) return false;
        asked_at_ = now;
        ++counts_.asked;
        return true;
    }

    // Asking the encoder failed: drop what was wanted.
    void ask_failed() {
        wanted_.reset();
        forced_ = false;
        asked_at_.reset();
        for (auto &[id, state] : clients_) state.waiting = false;
    }

    // An IDR of about `wire_bytes` (FEC and headers included) was read and goes to every stream of the channel. It answers
    // the clients that wait for one.
    void keyframe_sent(time_point now, std::size_t wire_bytes) {
        last_keyframe_ = now;
        keyframe_bytes_ = wire_bytes;
        wanted_.reset();
        forced_ = false;
        asked_at_.reset();
        for (auto &[id, state] : clients_) {
            if (!state.waiting) continue;
            state.waiting = false;
            state.seen = now;
        }
    }

    // How long after the last IDR a request of `client` was certainly sent before it could have it (rule 2).
    clock::duration blind_window(const Client &client) const {
        const auto rtt = std::min(client.rtt, rtt_cap);
        return idle_spread(client.kbps, client.fps, client.remote, keyframe_bytes_) + rtt * 3 / 4;
    }

    // A stream left the channel. What only it waited for isn't asked for any more (an IDR the encoder was asked for still
    // comes).
    void forget(std::uintptr_t id) {
        clients_.erase(id);
        if (std::none_of(clients_.begin(), clients_.end(), [](const auto &entry) { return entry.second.waiting; })) wanted_.reset();
    }

    Counts take_counts() { return std::exchange(counts_, {}); }

private:
    struct State {
        time_point seen{};     // its last request, or the IDR that answered it
        bool waiting = false;  // it asked, and no IDR has come since
    };

    void want(time_point due) { wanted_ = wanted_ ? std::min(*wanted_, due) : due; }

    void forget_old(time_point now) {
        std::erase_if(clients_, [&](const auto &entry) { return !entry.second.waiting && now - entry.second.seen > forget_after; });
    }

    std::optional<time_point> last_keyframe_;
    std::size_t keyframe_bytes_ = 0;
    std::optional<time_point> wanted_;  // when the waiting requests may be answered
    std::optional<time_point> asked_at_;
    bool forced_ = false;
    std::map<std::uintptr_t, State> clients_;
    Counts counts_{};
};

}
