// SPDX-License-Identifier: GPL-3.0-only
// What the host's game audio did over a stretch of time, for one line in the debug log: how much the console delivered,
// how loud it was, and how much went on to the streams as sound and as silence the host filled in. A stream without sound
// then shows where the sound ends: the console gave none (peak 0), the channel was lost (silence filled in), or it left
// the host and got lost on the way.
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>

namespace prospero {

class AudioMeter {
public:
    using Clock = std::chrono::steady_clock;

    // A packet read from the console: frames of `channels` interleaved float samples.
    void read(const float *samples, size_t frames, int channels) {
        if (channels <= 0) return;
        const size_t count = frames * static_cast<size_t>(channels);
        for (size_t i = 0; i < count; ++i) {
            // A sample that isn't a number counts as full scale, so that it shows.
            const double value = std::isfinite(samples[i]) ? std::fabs(static_cast<double>(samples[i])) : 1.0;
            peak_ = std::max(peak_, value);
            squares_ += value * value;
        }
        samples_ += count;
        frames_ += frames;
        packets_++;
    }
    // Frames a stream took: the console's sound, or silence in its place.
    void delivered(size_t frames) { delivered_ += frames; }
    void filled(size_t frames) { filled_ += frames; }

    // The line once `every` has passed since the last one, and the counters start anew. The first call only sets the
    // clock. streams: how many streams take the audio now.
    // "PS5 audio: secs=5.0 packets=235 frames=240640 peak=0.5012 rms=0.0312 streams=1 delivered=240000 silence=0"
    std::optional<std::string> line(Clock::time_point now, int streams, std::chrono::milliseconds every = std::chrono::seconds(5)) {
        if (!started_) {
            started_ = true;
            reset(now);
            return std::nullopt;
        }
        if (now - since_ < every) return std::nullopt;
        const double seconds = std::chrono::duration<double>(now - since_).count();
        const double rms = samples_ ? std::sqrt(squares_ / static_cast<double>(samples_)) : 0.0;
        char text[224];
        std::snprintf(text, sizeof text,
                      "PS5 audio: secs=%.1f packets=%zu frames=%zu peak=%.4f rms=%.4f streams=%d delivered=%zu silence=%zu", seconds,
                      packets_, frames_, std::min(peak_, 9.9999), std::min(rms, 9.9999), streams, delivered_, filled_);
        reset(now);
        return std::string(text);
    }

private:
    void reset(Clock::time_point now) {
        since_ = now;
        peak_ = squares_ = 0.0;
        samples_ = frames_ = packets_ = delivered_ = filled_ = 0;
    }

    Clock::time_point since_{};
    bool started_ = false;
    double peak_ = 0.0, squares_ = 0.0;
    size_t samples_ = 0, frames_ = 0, packets_ = 0, delivered_ = 0, filled_ = 0;
};

}  // namespace prospero
