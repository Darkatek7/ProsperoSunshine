// SPDX-License-Identifier: GPL-3.0-only
// When the web page says a stream is in trouble. Two signs, both read from what STREAMS already gives (stream_list.hpp), judged
// over a window of the last readings the page's polls left, not over one poll. Portable (tests/stream_health_test.cpp); the
// thresholds and where they come from:
//
//  Many lost frames. `recoveries` counts the client's requests for a new picture after a loss (patch 0015). A healthy
//  stream has one or two, at its start (measured: recoveries=1 at 3 s and 2 at 7 s, then none for minutes, docs/status-archive.md).
//  On a lossy path it feeds itself: issue 37 had 831 requests in 3.5 minutes, one every 0.25 s on average
//  (keyframe_policy.hpp). The host starts holding keyframes back when a client asks within 2 s of its last one. A request that
//  came in several readings is a different loss each time, one burst of three requests is one loss; so the sign is that the
//  count rose in at least kLossyRises of the readings of the last kWindow seconds, with at least kLossyTotal requests in all:
//  a loss about every 7 s or oftener. It needs kMinSpan seconds of readings and a stream older than kMinAge, which leaves
//  out the start's own requests.
//
//  Sending far above what was asked for. The host lowers the encoder's bitrate by 100/(100-fec) so that video and the repair
//  packets together stay within what the client asked (Sunshine rtsp.cpp, fec_percentage), so a busy picture sends about what
//  was asked and a still one much less. Above it is what repeated keyframes do: issue 37 sent 8 to 10 Mbit/s for 3.4. One
//  keyframe inside a two second reading adds about a fifth at that rate, so the sign is the mean of the readings of the last
//  kRateWindow seconds at kOverShare times the asked rate or more, and at least kOverExcess kbit/s above it. It doesn't
//  count where the encoder ignores the asked rate (10.x, experimental_list.h: ps_exp_short_record), where more than asked is
//  how the firmware works.
#pragma once
#include <cstddef>
#include <deque>
#include <iterator>
#include <map>
#include <set>
#include <string>

namespace prospero::web {

struct HealthInput {
    double now_s = 0;       // any steady clock, in seconds
    long long secs = 0;     // the stream's age
    int sent_kbps = -1;     // -1 while unknown
    int asked_kbps = 0;
    int recoveries = 0;
    bool starting = false;  // role "starting": the rate is what the client asked for, not yet what runs
    bool rate_honoured = true;  // false where the encoder keeps its own rate whatever is asked for
};

struct Health {
    bool lossy = false;
    bool over = false;
    std::string line;     // one plain sentence; empty when nothing is wrong
    std::string details;  // the numbers behind it
};

class HealthTracker {
public:
    static constexpr double kWindow = 30;      // seconds of readings kept
    static constexpr double kMinGap = 1.5;     // readings closer together than this count as one
    static constexpr double kMinSpan = 8;
    static constexpr long long kMinAge = 10;
    static constexpr int kLossyRises = 4;
    static constexpr int kLossyTotal = 5;
    static constexpr double kRateWindow = 12;
    static constexpr int kRateReadings = 4;
    static constexpr double kOverShare = 1.5;
    static constexpr int kOverExcess = 1000;

    // Adds the stream's reading and judges it. `key` tells streams apart (the device's uuid, else its address).
    Health update(const std::string &key, const HealthInput &in) {
        auto &readings = history_[key];
        // A counter that went back or an age that did: a new stream under the same key.
        if (!readings.empty() && (in.recoveries < readings.back().recoveries || in.secs < readings.back().secs)) readings.clear();
        if (readings.empty() || in.now_s - readings.back().at >= kMinGap || in.now_s < readings.back().at)
            readings.push_back({in.now_s, in.secs, in.sent_kbps, in.recoveries});
        while (readings.size() > 1 && in.now_s - readings.front().at > kWindow) readings.pop_front();

        Health health;
        if (in.secs < kMinAge) return health;
        std::string lossy_details, over_details;

        // Lost frames: how often the count rose, and by how much, over the window.
        if (readings.size() >= 2 && readings.back().at - readings.front().at >= kMinSpan) {
            int rises = 0;
            for (std::size_t i = 1; i < readings.size(); ++i)
                if (readings[i].recoveries > readings[i - 1].recoveries) ++rises;
            const int total = readings.back().recoveries - readings.front().recoveries;
            if (rises >= kLossyRises && total >= kLossyTotal) {
                health.lossy = true;
                lossy_details = "The device asked for a new picture " + std::to_string(total) + " times in the last " +
                                seconds(readings.back().at - readings.front().at) + " s (in " + std::to_string(rises) + " of " +
                                std::to_string(readings.size() - 1) + " checks).";
            }
        }

        // Sending: the mean of the readings in the rate window.
        if (in.rate_honoured && !in.starting && in.asked_kbps > 0) {
            double sum = 0;
            int count = 0;
            double from = in.now_s;
            for (const auto &r : readings) {
                if (in.now_s - r.at > kRateWindow || r.sent < 0) continue;
                sum += r.sent;
                from = from < r.at ? from : r.at;
                ++count;
            }
            if (count >= kRateReadings) {
                const double mean = sum / count;
                if (mean >= kOverShare * in.asked_kbps && mean - in.asked_kbps >= kOverExcess) {
                    health.over = true;
                    over_details = "The host sent " + std::to_string(static_cast<long long>(mean + 0.5)) + " kbit/s on average over the last " +
                                   seconds(in.now_s - from) + " s; the stream asked for " + std::to_string(in.asked_kbps) + " kbit/s.";
                }
            }
        }

        if (health.lossy) {
            health.line = "Many lost frames: the device keeps asking for a new picture, so the host keeps sending keyframes. "
                          "A lower resolution or bitrate helps.";
            if (health.over) health.line += " The host also sends far more video than was asked for.";
        } else if (health.over) {
            health.line = "The host sends far more video than was asked for. Keyframes sent again and again after lost frames can "
                          "cause that; a lower bitrate helps if the picture stutters.";
        }
        health.details = lossy_details + (lossy_details.empty() || over_details.empty() ? "" : " ") + over_details;
        return health;
    }

    // Forgets the streams that are gone.
    void keep_only(const std::set<std::string> &keys) {
        for (auto it = history_.begin(); it != history_.end();) it = keys.count(it->first) ? std::next(it) : history_.erase(it);
    }

private:
    struct Reading {
        double at;
        long long secs;
        int sent;
        int recoveries;
    };
    static std::string seconds(double value) { return std::to_string(static_cast<long long>(value + 0.5)); }
    std::map<std::string, std::deque<Reading>> history_;
};

}  // namespace prospero::web
