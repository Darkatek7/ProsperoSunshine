// SPDX-License-Identifier: GPL-3.0-only
// Which of the clients' keyframe (IDR) requests the capture passes on to the encoder, and when. Portable, so that the
// tests can run it; video_ps5.cpp keeps one per capture channel, under captures.mutex, for every stream on the channel.
//
// What the client does (moonlight-common-c, read in the pinned source): the host announces no reference frame
// invalidation, so after any loss Moonlight drops every frame until an IDR arrives. It asks for one once it received the
// next complete frame (asking while everything is lost would only add to the congestion), asks again after every further
// loss followed by a complete frame while it waits, and on its own after 120 dropped frames (2 s at 60 fps). Avcap2 sends
// the IDR 2 to 3 frames after the host asks (R1). An IDR is many average frames, and on a channel that several devices
// share (joined streams) it goes to all of them.
// On a lossy path this feeds itself. Issue 37: 831 requests in 3.5 minutes, nine in ten of them 100 to 300 ms apart, and
// the host sent 8 to 10 Mbit/s for a 3.4 Mbit/s stream; every IDR lost its tail on the way (frame_pacing.hpp), and the
// client asked again. While an IDR is on its way, every new loss also brings a request that the IDR already answers.
// The rules:
//
//  1. One IDR on its way answers every request before it. After the host asked the encoder, requests merge until an IDR
//     is read. After 250 ms without one (3 frames at 30 fps are 100 ms) the host asks again.
//  2. A request that reaches the host within W after the last IDR left was sent before the client could have that IDR,
//     and the IDR answers it. W = the IDR's spread by the pacer (frame_pacing.hpp) + 3/4 of the client's smoothed
//     round-trip time, at most 250 ms of it. A request because that IDR was lost arrives no earlier than its spread + one
//     frame interval (the client needs the next frame complete) + the true round trip, so W = spread + round trip would be
//     safe with an exact estimate; ENet's smoothed value lags when a queue drains, hence 3/4, and it starts at 500 ms
//     before its first measurement, hence the cap. Without a round trip W is the spread alone. A request swallowed wrongly
//     costs that client the wait until its next loss or its 120-frame limit; one let through costs an IDR, which rule 3
//     holds back.
//  3. A hold-off for clients whose IDRs don't hold. Each client has a level, 0 to 4. The first request a client sends
//     after the IDR that answered its last one judges that IDR: within H = W + 4 frame intervals of it (67 ms at 60 fps,
//     133 ms at 30) the IDR or the frames right behind it were lost, later it held. When two IDRs in a row failed the
//     level rises by one, with each that held it falls by one, and every 2 s without a request it falls by one (counted
//     from the IDR that answered it: while it waits it has no reason to ask). One failure alone could be an independent
//     loss that happened to come soon after; in simulations with random losses every 0.4 s on average on a LAN, single
//     verdicts drove the level up to 4 now and then, two in a row hardly ever (tests/keyframe_policy_test.cpp). The encoder's own IDRs and those for others aren't judged, so a loss right
//     after one of them is no sign that IDRs fail on this client's path. At level 0 the request
//     is answered at once: a single loss on a healthy link gets its IDR as fast as before, and so does the first request
//     after a quiet time. At level L an IDR for that client follows the last one no sooner than
//     min(2 s, base x 2^(L-1)), base = max(100 ms, 2 x airtime), airtime = the last IDR's bytes on the wire / the client's
//     bitrate: how long the link the client chose needs for one IDR. So its IDRs take at most half that link at level 1,
//     a quarter at level 2, an eighth at level 3; 2 s is how long the client itself waits before it asks again on its own.
//     A request that waits is answered when its time comes, or by any IDR before (the encoder's own every 10 s, one for
//     another client or a joining device). Random loss with room to spare keeps the level low: an IDR fails only when the
//     next loss happens to fall within H.
//     Issue 37's stream (3.4 Mbit/s, about 160 kB per IDR on the wire): airtime 380 ms, so while every IDR fails, IDRs at
//     most every 0.75 s, 1.5 s, then 2 s, instead of up to five a second. A LAN stream at 20 Mbit/s with 360 kB IDRs:
//     0.29 s, 0.58 s, 1.15 s, 2 s.
//     The level is each client's own, so that a device on a bad link doesn't hold back the IDRs of one on a good link
//     that joined the same channel; the hold-off counts from the last IDR of the channel, whoever it was for.
//  4. A device that joins a running channel needs an IDR to start: forced, only rule 1 applies.
//  5. When asking the encoder fails, the request is dropped as before: the encoder's own IDR follows within 10 s.
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
    static constexpr int judge_frames = 4;
    static constexpr ms quiet{2000};
    static constexpr int max_level = 4;
    static constexpr ms min_base{100};
    static constexpr ms max_gap{2000};
    static constexpr ms forget_after{60'000};

    // The client that asks: who it is (any number that tells the streams of the channel apart), its control
    // connection's smoothed round-trip time (0 while unknown), and its stream's bitrate, frame rate and whether it
    // streams over the internet, which give an IDR's spread and airtime.
    struct Client {
        std::uintptr_t id = 0;
        ms rtt{0};
        std::uint32_t kbps = 0;
        int fps = 60;
        bool remote = false;
    };

    enum class Outcome {
        now,     // the encoder is asked at the next poll
        waits,   // held back until the client's hold-off has passed (rule 3)
        merged,  // an IDR is already wanted or on its way from the encoder (rule 1)
        blind,   // sent before the client could have the last IDR (rule 2)
    };

    // For the log: what happened to the requests since the last take_counts().
    struct Counts {
        int requests = 0, merged = 0, blind = 0, waited = 0;
        int asked = 0;  // times the encoder was asked
        clock::duration longest_wait{};  // the longest hold-off a request got
    };

    Outcome request(time_point now, const Client &client) {
        ++counts_.requests;
        forget_old(now);
        State &state = clients_[client.id];
        state.client = client;
        if (asked_at_) {
            state.waiting = true;
            ++counts_.merged;
            return Outcome::merged;
        }
        if (last_keyframe_ && now < *last_keyframe_ + blind_window(client)) {
            ++counts_.blind;
            return Outcome::blind;
        }
        state.level = level_at(state, now);
        if (state.answered && state.answered == last_keyframe_) {
            if (now - *last_keyframe_ < blind_window(client) + judge_frames * frame_interval(client)) {
                if (++state.failed_in_row >= 2) state.level = std::min(max_level, state.level + 1);
            } else {
                state.failed_in_row = 0;
                state.level = std::max(0, state.level - 1);
            }
            state.answered.reset();
        }
        state.quiet_from = now;
        state.waiting = true;
        const time_point due = last_keyframe_ && state.level > 0 ? std::max(now, *last_keyframe_ + gap(state)) : now;
        const bool merged = wanted_.has_value();
        wanted_ = merged ? std::min(*wanted_, due) : due;
        if (merged) {
            ++counts_.merged;
            return Outcome::merged;
        }
        if (due > now) {
            ++counts_.waited;
            counts_.longest_wait = std::max(counts_.longest_wait, due - now);
            return Outcome::waits;
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
            state.answered = now;
            state.quiet_from = now;
        }
    }

    // How long after the last IDR a request of `client` was certainly sent before it could have it (rule 2).
    clock::duration blind_window(const Client &client) const {
        const auto rtt = std::min(client.rtt, rtt_cap);
        return idle_spread(client.kbps, client.fps, client.remote, keyframe_bytes_) + rtt * 3 / 4;
    }

    // The highest level of the clients as it stands at `now`, for the log.
    int level(time_point now) const {
        int highest = 0;
        for (const auto &[id, state] : clients_) highest = std::max(highest, level_at(state, now));
        return highest;
    }
    int level(std::uintptr_t id, time_point now) const {
        const auto it = clients_.find(id);
        return it == clients_.end() ? 0 : level_at(it->second, now);
    }

    // A stream left the channel.
    void forget(std::uintptr_t id) { clients_.erase(id); }

    Counts take_counts() { return std::exchange(counts_, {}); }

private:
    struct State {
        Client client;
        int level = 0;
        std::optional<time_point> quiet_from;  // its last request that rules 1 and 2 let through, or the IDR that answered it
        int failed_in_row = 0;  // IDRs that answered it and failed, one after the other
        bool waiting = false;  // it asked, and no IDR has come since
        std::optional<time_point> answered;  // the IDR that answered it, until its next request judges it
    };

    static clock::duration frame_interval(const Client &client) {
        return std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / (client.fps > 0 ? client.fps : 60)));
    }

    static int level_at(const State &state, time_point now) {
        if (state.waiting || !state.quiet_from || now - *state.quiet_from < quiet) return state.level;
        return std::max(0, state.level - static_cast<int>((now - *state.quiet_from) / quiet));
    }

    clock::duration gap(const State &state) const {
        if (state.level <= 0) return clock::duration::zero();
        clock::duration airtime{};
        if (state.client.kbps > 0)
            airtime = std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(static_cast<double>(keyframe_bytes_) * 8.0 / (state.client.kbps * 1000.0)));
        const clock::duration base = std::max<clock::duration>(min_base, 2 * airtime);
        return std::min<clock::duration>(max_gap, base * (1 << (state.level - 1)));
    }

    void forget_old(time_point now) {
        std::erase_if(clients_, [&](const auto &entry) {
            return !entry.second.waiting && entry.second.quiet_from && now - *entry.second.quiet_from > forget_after;
        });
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
