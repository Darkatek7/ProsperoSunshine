// SPDX-License-Identifier: GPL-3.0-only
// What the words of Avcap2's frame descriptor do over a stream's frames, for the debug log: on a firmware the host wasn't
// measured on, which word holds the encoder's 240 kHz clock and which the capture's TSC stamp, and whether the timestamps
// the host hands Sunshine come evenly (issue 61: HEVC on 11.40 played unevenly on two clients with no loss).
//
// What is known (docs/avcap2-descriptor.md): ReadVideo's descriptor (H.264, 0x70 bytes) has the TSC stamp at 0x48 and
// ReadVideo2's (HEVC, 0x88 bytes) at 0x58, both measured on 13.00 only. ps5-web-stream reads ReadVideo's 0x08 and 0x10 as
// presentation and decode time on a 240 kHz clock: 4004 ticks are one frame at 59.94 fps, the step 13.00 showed there.
// Where ReadVideo2 keeps them is not measured.
//
// DescriptorClock collects `window` frames and then makes a report: for each 64-bit word that changed, the smallest and
// the largest step from one frame to the next (a 240 kHz clock steps by 4004 at 59.94 fps and 2002 at 119.88; the TSC
// by about 26.6 million at 59.94 fps), for the read times and the timestamps sent the smallest, average and largest gap,
// how many timestamps fell back to the read time (capture_time in video_ps5.cpp: no stamp, or one that isn't a TSC of the
// last second), and the words of the window's first frame. Words that stayed put are left out of the steps. After a
// report it waits `pause` frames before the next window; reset() (a reopened capture) starts a window at once.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>

namespace prospero {

class DescriptorClock {
public:
    static constexpr size_t max_words = 0x88 / 8;

    struct Report {
        std::string steps;  // "300 frames: read 14.20/16.68/19.31 ms, sent ..., fallback 0; steps 08:4004..4004 ..."
        std::string words;  // the window's first descriptor, "00:0000000000000088 08:..."
    };

    explicit DescriptorClock(int window = 300, int pause = 18000) : window_(std::max(window, 2)), pause_(std::max(pause, 0)) {}

    void reset() {
        frames_ = 0;
        waiting_ = 0;
        count_ = 0;
    }

    // One frame read: its descriptor (whole 64-bit words of it are used, at most max_words), when it was read and the
    // timestamp handed on, both in ns on one clock, and whether that timestamp is the read time for want of a stamp.
    std::optional<Report> add(std::span<const uint8_t> descriptor, int64_t read_ns, int64_t sent_ns, bool fell_back) {
        if (waiting_ > 0) {
            waiting_--;
            return std::nullopt;
        }
        std::array<uint64_t, max_words> cur{};
        const size_t n = std::min(descriptor.size() / 8, max_words);
        std::memcpy(cur.data(), descriptor.data(), n * 8);
        if (frames_ == 0) {
            words_ = n;
            first_ = cur;
            steps_.fill(Range{});
            read_ = Gaps{};
            sent_ = Gaps{};
            fallbacks_ = 0;
        } else {
            for (size_t i = 0; i < words_; i++) steps_[i].add(static_cast<int64_t>(cur[i] - prev_[i]));
            read_.add(read_ns - prev_read_);
            sent_.add(sent_ns - prev_sent_);
        }
        fallbacks_ += fell_back;
        prev_ = cur;
        prev_read_ = read_ns;
        prev_sent_ = sent_ns;
        if (++frames_ < window_) return std::nullopt;
        Report report{line(), first_words()};
        frames_ = 0;
        waiting_ = pause_;
        count_++;
        return report;
    }

    int reports() const { return count_; }

private:
    struct Range {
        bool seen = false;
        int64_t low = 0, high = 0;
        void add(int64_t v) {
            low = seen ? std::min(low, v) : v;
            high = seen ? std::max(high, v) : v;
            seen = true;
        }
    };
    struct Gaps {
        int64_t low = std::numeric_limits<int64_t>::max(), high = std::numeric_limits<int64_t>::min(), sum = 0, count = 0;
        void add(int64_t ns) {
            low = std::min(low, ns);
            high = std::max(high, ns);
            sum += ns;
            count++;
        }
    };

    static void append(std::string &out, const char *format, auto... args) {
        char buf[96];
        const int len = std::snprintf(buf, sizeof buf, format, args...);
        if (len > 0) out.append(buf, static_cast<size_t>(std::min<int>(len, sizeof buf - 1)));
    }

    static void gaps(std::string &out, const char *what, const Gaps &g) {
        if (!g.count) return;
        append(out, ", %s %.2f/%.2f/%.2f ms", what, static_cast<double>(g.low) / 1e6,
               static_cast<double>(g.sum) / static_cast<double>(g.count) / 1e6, static_cast<double>(g.high) / 1e6);
    }

    std::string line() const {
        std::string out;
        append(out, "%d frames", window_);
        gaps(out, "read", read_);
        gaps(out, "sent", sent_);
        append(out, ", fallback %d; steps", fallbacks_);
        bool any = false;
        for (size_t i = 0; i < words_; i++) {
            const Range &r = steps_[i];
            if (!r.seen || (r.low == 0 && r.high == 0)) continue;
            append(out, " %02zx:%lld..%lld", i * 8, static_cast<long long>(r.low), static_cast<long long>(r.high));
            any = true;
        }
        if (!any) out += " none";
        return out;
    }

    std::string first_words() const {
        std::string out;
        for (size_t i = 0; i < words_; i++)
            append(out, "%s%02zx:%016llx", i ? " " : "", i * 8, static_cast<unsigned long long>(first_[i]));
        return out;
    }

    int window_, pause_;
    int frames_ = 0, waiting_ = 0, count_ = 0, fallbacks_ = 0;
    size_t words_ = 0;
    std::array<uint64_t, max_words> first_{}, prev_{};
    std::array<Range, max_words> steps_{};
    Gaps read_, sent_;
    int64_t prev_read_ = 0, prev_sent_ = 0;
};

}  // namespace prospero
