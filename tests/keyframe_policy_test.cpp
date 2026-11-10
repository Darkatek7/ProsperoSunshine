// SPDX-License-Identifier: GPL-3.0-only
#include "video/keyframe_policy.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
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
// A PC on the LAN, 20 Mbit/s at 60 fps, round trip 2 ms, IDRs of about 360 kB.
const KeyframePolicy::Client pc{2, 2ms, 20'000, 60, false};
constexpr std::size_t pc_idr = 360'000;

// Moonlight without reference frame invalidation, as far as the host sees it: after a loss it asks for an IDR; it asks
// once more for a P-frame lost while the IDR is on its way (the host's blind window should take it); if the IDR is lost it
// asks again once the next frame is complete, and every 150 ms while it waits.
struct LoopClient {
    KeyframePolicy::Client me;
    bool idrs_fail;
    std::vector<clock_type::time_point> pending;
    std::optional<clock_type::time_point> waiting_next;
    int requests = 0;
    void ask(clock_type::time_point at) { pending.push_back(at); }
    void keyframe(clock_type::time_point read_at, const KeyframePolicy &policy) {
        waiting_next.reset();
        pending.clear();
        pending.push_back(read_at + 20ms);
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
        assert(counts.requests == 3 && counts.merged == 1 && counts.asked == 2 && counts.waited == 0);
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

    // Rule 2: requests that left the client before it could have the last IDR are answered by it.
    {
        KeyframePolicy policy;
        policy.keyframe_sent(t0, phone_idr);
        const auto window = policy.blind_window(phone);
        // The IDR's spread over an internet path plus 3/4 of the round trip.
        assert(window > 3 * phone.rtt / 4 && window < 3 * phone.rtt / 4 + max_frame_delay);
        assert(policy.request(t0 + window - 1ms, phone) == Outcome::blind);
        assert(!policy.poll(t0 + window));
        assert(policy.request(t0 + window + 1ms, phone) == Outcome::now);
        // The round trip counts up to 250 ms (ENet starts at 500 ms), and without one only the spread counts.
        KeyframePolicy::Client slow = phone, unknown = phone;
        slow.rtt = 500ms;
        unknown.rtt = 0ms;
        assert(policy.blind_window(slow) - policy.blind_window(unknown) == 250ms * 3 / 4);
        // On a LAN a small IDR leaves at once: the window is a few milliseconds.
        KeyframePolicy lan;
        lan.keyframe_sent(t0, 50'000);
        assert(lan.blind_window(pc) < 5ms);
        assert(lan.request(t0 + 5ms, pc) == Outcome::now);
    }

    // Issue 37's loop: every IDR fails. The client asks once a P-frame lost while the IDR is on its way, once it sees
    // that the IDR was lost (its spread, the round trip and a frame after it left), and every 150 ms while it waits, as
    // in the log. Today that made up to five IDRs a second; the hold-off brings it down to one in 2 s.
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
        assert(client.requests > 100);
        // The first request is answered at once (the encoder delivers 2 frames later), and so is the second, since one
        // failure alone may be chance. Then the gaps double from twice the airtime (160 kB at 3.4 Mbit/s: 0.37 s) to 2 s.
        assert(first_idr && *first_idr - t0 < 50ms);
        assert(gaps.size() >= 8);
        assert(gaps[0] < 400ms);
        assert(gaps[1] >= 740ms && gaps[1] < 850ms);
        assert(gaps[2] >= 1490ms && gaps[2] < 1600ms);
        for (std::size_t i = 3; i < gaps.size(); ++i) assert(gaps[i] >= 2s && gaps[i] <= 2100ms);
        assert(ch.idrs < 14);
        assert(ch.policy.level(phone.id, now) == KeyframePolicy::max_level);
        const auto counts = ch.policy.take_counts();
        assert(counts.blind > 0 && counts.waited > 0 && counts.longest_wait <= 2s);

        // The path recovers (the pacer gets the IDRs through): the IDR the client waits for holds, the client is quiet,
        // and its level falls by one every 2 s from then on; while it waited it didn't fall. 8 s later a loss gets its IDR
        // at once again.
        client.idrs_fail = false;
        const int idrs = ch.idrs;
        while (ch.idrs == idrs) {
            assert(ch.policy.level(phone.id, now) == KeyframePolicy::max_level);
            now += 1ms;
            ch.tick(now);
            client.tick(now, ch.policy);
        }
        client.keyframe(now, ch.policy);
        const auto answered = now;
        assert(ch.policy.level(phone.id, answered + 1900ms) == 4);
        assert(ch.policy.level(phone.id, answered + 2s) == 3);
        assert(ch.policy.level(phone.id, answered + 7s) == 1);
        assert(ch.policy.level(phone.id, answered + 8s) == 0);
        for (; now < answered + 8s; now += 1ms) {
            ch.tick(now);
            client.tick(now, ch.policy);
        }
        assert(ch.policy.request(now, phone) == Outcome::now);
    }

    // The same client on a path where IDRs hold: every request is answered at once.
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
        assert(counts.waited == 0 && ch.idrs == 20);
    }

    // An IDR that holds lowers the level: the client's next loss comes well after it.
    {
        KeyframePolicy policy;
        auto now = t0;
        clock_type::time_point idr{};
        // Five IDRs; the first four fail (the next request comes right after the blind window). The first failure alone
        // leaves the level at 0, each one after it raises it.
        for (int i = 0; i < 5; ++i) {
            policy.request(now, phone);
            while (!policy.poll(now)) now += 5ms;
            idr = now + 33ms;
            policy.keyframe_sent(idr, phone_idr);
            now = idr + policy.blind_window(phone) + 20ms;
        }
        assert(policy.level(phone.id, now) == 3);
        // The fifth held: the next request comes a second after it.
        assert(policy.request(idr + 1s, phone) == Outcome::waits);
        assert(policy.level(phone.id, idr + 1s) == 2);
    }

    // Random loss with room to spare (a LAN at 20 Mbit/s, a loss every 400 ms on average): most IDRs hold, the level stays
    // low and nearly every request is answered at once.
    {
        Channel ch{KeyframePolicy{}, pc_idr, {}, 0};
        std::mt19937 rng(37);
        std::exponential_distribution<double> next_loss(1.0 / 0.4);
        const auto after = [&](clock_type::time_point t) {
            return t + std::chrono::duration_cast<clock_type::duration>(std::chrono::duration<double>(next_loss(rng)));
        };
        auto now = t0;
        auto loss = after(now);
        std::optional<clock_type::time_point> asks;
        int requests = 0, highest = 0;
        for (; now < t0 + 120s; now += 1ms) {
            ch.tick(now);
            if (now >= loss) {
                // The client asks when the next frame is complete, and the request reaches the host a round trip later.
                if (!asks) asks = now + 17ms;
                loss = after(now);
            }
            if (asks && *asks <= now) {
                ch.policy.request(now, pc);
                ++requests;
                asks.reset();
            }
            highest = std::max(highest, ch.policy.level(pc.id, now));
        }
        const auto counts = ch.policy.take_counts();
        assert(requests > 200);
        assert(highest <= 2);
        // Fewer than one request in twenty waits, and none for long.
        assert(counts.waited * 20 < counts.requests);
        assert(counts.longest_wait < 600ms);
    }

    // The encoder's own IDR: a request within its blind window is answered by it, one after it at once, also when it
    // comes soon after it, since that IDR didn't answer this client.
    {
        KeyframePolicy policy;
        policy.keyframe_sent(t0, pc_idr);
        assert(policy.request(t0 + 150ms, pc) == Outcome::now);
        assert(policy.level(pc.id, t0 + 150ms) == 0);
        // A request waiting for its hold-off is answered by the encoder's own IDR as well.
        KeyframePolicy busy;
        auto now = t0;
        for (int i = 0; i < 4; ++i) {
            busy.request(now, phone);
            while (!busy.poll(now)) now += 5ms;
            now += 33ms;
            busy.keyframe_sent(now, phone_idr);
            now += busy.blind_window(phone) + 20ms;
        }
        assert(busy.request(now, phone) == Outcome::waits);
        assert(!busy.poll(now));
        busy.keyframe_sent(now + 100ms, phone_idr);  // the periodic one
        assert(!busy.poll(now + 3s));
    }

    // Two devices on one channel: the phone's bad link holds back its own IDRs, not those of the PC on the LAN.
    {
        LoopClient client{phone, true, {}, {}, 0};
        Channel ch{KeyframePolicy{}, phone_idr, {}, 0};
        auto now = t0;
        client.ask(now);
        for (; now < t0 + 10s; now += 1ms) {
            const int before = ch.idrs;
            ch.tick(now);
            if (ch.idrs != before) client.keyframe(now, ch.policy);
            client.tick(now, ch.policy);
        }
        assert(ch.policy.level(phone.id, now) == KeyframePolicy::max_level);
        // The phone waits for its hold-off; the PC's single loss is answered at once, and its IDR answers the phone too.
        while (ch.policy.request(now, phone) != Outcome::waits) {
            now += 1ms;
            const int before = ch.idrs;
            ch.tick(now);
            if (ch.idrs != before) client.keyframe(now, ch.policy);
        }
        assert(!ch.policy.poll(now));
        assert(ch.policy.request(now + 1ms, pc) == Outcome::merged);
        assert(ch.policy.poll(now + 1ms));
        assert(ch.policy.level(pc.id, now + 1ms) == 0);
        ch.policy.forget(phone.id);
        assert(ch.policy.level(phone.id, now) == 0);
    }

    // A joining device's IDR: at once, whatever the level, but not twice while one is on its way.
    {
        KeyframePolicy policy;
        auto now = t0;
        for (int i = 0; i < 4; ++i) {
            policy.request(now, phone);
            while (!policy.poll(now)) now += 5ms;
            now += 33ms;
            policy.keyframe_sent(now, phone_idr);
            now += policy.blind_window(phone) + 20ms;
        }
        assert(policy.request(now, phone) == Outcome::waits);
        assert(!policy.poll(now));
        policy.force();
        assert(policy.poll(now));
        policy.force();
        assert(!policy.poll(now + 10ms));
        policy.keyframe_sent(now + 33ms, phone_idr);
        assert(!policy.poll(now + 40ms));
    }

    // A client unknown for a minute is forgotten.
    {
        KeyframePolicy policy;
        for (int i = 0; i < 3; ++i) {
            policy.request(t0 + i * 300ms, phone);
            policy.poll(t0 + i * 300ms);
            policy.keyframe_sent(t0 + i * 300ms + 33ms, phone_idr);
        }
        policy.request(t0 + 2min, pc);
        assert(policy.level(phone.id, t0 + 2min) == 0);
    }

    std::cout << "keyframe_policy_test: OK\n";
    return 0;
}
