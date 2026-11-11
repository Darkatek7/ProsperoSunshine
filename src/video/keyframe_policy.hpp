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
//     A blind request counts in rule 3 like any other: within H of the IDR that answered the client's last request, as a
//     failure. One that crossed an IDR that arrived is one failure, and the level needs two in a row; a client whose every
//     request looks blind (all its IDRs lost, its round trip overestimated) is still held back.
//  3. A hold-off for clients whose IDRs don't hold. Each client has a level, 0 to 4. The first request a client sends
//     after the IDR that answered its last one judges that IDR: within H = W + 4 frame intervals of it (67 ms at 60 fps,
//     133 ms at 30) the IDR or the frames right behind it were lost, later it held. When two IDRs in a row failed the
//     level rises by one, with each that held it falls by one, and every 2 s without a request it falls by one (counted
//     from the IDR that answered it: while it waits it has no reason to ask). One failure alone could be an independent
//     loss that happened to come soon after; in simulations with random losses every 0.4 s on average on a LAN, single
//     verdicts drove the level up to 4 now and then, two in a row hardly ever (tests/keyframe_policy_test.cpp). The
//     encoder's own IDRs and those for others aren't judged, so a loss right after one of them is no sign that IDRs fail
//     on this client's path. At level 0 the request is answered at once: a single loss on a healthy link gets its IDR as
//     fast as before, and so does the first request after a quiet time. At level L an IDR for that client follows the last
//     one no sooner than min(max_gap, base x 2^(L-1)), base = max(100 ms, 2 x airtime), airtime = the last IDR's bytes on
//     the wire / the client's bitrate: how long the link the client chose needs for one IDR. So its IDRs take at most half
//     that link at level 1 and less above, down to two a second. A request that waits is answered when its time comes,
//     or by any IDR before (the encoder's own every 10 s, one for another client or a joining device). Random loss with
//     room to spare keeps the level low: an IDR fails only when the next loss happens to fall within H. A blind request
//     waits for the later of blind_hold and the hold-off.
//     max_gap is 500 ms, measured on the console (2026-11-11, 3.4 Mbit/s, 1024-byte packets): with 2 s, and 10 % of the
//     packets lost in bursts, an IDR held back for 1.9 s was lost as well and the next one held back again, and the
//     picture stood for up to 4.5 s, where answering every request at once (0.6.0) left it broken for 0.5 s. Issue 37's
//     loop was over without the hold-off: once large frames were paced (frame_pacing.hpp), the same stream through a
//     buffer of 39 packets asked for one IDR in 30 s. So the hold-off is only the limit for a path where paced IDRs still
//     fail, and it must not cost more than it saves: at most two IDRs a second for such a client, instead of up to five.
//     Issue 37's stream (3.4 Mbit/s, about 160 kB per IDR on the wire): airtime 380 ms, so while every IDR fails, IDRs at
//     most every 0.5 s from level 1 on. A LAN stream at 20 Mbit/s with 300 kB IDRs on the wire (240 kB, the largest the
//     repository measured, frame_pacing.hpp): 0.24 s, 0.48 s, then 0.5 s.
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
    static constexpr ms blind_hold{250};
    static constexpr int judge_frames = 4;
    static constexpr ms quiet{2000};
    static constexpr int max_level = 4;
    static constexpr ms min_base{100};
    static constexpr ms max_gap{500};
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
        blind,   // probably sent before the client could have the last IDR: answered blind_hold later unless one comes (rule 2)
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
        state.level = level_at(state, now);
        const bool blind = last_keyframe_ && now < *last_keyframe_ + blind_window(client);
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
        time_point due = last_keyframe_ && state.level > 0 ? std::max(now, *last_keyframe_ + gap(state)) : now;
        if (blind) due = std::max(due, now + blind_hold);
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

    // A stream left the channel. What only it waited for isn't asked for any more (an IDR the encoder was asked for still
    // comes).
    void forget(std::uintptr_t id) {
        clients_.erase(id);
        if (std::none_of(clients_.begin(), clients_.end(), [](const auto &entry) { return entry.second.waiting; })) wanted_.reset();
    }

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

    void want(time_point due) { wanted_ = wanted_ ? std::min(*wanted_, due) : due; }

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
