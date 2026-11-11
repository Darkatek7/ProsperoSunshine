// SPDX-License-Identifier: GPL-3.0-only
#include "video/keyframe_policy.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
#include <utility>
#include <vector>
using namespace prospero;
using namespace std::chrono_literals;
using clock_type = std::chrono::steady_clock;
using Outcome = KeyframePolicy::Outcome;

namespace {
const clock_type::time_point t0 = clock_type::time_point{} + 1h;

// Issue 37: a phone over 5G, 3.4 Mbit/s at 30 fps, round trip about 55 ms, IDRs of about 160 kB on the wire.
const KeyframePolicy::Client phone{1, 55ms, 3428, 30, true};
constexpr std::size_t phone_idr = 160'000;
// A PC on the LAN, 20 Mbit/s at 60 fps, round trip 2 ms, IDRs of about 300 kB on the wire (240 kB, the largest the
// repository measured).
const KeyframePolicy::Client pc{2, 2ms, 20'000, 60, false};
constexpr std::size_t pc_idr = 300'000;

// Moonlight without reference frame invalidation, as far as the host sees it: after a loss it asks for an IDR; it asks
// once more for a P-frame lost while the IDR is on its way (the host's blind window should take it: it reaches the host
// before the IDR could have reached the client, 20 ms after the IDR over the internet, within the few milliseconds of the
// window on a LAN); if the IDR is lost it asks again once the next frame is complete, and every 150 ms while it waits.
// Only an IDR it asked for gets such a crossing request: the loss behind it falls into the time it waited for the IDR.
struct LoopClient {
    KeyframePolicy::Client me;
    bool idrs_fail;
    std::vector<clock_type::time_point> pending;
    std::optional<clock_type::time_point> waiting_next;
    int requests = 0;
    bool asked = false;
    void ask(clock_type::time_point at) {
        pending.push_back(at);
        asked = true;
    }
    void keyframe(clock_type::time_point read_at, const KeyframePolicy &policy) {
        waiting_next.reset();
        pending.clear();
        if (std::exchange(asked, false)) pending.push_back(read_at + std::min<clock_type::duration>(20ms, policy.blind_window(me) / 2));
        if (idrs_fail) {
            const auto spread = policy.blind_window(me) - std::min(me.rtt, KeyframePolicy::rtt_cap) * 3 / 4;
            waiting_next = read_at + spread + me.rtt + 1s / me.fps;
        }
    }
    void tick(clock_type::time_point now, KeyframePolicy &policy) {
        for (auto it = pending.begin(); it != pending.end();) {
            if (*it > now) {
                ++it;
                continue;
            }
            policy.request(now, me);
            ++requests;
            it = pending.erase(it);
        }
        if (waiting_next && *waiting_next <= now) {
            policy.request(now, me);
            ++requests;
            asked = true;
            waiting_next = now + 150ms;
        }
    }
};

// The capture thread: polls, and when it asks, the IDR comes 2 frames later.
struct Channel {
    KeyframePolicy policy;
    std::size_t idr_bytes;
    std::optional<clock_type::time_point> idr_at;
    int idrs = 0;
    void tick(clock_type::time_point now) {
        if (idr_at && now >= *idr_at) {
            policy.keyframe_sent(*idr_at, idr_bytes);
            idr_at.reset();
            ++idrs;
        }
        if (policy.poll(now)) idr_at = now + 33ms;
    }
};
}

int main() {
    // A single loss on a healthy link: the encoder is asked at once, and the IDR answers it.
    {
        KeyframePolicy policy;
        assert(policy.request(t0, pc) == Outcome::now);
        assert(policy.poll(t0));
        // Further requests while the IDR is coming merge into it, and the encoder is not asked again.
        assert(policy.request(t0 + 10ms, pc) == Outcome::merged);
        assert(!policy.poll(t0 + 20ms));
        policy.keyframe_sent(t0 + 33ms, pc_idr);
        assert(!policy.poll(t0 + 40ms));
        // A loss a second later is the next single loss: at once again.
        assert(policy.request(t0 + 1s, pc) == Outcome::now);
        assert(policy.poll(t0 + 1s));
        const auto counts = policy.take_counts();
        assert(counts.requests == 3 && counts.merged == 1 && counts.asked == 2 && counts.blind == 0);
        assert(policy.take_counts().requests == 0);
    }

    // The encoder doesn't answer within 250 ms: the host asks again. Asking fails: the request is dropped.
    {
        KeyframePolicy policy;
        policy.request(t0, pc);
        assert(policy.poll(t0));
        assert(!policy.poll(t0 + 249ms));
        assert(policy.poll(t0 + 250ms));
        policy.ask_failed();
        assert(!policy.poll(t0 + 600ms));
        assert(policy.request(t0 + 700ms, pc) == Outcome::now);
    }

    // Rule 2: requests that left the client before it could have the last IDR are answered by it, or blind_hold later.
    {
        KeyframePolicy policy;
        policy.keyframe_sent(t0, phone_idr);
        const auto window = policy.blind_window(phone);
        // The IDR's spread over an internet path plus 3/4 of the round trip.
        assert(window > 3 * phone.rtt / 4 && window < 3 * phone.rtt / 4 + max_frame_delay);
        const auto blind_at = t0 + window - 1ms;
        assert(policy.request(blind_at, phone) == Outcome::blind);
        assert(!policy.poll(t0 + window));
        // The request may have been the client asking for the IDR it lost, while ENet's round trip is too high: it is
        // answered blind_hold after it.
        assert(!policy.poll(blind_at + KeyframePolicy::blind_hold - 1ms));
        assert(policy.poll(blind_at + KeyframePolicy::blind_hold));
        // A request after the window is answered at once.
        KeyframePolicy after;
        after.keyframe_sent(t0, phone_idr);
        assert(after.request(t0 + window + 1ms, phone) == Outcome::now);
        assert(after.poll(t0 + window + 1ms));
        // An IDR in between answers a blind request: it isn't asked for again.
        KeyframePolicy answered;
        answered.keyframe_sent(t0, phone_idr);
        assert(answered.request(t0 + 10ms, phone) == Outcome::blind);
        answered.keyframe_sent(t0 + 100ms, phone_idr);  // the encoder's own, or one for another device
        assert(!answered.poll(t0 + 10ms + KeyframePolicy::blind_hold) && !answered.poll(t0 + 2s));
        // A later request of the same client that is answered at once takes the blind one along.
        KeyframePolicy later;
        later.keyframe_sent(t0, phone_idr);
        assert(later.request(t0 + 10ms, phone) == Outcome::blind);
        assert(later.request(t0 + window + 1ms, phone) == Outcome::now);
        assert(later.poll(t0 + window + 1ms));
        later.keyframe_sent(t0 + window + 70ms, phone_idr);
        assert(!later.poll(t0 + 10ms + KeyframePolicy::blind_hold) && !later.poll(t0 + 2s));
        // The round trip counts up to 250 ms (ENet starts at 500 ms), and without one only the spread counts.
        KeyframePolicy::Client slow = phone, unknown = phone;
        slow.rtt = 500ms;
        unknown.rtt = 0ms;
        assert(policy.blind_window(slow) - policy.blind_window(unknown) == 250ms * 3 / 4);
        // On a LAN the pacer doesn't spread an IDR, only the wire limit (2 ms for 300 kB): the window is a few
        // milliseconds.
        KeyframePolicy lan;
        lan.keyframe_sent(t0, pc_idr);
        assert(lan.blind_window(pc) < 5ms);
        assert(lan.request(t0 + 5ms, pc) == Outcome::now);
    }

    // A real request that only looks blind: ENet's round trip stuck at 110 ms on a path of 55 ms (the host measures it
    // only from acknowledgements of its own reliable messages). Every request about a lost IDR then falls into the
    // window. It is answered blind_hold later instead of when the client gives up after 120 frames (4 s at 30 fps).
    {
        KeyframePolicy::Client stale = phone;
        stale.rtt = 110ms;
        KeyframePolicy policy;
        auto now = t0;
        // One lost IDR on an otherwise clean path. The frame behind the IDR waited for it and follows within a few ms, the
        // client asks once it is complete, and the request takes the true round trip.
        policy.keyframe_sent(now, phone_idr);
        const auto spread = policy.blind_window(stale) - stale.rtt * 3 / 4;
        const auto asks = now + spread + 10ms + 55ms;
        assert(asks < now + policy.blind_window(stale));
        assert(policy.request(asks, stale) == Outcome::blind);
        auto ask_at = asks;
        while (!policy.poll(ask_at)) ask_at += 1ms;
        assert(ask_at - asks == KeyframePolicy::blind_hold);

        // Every IDR lost, every request blind: one IDR per blind_hold plus the time the request and the encoder take.
        Channel ch{KeyframePolicy{}, phone_idr, {}, 0};
        std::optional<clock_type::time_point> next_ask = t0;
        std::optional<clock_type::time_point> last_idr;
        clock_type::duration shortest = 1h, longest{};
        for (now = t0; now < t0 + 30s; now += 1ms) {
            const int before = ch.idrs;
            ch.tick(now);
            if (ch.idrs != before) {
                if (last_idr) {
                    shortest = std::min(shortest, now - *last_idr);
                    longest = std::max(longest, now - *last_idr);
                }
                last_idr = now;
                next_ask = now + spread + 10ms + 55ms;
            }
            if (next_ask && *next_ask <= now) {
                ch.policy.request(now, stale);
                next_ask.reset();
            }
        }
        assert(shortest >= KeyframePolicy::blind_hold + 33ms && longest <= KeyframePolicy::blind_hold + 33ms + spread + 70ms);
        assert(ch.policy.take_counts().blind == ch.idrs && ch.idrs > 60 && ch.idrs < 80);
    }

    // Issue 37's loop: every IDR fails. The client asks once for a P-frame lost while the IDR is on its way, once it sees
    // that the IDR was lost (its spread, the round trip and a frame after it left), and every 150 ms while it waits, as
    // in the log. This is the most the two rules let through: the request that crossed the IDR looks blind and is taken
    // along by the one after the window, which is answered at once; an IDR every 164 ms, 6 a second, while the path
    // fails every one of them. A hold-off brought that down to two a second, and cost twice the time without a
    // picture on a path that only loses packets (keyframe_policy.hpp), so there is none.
    {
        LoopClient client{phone, true, {}, {}, 0};
        Channel ch{KeyframePolicy{}, phone_idr, {}, 0};
        auto now = t0;
        client.ask(now);
        std::optional<clock_type::time_point> first_idr, last_idr;
        std::vector<clock_type::duration> gaps;
        for (; now < t0 + 20s; now += 1ms) {
            const int before = ch.idrs;
            ch.tick(now);
            if (ch.idrs != before) {
                if (!first_idr) first_idr = now;
                if (last_idr) gaps.push_back(now - *last_idr);
                last_idr = now;
                client.keyframe(now, ch.policy);
            }
            client.tick(now, ch.policy);
        }
        // The first request is answered at once (the encoder delivers 2 frames later), and so is every one after it.
        assert(first_idr && *first_idr - t0 < 50ms);
        assert(client.requests > 200 && ch.idrs > 110 && ch.idrs < 130);
        for (const auto gap : gaps) assert(gap >= 150ms && gap <= 180ms);
        // Each IDR answered two requests: the one that crossed the IDR before it, and the one about that IDR's loss.
        const auto counts = ch.policy.take_counts();
        assert(counts.asked == ch.idrs && counts.blind >= ch.idrs - 2 && counts.requests == client.requests);

        // The path recovers (the pacer gets the IDRs through): the IDR the client waits for holds, and the client is quiet.
        client.idrs_fail = false;
        const int idrs = ch.idrs;
        while (ch.idrs == idrs) {
            now += 1ms;
            ch.tick(now);
            client.tick(now, ch.policy);
        }
        client.keyframe(now, ch.policy);
        for (const auto until = now + 5s; now < until; now += 1ms) {
            ch.tick(now);
            client.tick(now, ch.policy);
        }
        // Only the request that crossed that IDR followed, answered blind_hold later.
        assert(ch.idrs == idrs + 2);
        assert(ch.policy.request(now, phone) == Outcome::now);
    }

    // The same client on a path where IDRs hold: every request is answered at once. Each loss here also brings a request
    // that crossed its IDR; it is answered blind_hold later (the host can't tell it from a request about a lost IDR), so
    // each loss costs two IDRs.
    {
        LoopClient client{pc, false, {}, {}, 0};
        Channel ch{KeyframePolicy{}, pc_idr, {}, 0};
        auto now = t0;
        for (int loss = 0; loss < 20; ++loss) {
            client.ask(now);
            for (const auto until = now + 700ms; now < until; now += 1ms) {
                const int before = ch.idrs;
                ch.tick(now);
                if (ch.idrs != before) client.keyframe(now, ch.policy);
                client.tick(now, ch.policy);
            }
        }
        const auto counts = ch.policy.take_counts();
        assert(counts.blind == 20 && ch.idrs == 40);
    }

    // Random loss (a LAN at 20 Mbit/s, a loss every 400 ms on average): every request gets its IDR within the encoder's
    // two frames, or was answered by one on its way.
    {
        Channel ch{KeyframePolicy{}, pc_idr, {}, 0};
        std::mt19937 rng(37);
        std::exponential_distribution<double> next_loss(1.0 / 0.4);
        const auto after = [&](clock_type::time_point t) {
            return t + std::chrono::duration_cast<clock_type::duration>(std::chrono::duration<double>(next_loss(rng)));
        };
        auto now = t0;
        auto loss = after(now);
        std::optional<clock_type::time_point> asks, asked;
        clock_type::duration longest{};
        int requests = 0;
        for (; now < t0 + 120s; now += 1ms) {
            const int before = ch.idrs;
            ch.tick(now);
            if (ch.idrs != before && asked) {
                longest = std::max(longest, now - *asked);
                asked.reset();
            }
            if (now >= loss) {
                // The client asks when the next frame is complete, and the request reaches the host a round trip later.
                if (!asks) asks = now + 17ms;
                loss = after(now);
            }
            if (asks && *asks <= now) {
                const auto outcome = ch.policy.request(now, pc);
                if (!asked && outcome != Outcome::blind) asked = now;
                ++requests;
                asks.reset();
            }
        }
        const auto counts = ch.policy.take_counts();
        assert(requests > 200 && counts.requests == requests);
        assert(longest <= 35ms);
        // Hardly any request falls into a LAN's blind window of a few milliseconds.
        assert(counts.blind * 20 < counts.requests);
    }

    // The encoder's own IDR: a request within its blind window is answered by it or blind_hold later, one after the window
    // at once.
    {
        KeyframePolicy policy;
        policy.keyframe_sent(t0, pc_idr);
        assert(policy.request(t0 + 150ms, pc) == Outcome::now);
        assert(policy.poll(t0 + 150ms));
    }

    // Two devices on one channel: a request of the phone that waits for blind_hold doesn't hold back the PC's, and the
    // IDR for the PC answers the phone as well.
    {
        KeyframePolicy policy;
        policy.keyframe_sent(t0, phone_idr);
        assert(policy.request(t0 + 10ms, phone) == Outcome::blind);
        assert(!policy.poll(t0 + 10ms));
        assert(policy.request(t0 + 11ms, pc) == Outcome::now);
        assert(policy.poll(t0 + 11ms));
        policy.keyframe_sent(t0 + 44ms, phone_idr);
        assert(!policy.poll(t0 + 10ms + KeyframePolicy::blind_hold) && !policy.poll(t0 + 2s));
    }

    // A joining device's IDR: at once, also while a request waits, but not twice while one is on its way.
    {
        KeyframePolicy policy;
        policy.keyframe_sent(t0, phone_idr);
        const auto now = t0 + 10ms;
        assert(policy.request(now, phone) == Outcome::blind);
        assert(!policy.poll(now));
        policy.force();
        assert(policy.poll(now));
        policy.force();
        assert(!policy.poll(now + 10ms));
        policy.keyframe_sent(now + 33ms, phone_idr);
        assert(!policy.poll(now + 40ms) && !policy.poll(now + KeyframePolicy::blind_hold));
    }

    // A device that leaves while its request waits: nobody else waits, so no IDR is asked for it.
    {
        KeyframePolicy policy;
        policy.keyframe_sent(t0, phone_idr);
        assert(policy.request(t0 + 10ms, phone) == Outcome::blind);
        policy.forget(phone.id);
        assert(!policy.poll(t0 + 3s));
        // With another device still waiting, its request stays.
        KeyframePolicy two;
        two.keyframe_sent(t0, phone_idr);
        assert(two.request(t0 + 10ms, phone) == Outcome::blind);
        assert(two.request(t0 + 11ms, pc) == Outcome::now);
        two.forget(pc.id);
        assert(two.poll(t0 + 12ms));
    }

    // Odd inputs: no bitrate, no frame rate, a huge round trip and IDR.
    {
        KeyframePolicy policy;
        const KeyframePolicy::Client odd{9, 100'000ms, 0, 0, true};
        policy.keyframe_sent(t0, 50'000'000);
        assert(policy.blind_window(odd) == KeyframePolicy::rtt_cap * 3 / 4);
        auto now = t0;
        for (int i = 0; i < 8; ++i) {
            policy.request(now, odd);
            while (!policy.poll(now)) now += 5ms;
            now += 33ms;
            policy.keyframe_sent(now, 50'000'000);
            now += 10ms;
        }
        policy.request(now, odd);
        auto at = now;
        while (!policy.poll(at)) at += 1ms;
        assert(at - now <= KeyframePolicy::blind_hold);
    }

    // Clients that came and went over hours (their state is forgotten a minute after their last request): nothing piles
    // up that would change an answer.
    {
        KeyframePolicy policy;
        auto now = t0;
        for (std::uintptr_t id = 100; id < 400; ++id, now += 2min) {
            const KeyframePolicy::Client visitor{id, 20ms, 10'000, 60, true};
            assert(policy.request(now, visitor) == Outcome::now);
            assert(policy.poll(now));
            policy.keyframe_sent(now + 33ms, pc_idr);
        }
    }

    std::cout << "keyframe_policy_test: OK\n";
    return 0;
}
