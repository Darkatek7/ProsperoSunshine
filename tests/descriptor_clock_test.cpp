// SPDX-License-Identifier: GPL-3.0-only
#include "video/descriptor_clock.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
using prospero::DescriptorClock;

namespace {
// A descriptor of `size` bytes as 13.00's ReadVideo fills it at 59.94 fps (docs/avcap2-descriptor.md): 240 kHz times at
// 0x08 and 0x10, the frame number at 0x20, the TSC stamp at `stamp_at`, the size word at 0.
std::vector<uint8_t> descriptor(size_t size, uint64_t frame, size_t stamp_at, uint64_t tsc_step = 26'640'000) {
    std::vector<uint8_t> d(size, 0);
    auto put = [&](size_t at, uint64_t v) { std::memcpy(d.data() + at, &v, 8); };
    put(0, size);
    put(0x08, 1'000'000 + (frame + 1) * 4004);
    put(0x10, 1'000'000 + frame * 4004);
    put(0x20, frame + 1);
    put(0x38, 0x780ull << 32);
    put(0x40, 0x438);
    put(stamp_at, 5'000'000'000ull + frame * tsc_step);
    return d;
}

bool has(const std::string &s, const std::string &part) { return s.find(part) != std::string::npos; }
}  // namespace

int main() {
    // H.264 at 60 fps: the two 240 kHz words step by 4004, the counter by 1, the stamp by the TSC's step; constant words are
    // left out, and the first frame's words come with it.
    {
        DescriptorClock clock(5, 0);
        std::optional<DescriptorClock::Report> report;
        for (uint64_t f = 0; f < 5; f++) {
            const auto d = descriptor(0x70, f, 0x48);
            const int64_t read = 1'000'000'000 + static_cast<int64_t>(f) * 16'683'000;
            report = clock.add(d, read, read - 4'000'000, false);
            assert(f == 4 || !report);
        }
        assert(report);
        std::cout << report->steps << "\n" << report->words << "\n";
        assert(has(report->steps, "5 frames, read 16.68/16.68/16.68 ms, sent 16.68/16.68/16.68 ms, fallback 0; steps"));
        assert(has(report->steps, " 08:4004..4004 10:4004..4004 20:1..1 48:26640000..26640000"));
        assert(!has(report->steps, " 00:") && !has(report->steps, " 38:") && !has(report->steps, " 40:"));
        assert(has(report->words, "00:0000000000000070 08:00000000000f51e4"));  // 1'000'000 + 4004
        assert(!has(report->words, " 70:"));                                     // 0x70 bytes: 14 words
        assert(clock.reports() == 1);
    }
    // HEVC's 0x88 descriptor: all 17 words, the stamp at 0x58; uneven reads and timestamps that fell back show.
    {
        DescriptorClock clock(4, 0);
        std::optional<DescriptorClock::Report> report;
        const int64_t reads[] = {0, 10'000'000, 40'000'000, 50'000'000};
        for (uint64_t f = 0; f < 4; f++) {
            const auto d = descriptor(0x88, f, 0x58);
            report = clock.add(d, reads[f], reads[f], f % 2 == 1);
        }
        assert(report);
        std::cout << report->steps << "\n";
        assert(has(report->steps, "read 10.00/16.67/30.00 ms, sent 10.00/16.67/30.00 ms, fallback 2;"));
        assert(has(report->steps, " 58:26640000..26640000"));
        assert(has(report->words, " 80:0000000000000000"));
    }
    // A step backwards (a counter starting anew) shows as a negative step; a short descriptor uses its whole words only.
    {
        DescriptorClock clock(3, 0);
        std::optional<DescriptorClock::Report> report;
        const uint64_t frames[] = {7, 8, 2};
        for (uint64_t f : frames) report = clock.add(descriptor(0x70, f, 0x48), 0, 0, false);
        assert(report && has(report->steps, " 20:-6..1"));
        assert(!clock.add(std::vector<uint8_t>(5, 0xff), 0, 0, false));  // no whole word: nothing read, nothing crashes
    }
    // After a report it waits `pause` frames, then makes the next; reset() starts at once, with no step across it.
    {
        DescriptorClock clock(2, 3);
        int reports = 0;
        for (uint64_t f = 0; f < 2 + 3 + 2; f++) reports += clock.add(descriptor(0x70, f, 0x48), 0, 0, false).has_value();
        assert(reports == 2 && clock.reports() == 2);
        clock.add(descriptor(0x70, 0, 0x48), 0, 0, false);  // waiting
        clock.reset();
        assert(!clock.add(descriptor(0x70, 100, 0x48), 0, 0, false));
        const auto report = clock.add(descriptor(0x70, 101, 0x48), 0, 0, false);
        assert(report && has(report->steps, " 20:1..1"));
    }
    // Nothing changed: "none".
    {
        DescriptorClock clock(2, 0);
        const std::vector<uint8_t> d(0x70, 0);
        clock.add(d, 0, 0, true);
        const auto report = clock.add(d, 0, 0, true);
        assert(report && has(report->steps, "fallback 2; steps none"));
    }
    std::cout << "PASS descriptor_clock_test\n";
}
