// SPDX-License-Identifier: GPL-3.0-only
// The audio line of the debug log (src/audio/audio_meter.hpp): what it counts, when it comes, and that it starts anew.
#include "audio/audio_meter.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>

using namespace prospero;
using namespace std::chrono_literals;

int main() {
    const auto t0 = AudioMeter::Clock::now();
    AudioMeter meter;

    // The first call sets the clock, and nothing comes before the time has passed.
    assert(!meter.line(t0, 1));
    assert(!meter.line(t0 + 4999ms, 1));

    // Two stereo packets of four frames: one silent, one with a peak of 0.5 on the right channel.
    const std::vector<float> quiet(8, 0.0f);
    const std::vector<float> loud{0.0f, -0.5f, 0.0f, 0.5f, 0.0f, -0.5f, 0.0f, 0.5f};
    meter.read(quiet.data(), 4, 2);
    meter.read(loud.data(), 4, 2);
    meter.delivered(6);
    meter.filled(2);
    // 4 of 16 samples at 0.5: rms = sqrt(4 * 0.25 / 16) = 0.25.
    const auto first = meter.line(t0 + 5s, 1);
    assert(first);
    assert(*first == "PS5 audio: secs=5.0 packets=2 frames=8 peak=0.5000 rms=0.2500 streams=1 delivered=6 silence=2");

    // The counters start anew: a stretch in which the console gave nothing and the host filled in silence.
    assert(!meter.line(t0 + 6s, 2));
    meter.filled(480);
    meter.filled(480);
    const auto second = meter.line(t0 + 10500ms, 2);
    assert(second);
    assert(*second == "PS5 audio: secs=5.5 packets=0 frames=0 peak=0.0000 rms=0.0000 streams=2 delivered=0 silence=960");

    // A packet of silence is told from none: packets and frames count, the level stays 0.
    meter.read(quiet.data(), 4, 2);
    const auto third = meter.line(t0 + 16s, 0);
    assert(third);
    assert(*third == "PS5 audio: secs=5.5 packets=1 frames=4 peak=0.0000 rms=0.0000 streams=0 delivered=0 silence=0");

    // Samples that are no numbers or far over full scale show, and stay within the line.
    const std::vector<float> odd{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 1e30f, -3.0f};
    meter.read(odd.data(), 2, 2);
    meter.read(nullptr, 0, 2);
    meter.read(quiet.data(), 4, -1);
    const auto fourth = meter.line(t0 + 30s, 1);
    assert(fourth);
    assert(fourth->find("packets=2 frames=2 peak=9.9999 rms=9.9999") != std::string::npos);
    assert(fourth->size() < 200);

    // Twelve channels: frames count once, the level over all channels.
    std::vector<float> bed(24, 0.0f);
    bed[13] = 1.0f;
    meter.read(bed.data(), 2, 12);
    const auto fifth = meter.line(t0 + 35s, 1);
    assert(fifth);
    assert(fifth->find("packets=1 frames=2 peak=1.0000 rms=0.2041") != std::string::npos);

    // Another interval can be asked for.
    assert(!meter.line(t0 + 35s + 900ms, 1, 1s));
    assert(meter.line(t0 + 36s, 1, 1s));

    std::puts("audio_meter_test: OK");
    return 0;
}
