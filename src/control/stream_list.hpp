// SPDX-License-Identifier: GPL-3.0-only
// The control socket's STREAMS line for one stream. Sunshine knows each session's client and what it
// asked for; the PS5 capture knows what is delivered: up to two capture channels, and a device that
// joined a channel gets the mode, codec and bit rate of the stream that holds it (video_ps5.cpp).
#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace prospero {

// One Sunshine session, as rtsp_stream::session_infos() gives it.
struct StreamFacts {
    std::string cert;
    std::string address;
    int width = 0, height = 0, fps = 0;
    int kbps = 0;  // what the client asked for, kbit/s
    int video_format = 0;  // 0 H.264, 1 HEVC, 2 AV1
    bool hdr = false;  // the client asked for 10 bit
    long long seconds = 0;  // since the session started
    int pads = 0;  // virtual gamepads of the client
    // What reaches the network (patch 0015): -1 while unknown.
    int sent_kbps = -1;  // video kbit/s sent lately
    int rtt_ms = -1;  // round-trip time of the control connection
    int recoveries = 0;  // times the client asked for a new picture after a loss
    int missing_frames = 0;  // frames the client reported lost (patch 0025)
};

// kbit/s from a count of bytes sent over some seconds; -1 when there's too little time to tell. A counter that jumped (a
// difference of two that went back) gives the largest int, not a conversion that overflows.
inline int sent_rate_kbps(unsigned long long bytes, double seconds) {
    if (!(seconds >= 0.5)) return -1;  // also NaN
    const double rate = static_cast<double>(bytes) * 8.0 / 1000.0 / seconds + 0.5;
    return rate >= 2147483647.0 ? 2147483647 : static_cast<int>(rate);
}

// A PS5 capture channel while a stream holds it.
struct CaptureFacts {
    int channel = 0;  // 0 the first, 1 the second
    std::string cert;  // the stream that holds it
    std::vector<std::string> joined;  // the streams that get its picture
    int width = 0, height = 0, fps = 0;
    int kbps = 0;  // the average it opened with
    int video_format = 0;
    bool hdr = false;
};

inline std::string codec_name(int video_format) {
    return video_format == 1 ? "HEVC" : video_format == 2 ? "AV1" : "H.264";
}

// What a stream shows: what the client asked for until it holds or joined a capture channel, then that channel's codec,
// dynamic range, mode and bit rate.
struct Shown {
    std::string role = "starting";  // capture, joined or starting
    std::string channel = "-";
    std::string mode = "-";  // WxH@fps
    int kbps = 0;
    int video_format = 0;
    bool hdr = false;
};

inline Shown shown(const StreamFacts &stream, const std::vector<CaptureFacts> &captures) {
    Shown out;
    int width = stream.width, height = stream.height, fps = stream.fps;
    out.kbps = stream.kbps;
    out.video_format = stream.video_format;
    out.hdr = stream.hdr && stream.video_format != 0;
    for (const CaptureFacts &capture : captures) {
        if (stream.cert.empty()) break;
        const bool holds = capture.cert == stream.cert;
        if (!holds && std::find(capture.joined.begin(), capture.joined.end(), stream.cert) == capture.joined.end()) continue;
        out.role = holds ? "capture" : "joined";
        out.channel = std::to_string(capture.channel);
        width = capture.width;
        height = capture.height;
        fps = capture.fps;
        out.kbps = capture.kbps;
        out.video_format = capture.video_format;
        out.hdr = capture.hdr;
        break;
    }
    if (width > 0 && height > 0 && fps > 0) out.mode = std::to_string(width) + 'x' + std::to_string(height) + '@' + std::to_string(fps);
    return out;
}

// "OK uuid=<uuid> address=<ip> codec=<H.264|HEVC|AV1> hdr=<0|1> mode=<WxH@fps> kbps=<n> secs=<n> pads=<n>
//  role=<capture|joined|starting> channel=<0|1|-> sent_kbps=<n|-> rtt_ms=<n|-> recoveries=<n> name=<name>". The name comes
// last because it may hold spaces; "-" stands for an unknown uuid, address, mode, channel, rate or round-trip time. role: capture holds a capture channel, joined gets its picture,
// starting has neither yet (its values are what it asked for). channel: the capture channel it holds or joined, whose
// codec, dynamic range, mode and bit rate the line shows then. sent_kbps, rtt_ms and recoveries are what reaches the
// network: the video sent lately, the control connection's round-trip time, and how often the client lost a picture.
inline std::string stream_line(const StreamFacts &stream, const std::vector<CaptureFacts> &captures, const std::string &uuid,
                               const std::string &name) {
    const Shown show = shown(stream, captures);
    // A name is one line; the app takes the rest of it.
    std::string clean_name = name;
    std::replace_if(clean_name.begin(), clean_name.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
    // An IPv4 client on a dual-stack socket: its plain address, as STATUS gives it.
    std::string address = stream.address;
    if (address.rfind("::ffff:", 0) == 0 && address.find('.') != std::string::npos) address.erase(0, 7);
    return "OK uuid=" + (uuid.empty() ? "-" : uuid) + " address=" + (address.empty() ? "-" : address) +
           " codec=" + codec_name(show.video_format) + " hdr=" + (show.hdr ? "1" : "0") + " mode=" + show.mode +
           " kbps=" + std::to_string(std::max(0, show.kbps)) + " secs=" + std::to_string(std::max(0LL, stream.seconds)) +
            " pads=" + std::to_string(std::max(0, stream.pads)) + " role=" + show.role + " channel=" + show.channel +
           " sent_kbps=" + (stream.sent_kbps < 0 ? std::string("-") : std::to_string(stream.sent_kbps)) +
           " rtt_ms=" + (stream.rtt_ms < 0 ? std::string("-") : std::to_string(stream.rtt_ms)) +
           " recoveries=" + std::to_string(std::max(0, stream.recoveries)) + " name=" + clean_name;
}

// The log line for one stream every few seconds while the log level is debug (main_ps5.cpp): the same facts as its
// STREAMS line without who it is (no uuid, address or name), plus the frames the client reported lost (patch 0025) and,
// in brackets, how many recoveries and lost frames came since the last line. `number` counts the running streams from 1.
inline std::string stats_line(int number, const StreamFacts &stream, const std::vector<CaptureFacts> &captures,
                              int recoveries_before, int missing_before) {
    const Shown show = shown(stream, captures);
    const auto with_change = [](int now, int before) {
        return std::to_string(std::max(0, now)) + "(+" + std::to_string(std::max(0, now - std::max(0, before))) + ")";
    };
    return "Stream stats: stream=" + std::to_string(number) + " role=" + show.role + " channel=" + show.channel +
           " codec=" + codec_name(show.video_format) + " hdr=" + (show.hdr ? "1" : "0") + " mode=" + show.mode +
           " kbps=" + std::to_string(std::max(0, show.kbps)) +
           " sent_kbps=" + (stream.sent_kbps < 0 ? std::string("-") : std::to_string(stream.sent_kbps)) +
           " rtt_ms=" + (stream.rtt_ms < 0 ? std::string("-") : std::to_string(stream.rtt_ms)) +
           " recoveries=" + with_change(stream.recoveries, recoveries_before) +
           " missing_frames=" + with_change(stream.missing_frames, missing_before) +
           " pads=" + std::to_string(std::max(0, stream.pads)) + " secs=" + std::to_string(std::max(0LL, stream.seconds));
}

}
