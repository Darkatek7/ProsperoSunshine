// SPDX-License-Identifier: GPL-3.0-only
#include "control/stream_list.hpp"
#include <cassert>
#include <iostream>
using namespace prospero;

int main() {
    assert(codec_name(0) == "H.264");
    assert(codec_name(1) == "HEVC");
    assert(codec_name(2) == "AV1");

    StreamFacts first;
    first.cert = "cert-a";
    first.address = "192.168.1.20";
    first.width = 1280;
    first.height = 720;
    first.fps = 30;
    first.kbps = 10000;
    first.video_format = 1;
    first.hdr = true;
    first.seconds = 754;
    first.pads = 1;

    // No capture yet: what the client asked for.
    assert(stream_line(first, {}, "UUID-A", "Living room PC") ==
           "OK uuid=UUID-A address=192.168.1.20 codec=HEVC hdr=1 mode=1280x720@30 kbps=10000 secs=754 pads=1 "
           "role=starting channel=- sent_kbps=- rtt_ms=- recoveries=0 name=Living room PC");

    // The stream that holds the capture: what is delivered.
    CaptureFacts capture;
    capture.cert = "cert-a";
    capture.joined = {"cert-b"};
    capture.width = 1920;
    capture.height = 1080;
    capture.fps = 60;
    capture.kbps = 20000;
    capture.video_format = 1;
    capture.hdr = false;
    assert(stream_line(first, {capture}, "UUID-A", "Living room PC") ==
           "OK uuid=UUID-A address=192.168.1.20 codec=HEVC hdr=0 mode=1920x1080@60 kbps=20000 secs=754 pads=1 "
           "role=capture channel=0 sent_kbps=- rtt_ms=- recoveries=0 name=Living room PC");

    // A device that joined gets the same picture, whatever it asked for.
    StreamFacts second;
    second.cert = "cert-b";
    second.address = "::ffff:192.168.1.21";
    second.width = 1280;
    second.height = 800;
    second.fps = 90;
    second.kbps = 5000;
    second.seconds = 5;
    second.pads = 2;
    assert(stream_line(second, {capture}, "UUID-B", "Steam Deck") ==
           "OK uuid=UUID-B address=192.168.1.21 codec=HEVC hdr=0 mode=1920x1080@60 kbps=20000 secs=5 pads=2 "
           "role=joined channel=0 sent_kbps=- rtt_ms=- recoveries=0 name=Steam Deck");

    // A second channel next to the first: the device that holds it and one that joined it get its own mode.
    CaptureFacts second_channel;
    second_channel.channel = 1;
    second_channel.cert = "cert-d";
    second_channel.joined = {"cert-e"};
    second_channel.width = 1280;
    second_channel.height = 720;
    second_channel.fps = 30;
    second_channel.kbps = 8000;
    second_channel.video_format = 0;
    StreamFacts fourth;
    fourth.cert = "cert-d";
    fourth.video_format = 1;
    fourth.hdr = true;
    assert(stream_line(fourth, {capture, second_channel}, "UUID-D", "Phone") ==
           "OK uuid=UUID-D address=- codec=H.264 hdr=0 mode=1280x720@30 kbps=8000 secs=0 pads=0 role=capture channel=1 sent_kbps=- rtt_ms=- recoveries=0 name=Phone");
    StreamFacts fifth;
    fifth.cert = "cert-e";
    assert(stream_line(fifth, {capture, second_channel}, "UUID-E", "Tablet").find(" mode=1280x720@30 ") != std::string::npos);
    assert(stream_line(fifth, {capture, second_channel}, "UUID-E", "Tablet").find(" role=joined channel=1 ") != std::string::npos);
    // The first channel's streams stay on it.
    assert(stream_line(second, {capture, second_channel}, "UUID-B", "Steam Deck").find(" role=joined channel=0 ") != std::string::npos);

    // Neither holding nor joined (it is still setting up, or about to be turned away): its own values.
    StreamFacts third;
    third.cert = "cert-c";
    third.video_format = 0;
    third.hdr = true;  // 10 bit means nothing for H.264
    third.seconds = -3;
    third.pads = -1;
    assert(stream_line(third, {capture}, "", "") ==
           "OK uuid=- address=- codec=H.264 hdr=0 mode=- kbps=0 secs=0 pads=0 role=starting channel=- sent_kbps=- rtt_ms=- recoveries=0 name=");

    // A session without a certificate never counts as the capture's.
    StreamFacts anonymous;
    CaptureFacts empty_cert;
    assert(stream_line(anonymous, {empty_cert}, "X", "a").find(" role=starting ") != std::string::npos);

    // A name stays on one line; an IPv6 address stays as it is.
    StreamFacts v6;
    v6.address = "fe80::1";
    assert(stream_line(v6, {}, "U", "two\nlines\r") ==
           "OK uuid=U address=fe80::1 codec=H.264 hdr=0 mode=- kbps=0 secs=0 pads=0 role=starting channel=- sent_kbps=- rtt_ms=- recoveries=0 name=two lines ");

    // What reaches the network.
    StreamFacts measured = v6;
    measured.sent_kbps = 18250;
    measured.rtt_ms = 3;
    measured.recoveries = 2;
    assert(stream_line(measured, {}, "U", "pc").find(" sent_kbps=18250 rtt_ms=3 recoveries=2 name=pc") != std::string::npos);
    measured.recoveries = -4;
    assert(stream_line(measured, {}, "U", "pc").find(" recoveries=0 ") != std::string::npos);

    // The rate: 2.5 MB in 2 s is 10 Mbit/s; too short an interval tells nothing.
    assert(sent_rate_kbps(2'500'000, 2.0) == 10000);
    assert(sent_rate_kbps(0, 5.0) == 0);
    assert(sent_rate_kbps(1'000'000, 0.2) == -1);

    // The debug log's line: the same facts without who it is, and what changed since the last line.
    StreamFacts logged = first;
    logged.sent_kbps = 19876;
    logged.rtt_ms = 4;
    logged.recoveries = 3;
    logged.missing_frames = 12;
    assert(stats_line(1, logged, {capture}, 2, 8) ==
           "Stream stats: stream=1 role=capture channel=0 codec=HEVC hdr=0 mode=1920x1080@60 kbps=20000 sent_kbps=19876 rtt_ms=4 "
           "recoveries=3(+1) missing_frames=12(+4) pads=1 secs=754");
    assert(stats_line(2, StreamFacts{}, {}, -1, 0) ==
           "Stream stats: stream=2 role=starting channel=- codec=H.264 hdr=0 mode=- kbps=0 sent_kbps=- rtt_ms=- recoveries=0(+0) "
           "missing_frames=0(+0) pads=0 secs=0");
    assert(stats_line(1, logged, {capture}, 0, 0).find("192.168") == std::string::npos);

    std::cout << "PASS: stream list (what each device asked for, what each capture channel delivers, unknown values, one line, "
                 "what reaches the network, the debug log's stats line)\n";
    return 0;
}
