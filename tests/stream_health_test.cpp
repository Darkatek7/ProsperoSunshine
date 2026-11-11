// SPDX-License-Identifier: GPL-3.0-only
#include "web/stream_health.hpp"
#include <cassert>
#include <iostream>
using namespace prospero::web;

namespace {
// One poll every 2 s for `seconds`, the stream `age` seconds old at the first; `recoveries(t)` and `sent(t)` give the readings.
template <class R, class S>
Health run(HealthTracker &tracker, const std::string &key, int seconds, long long age, int asked, R recoveries, S sent,
           bool honoured = true, bool starting = false) {
    Health last;
    for (int t = 0; t <= seconds; t += 2) {
        HealthInput in;
        in.now_s = 1000.0 + t;
        in.secs = age + t;
        in.asked_kbps = asked;
        in.recoveries = recoveries(t);
        in.sent_kbps = sent(t);
        in.rate_honoured = honoured;
        in.starting = starting;
        last = tracker.update(key, in);
    }
    return last;
}
}  // namespace

int main() {
    // A healthy stream: the two requests of its start (measured: recoveries 1 at 3 s and 2 at 7 s), a busy picture a bit under
    // what was asked.
    {
        HealthTracker t;
        const Health h = run(t, "a", 120, 0, 20000, [](int s) { return s < 3 ? 0 : s < 7 ? 1 : 2; }, [](int) { return 17000; });
        assert(!h.lossy && !h.over && h.line.empty() && h.details.empty());
    }
    // A still picture sends little: no fault.
    {
        HealthTracker t;
        const Health h = run(t, "a", 60, 20, 4148, [](int) { return 2; }, [](int) { return 1600; });
        assert(!h.lossy && !h.over);
    }
    // Issue 37: a request about every 0.25 s and 8 to 10 Mbit/s sent for 3.4 Mbit/s asked for.
    {
        HealthTracker t;
        const Health h = run(t, "a", 40, 20, 3400, [](int s) { return s * 4; }, [](int) { return 9000; });
        assert(h.lossy && h.over);
        assert(h.line.find("Many lost frames") == 0);
        assert(h.line.find("also sends far more") != std::string::npos);
        assert(h.details.find("times in the last") != std::string::npos && h.details.find("3400") != std::string::npos);
    }
    // Lost frames alone: a loss every 6 s, the rate fine.
    {
        HealthTracker t;
        const Health h = run(t, "a", 40, 20, 20000, [](int s) { return s / 6; }, [](int) { return 18000; });
        assert(h.lossy && !h.over);
        assert(h.line.find("lower resolution or bitrate") != std::string::npos);
    }
    // One burst is one loss, however many requests it made; so is a loss every 15 s.
    {
        HealthTracker t;
        const Health h = run(t, "a", 40, 20, 20000, [](int s) { return s >= 20 ? 6 : 0; }, [](int) { return 18000; });
        assert(!h.lossy);
        HealthTracker u;
        assert(!run(u, "a", 120, 20, 20000, [](int s) { return s / 15; }, [](int) { return 18000; }).lossy);
    }
    // Sending too much alone, with the lost frames low.
    {
        HealthTracker t;
        const Health h = run(t, "a", 30, 20, 10000, [](int) { return 1; }, [](int) { return 16000; });
        assert(!h.lossy && h.over);
        assert(h.line.find("far more video") != std::string::npos);
        // 20 % above is a keyframe in the window or the rate control, not trouble; a small ask doesn't count either.
        HealthTracker u;
        assert(!run(u, "a", 30, 20, 10000, [](int) { return 1; }, [](int) { return 12000; }).over);
        HealthTracker v;
        assert(!run(v, "a", 30, 20, 500, [](int) { return 1; }, [](int) { return 900; }).over);
    }
    // One reading far above is a keyframe, not a trend.
    {
        HealthTracker t;
        const Health h = run(t, "a", 30, 20, 3400, [](int) { return 1; }, [](int s) { return s == 20 ? 9000 : 2500; });
        assert(!h.over);
    }
    // Where the encoder keeps its own rate, and in the first seconds, more than asked is no sign.
    {
        HealthTracker t;
        assert(!run(t, "a", 30, 20, 3400, [](int) { return 1; }, [](int) { return 9000; }, false).over);
        HealthTracker u;
        assert(!run(u, "a", 30, 20, 3400, [](int) { return 1; }, [](int) { return 9000; }, true, true).over);
        HealthTracker v;
        const Health h = run(v, "a", 8, 0, 3400, [](int s) { return s * 4; }, [](int) { return 9000; });
        assert(!h.lossy && !h.over);  // under kMinAge
    }
    // Unknown rates (-1) are left out; no asked rate means nothing to compare.
    {
        HealthTracker t;
        assert(!run(t, "a", 30, 20, 20000, [](int) { return 1; }, [](int) { return -1; }).over);
        HealthTracker u;
        assert(!run(u, "a", 30, 20, 0, [](int) { return 1; }, [](int) { return 9000; }).over);
    }
    // The trouble ends: the line goes when the window holds no more of it.
    {
        HealthTracker t;
        run(t, "a", 30, 20, 3400, [](int s) { return s * 4; }, [](int) { return 9000; });
        Health h;
        for (int s = 32; s <= 80; s += 2) {
            HealthInput in;
            in.now_s = 1000.0 + s;
            in.secs = 20 + s;
            in.asked_kbps = 3400;
            in.recoveries = 120;
            in.sent_kbps = 3000;
            h = t.update("a", in);
        }
        assert(!h.lossy && !h.over && h.line.empty());
    }
    // Two browsers polling at once don't count one loss twice: readings less than kMinGap apart are one.
    {
        HealthTracker t;
        Health h;
        for (int s = 0; s <= 40; s += 2) {
            for (double extra : {0.0, 0.3}) {
                HealthInput in;
                in.now_s = 1000.0 + s + extra;
                in.secs = 20 + s;
                in.asked_kbps = 20000;
                in.recoveries = s >= 20 ? 6 : 0;
                in.sent_kbps = 18000;
                h = t.update("a", in);
            }
        }
        assert(!h.lossy);
    }
    // A new stream under the same key (the counter goes back) starts from nothing; streams are kept apart; gone ones are forgotten.
    {
        HealthTracker t;
        run(t, "a", 40, 20, 3400, [](int s) { return s * 4; }, [](int) { return 9000; });
        const Health fresh = run(t, "a", 4, 20, 3400, [](int) { return 0; }, [](int) { return 3000; });
        assert(!fresh.lossy && !fresh.over);
        HealthTracker u;
        run(u, "bad", 40, 20, 3400, [](int s) { return s * 4; }, [](int) { return 9000; });
        assert(!run(u, "good", 40, 20, 3400, [](int) { return 1; }, [](int) { return 3000; }).lossy);
        u.keep_only({"good"});
        // "bad" starts again: one reading can't judge.
        assert(!run(u, "bad", 0, 20, 3400, [](int s) { return s * 4 + 200; }, [](int) { return 9000; }).lossy);
    }
    std::cout << "PASS stream_health_test\n";
}
