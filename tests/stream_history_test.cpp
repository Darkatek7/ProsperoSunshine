// SPDX-License-Identifier: GPL-3.0-only
#include "control/stream_history.hpp"
#include <cassert>
#include <iostream>
using namespace prospero;

namespace {
StreamHistory::Live live(const std::string &key, long long seconds, const std::string &mode = "1920x1080@60") {
    StreamHistory::Live out;
    out.key = key;
    out.now.seconds = seconds;
    out.now.uuid = "UUID-A";
    out.now.name = "Living room PC";
    out.now.mode = mode;
    out.now.video_format = 1;
    out.now.hdr = true;
    out.now.kbps = 20000;
    out.now.sent_kbps = 14200;
    out.now.rtt_ms = 3;
    out.now.recoveries = 2;
    out.now.missing_frames = 5;
    return out;
}
}  // namespace

int main() {
    // The line, and back.
    StreamRecord record = live("a", 754).now;
    record.ended = 1795600000;
    record.internet = true;
    const std::string line = history_line(record);
    assert(line == "ended=1795600000 secs=754 codec=HEVC hdr=1 mode=1920x1080@60 kbps=20000 sent_kbps=14200 rtt_ms=3 "
                   "recoveries=2 missing_frames=5 internet=1 uuid=UUID-A name=Living room PC");
    const auto back = parse_history_line(line + "\r\n");
    assert(back && history_line(*back) == line);
    assert(back->ended == 1795600000 && back->seconds == 754 && back->video_format == 1 && back->hdr && back->internet);
    assert(back->uuid == "UUID-A" && back->name == "Living room PC" && back->mode == "1920x1080@60");
    // The log's line names neither the device nor the time.
    assert(ended_line(record) == "Stream ended: secs=754 codec=HEVC hdr=1 mode=1920x1080@60 kbps=20000 sent_kbps=14200 "
                                 "rtt_ms=3 recoveries=2 missing_frames=5 internet=1");

    // Unknown values are "-", a name keeps its spaces and loses its line breaks, an unpaired client has no uuid.
    StreamRecord bare;
    bare.ended = 7;
    bare.name = "a = b\nname=c";
    const std::string bare_line = history_line(bare);
    assert(bare_line == "ended=7 secs=0 codec=H.264 hdr=0 mode=- kbps=0 sent_kbps=- rtt_ms=- recoveries=0 missing_frames=0 "
                        "internet=0 uuid=- name=a = b name=c");
    const auto bare_back = parse_history_line(bare_line);
    assert(bare_back && bare_back->uuid.empty() && bare_back->name == "a = b name=c" && bare_back->sent_kbps == -1 &&
           bare_back->rtt_ms == -1 && bare_back->mode == "-");
    StreamRecord odd = bare;
    odd.uuid = "two words";
    assert(history_line(odd).find(" uuid=- ") != std::string::npos);

    // What is no record.
    assert(!parse_history_line(""));
    assert(!parse_history_line("garbage"));
    assert(!parse_history_line("ended=1 secs=2"));  // no name
    assert(!parse_history_line("ended=x secs=2 name=a"));
    assert(!parse_history_line("ended=1 secs=-2 name=a"));
    assert(!parse_history_line("ended=1234567890123456789 secs=2 name=a"));  // 19 digits
    // Numbers beyond an int stay an int; words missing take their defaults.
    const auto wide = parse_history_line("ended=1 secs=2 kbps=99999999999 recoveries=99999999999 name=");
    assert(wide && wide->kbps == 2147483647 && wide->recoveries == 2147483647 && wide->name.empty() && wide->video_format == 0);

    // A stream that is gone becomes a record with what it last showed.
    StreamHistory history;
    assert(history.observe({live("a", 1)}, 100).empty());
    assert(history.observe({live("a", 2)}, 101).empty());
    assert(history.records().empty());
    auto ended = history.observe({}, 130);
    assert(ended.size() == 1 && ended[0].ended == 130 && ended[0].seconds == 2 && ended[0].name == "Living room PC");
    assert(history.records().size() == 1);
    assert(history.observe({}, 131).empty());  // once

    // What a later look no longer knows stays: the mode after the capture is gone, the round trip, the device.
    StreamHistory::Live fading = live("b", 40, "-");
    fading.now.rtt_ms = -1;
    fading.now.sent_kbps = -1;
    fading.now.uuid.clear();
    fading.now.name.clear();
    fading.now.video_format = 0;
    fading.now.hdr = false;
    fading.now.kbps = 5000;
    fading.now.recoveries = 1;  // a counter never goes back in a record
    assert(history.observe({live("b", 39)}, 200).empty());
    assert(history.observe({fading}, 201).empty());
    ended = history.observe({}, 202);
    assert(ended.size() == 1);
    assert(ended[0].mode == "1920x1080@60" && ended[0].video_format == 1 && ended[0].hdr && ended[0].kbps == 20000);
    assert(ended[0].rtt_ms == 3 && ended[0].sent_kbps == 14200 && ended[0].uuid == "UUID-A" && ended[0].recoveries == 2);
    assert(ended[0].seconds == 40);

    // The same device's next stream is another key: the first ends while the second runs.
    assert(history.observe({live("c1", 10)}, 300).empty());
    ended = history.observe({live("c2", 1)}, 301);
    assert(ended.size() == 1 && ended[0].seconds == 10);
    // Two that end in the same look: the one that ran longer first. A stream without a key is not followed.
    assert(history.observe({live("c2", 5), live("d", 9), live("", 3)}, 302).empty());
    ended = history.observe({}, 303);
    assert(ended.size() == 2 && ended[0].seconds == 9 && ended[1].seconds == 5);

    // The file keeps the last kKeep, oldest first, and reads back the same; a damaged line loses only itself.
    StreamHistory many;
    for (int i = 0; i < 25; ++i) {
        many.observe({live("k" + std::to_string(i), i)}, 1000 + i);
        many.observe({}, 2000 + i);
    }
    assert(many.records().size() == StreamHistory::kKeep);
    assert(many.records().front().seconds == 5 && many.records().back().seconds == 24);
    const std::string text = many.text();
    StreamHistory read;
    read.load("no record\n" + text + "ended=broken\n\n");
    assert(read.text() == text);
    read.load(text + text);
    assert(read.records().size() == StreamHistory::kKeep && read.text() == text);
    read.load("");
    assert(read.records().empty());

    std::cout << "PASS stream_history_test\n";
}
