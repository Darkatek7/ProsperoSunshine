// SPDX-License-Identifier: GPL-3.0-only
// The streams that ended lately, for the web page (issue 63): which device, at what mode, for how long, and how the path
// did. The host looks at its running sessions every second (ControlServer::note_streams); a session that is gone becomes
// a record with what it last showed. The last kKeep records are kept in <appdata>/stream_history.txt, one line each, so
// they outlast a host restart. Portable (tests/stream_history_test.cpp, tests/fuzz/web_lines_fuzz.cpp).
//
// Where the numbers come from: `recoveries` are the client's requests for a new picture after a loss and `missing_frames`
// the frames it reported lost with them (Sunshine patches 0015 and 0025). The host sees nothing else of the path: a
// client that asks for a whole new picture each time names no frames, so its missing_frames stay 0 while recoveries rise.
#pragma once
#include "control/stream_list.hpp"
#include <algorithm>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prospero {

struct StreamRecord {
    long long ended = 0;  // the console's clock, seconds since 1970
    long long seconds = 0;  // how long it ran
    std::string uuid;  // empty: not a paired device when it was seen
    std::string name;
    std::string mode = "-";  // WxH@fps, of the capture channel it held or joined
    int video_format = 0;
    bool hdr = false;
    int kbps = 0;  // asked for
    int sent_kbps = -1;  // video sent, the mean over the stream; -1 unknown
    int rtt_ms = -1;  // the control connection's round trip, the last known; -1 unknown
    int recoveries = 0;
    int missing_frames = 0;
    bool internet = false;  // the device asked for Moonlight's small packets
};

namespace history_detail {
// A whole number of at most 18 digits, or nothing.
inline std::optional<long long> number(std::string_view text) {
    if (text.empty() || text.size() > 18) return std::nullopt;
    long long value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + (c - '0');
    }
    return value;
}
inline int clamped(long long value) { return value > 2147483647LL ? 2147483647 : static_cast<int>(value); }
// A value that stays one word of one line.
inline bool one_word(std::string_view text) { return text.find_first_of(" \n\r") == std::string_view::npos; }
// The record's words without who it was.
inline std::string facts(const StreamRecord &r) {
    return "secs=" + std::to_string(std::max(0LL, r.seconds)) + " codec=" + codec_name(r.video_format) +
           " hdr=" + (r.hdr ? "1" : "0") + " mode=" + (r.mode.empty() || !one_word(r.mode) ? "-" : r.mode) + " kbps=" + std::to_string(std::max(0, r.kbps)) +
           " sent_kbps=" + (r.sent_kbps < 0 ? std::string("-") : std::to_string(r.sent_kbps)) +
           " rtt_ms=" + (r.rtt_ms < 0 ? std::string("-") : std::to_string(r.rtt_ms)) +
           " recoveries=" + std::to_string(std::max(0, r.recoveries)) +
           " missing_frames=" + std::to_string(std::max(0, r.missing_frames)) + " internet=" + (r.internet ? "1" : "0");
}
}  // namespace history_detail

// "ended=<unix s> secs=<n> codec=<H.264|HEVC|AV1> hdr=<0|1> mode=<WxH@fps|-> kbps=<n> sent_kbps=<n|-> rtt_ms=<n|->
//  recoveries=<n> missing_frames=<n> internet=<0|1> uuid=<uuid|-> name=<name>": the file's line and, after "OK ", the
// control socket's HISTORY line. The name comes last because it may hold spaces.
inline std::string history_line(const StreamRecord &r) {
    std::string name = r.name;
    std::replace_if(name.begin(), name.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
    std::string uuid = r.uuid;
    if (uuid.empty() || !history_detail::one_word(uuid)) uuid = "-";
    return "ended=" + std::to_string(std::max(0LL, r.ended)) + ' ' + history_detail::facts(r) + " uuid=" + uuid + " name=" + name;
}

// The log's line when a stream ends: the same without who it was and when.
inline std::string ended_line(const StreamRecord &r) { return "Stream ended: " + history_detail::facts(r); }

// A line as history_line writes it; nothing for a line that isn't one (a damaged file loses that record, not the rest).
// Of a text with line breaks only the first line is read, so no word of a record holds one.
inline std::optional<StreamRecord> parse_history_line(std::string_view line) {
    line = line.substr(0, line.find_first_of("\r\n"));
    std::map<std::string, std::string, std::less<>> words;
    std::string_view rest = line;
    bool named = false;
    while (!rest.empty()) {
        while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
        const std::size_t eq = rest.find('=');
        if (eq == std::string_view::npos) break;
        const std::string_view key = rest.substr(0, eq);
        rest.remove_prefix(eq + 1);
        if (key == "name") {
            words.emplace("name", rest);
            named = true;
            break;
        }
        const std::size_t space = rest.find(' ');
        words.emplace(key, rest.substr(0, space));
        rest = space == std::string_view::npos ? std::string_view() : rest.substr(space + 1);
    }
    const auto word = [&words](std::string_view key) -> std::string_view {
        const auto it = words.find(key);
        return it == words.end() ? std::string_view() : std::string_view(it->second);
    };
    using history_detail::clamped;
    using history_detail::number;
    const auto ended = number(word("ended")), seconds = number(word("secs"));
    if (!named || !ended || !seconds) return std::nullopt;
    StreamRecord r;
    r.ended = *ended;
    r.seconds = *seconds;
    const std::string_view codec = word("codec");
    r.video_format = codec == "HEVC" ? 1 : codec == "AV1" ? 2 : 0;
    r.hdr = word("hdr") == "1";
    r.mode = word("mode").empty() || word("mode").size() > 24 ? std::string("-") : std::string(word("mode"));
    r.kbps = clamped(number(word("kbps")).value_or(0));
    r.sent_kbps = clamped(number(word("sent_kbps")).value_or(-1));
    r.rtt_ms = clamped(number(word("rtt_ms")).value_or(-1));
    r.recoveries = clamped(number(word("recoveries")).value_or(0));
    r.missing_frames = clamped(number(word("missing_frames")).value_or(0));
    r.internet = word("internet") == "1";
    r.uuid = word("uuid") == "-" || word("uuid").size() > 64 ? std::string() : std::string(word("uuid"));
    r.name = std::string(word("name").substr(0, 256));
    return r;
}

class StreamHistory {
public:
    static constexpr std::size_t kKeep = 20;

    // A running stream as it shows now: `key` tells it from the same device's next one (its certificate and its start).
    struct Live {
        std::string key;
        StreamRecord now;  // `ended` unused
    };

    // What runs now. Streams seen before that are gone become records, ended at `wall_now`; they are returned, oldest
    // first, and the caller saves text() when there are any.
    std::vector<StreamRecord> observe(const std::vector<Live> &live, long long wall_now) {
        std::map<std::string, StreamRecord> seen;
        for (const Live &stream : live) {
            if (stream.key.empty()) continue;
            StreamRecord record = stream.now;
            // What a later look no longer knows stays as it was known: the mode once the capture is gone, the round trip
            // and the device once it is unpaired while streaming.
            if (const auto before = live_.find(stream.key); before != live_.end()) {
                const StreamRecord &old = before->second;
                if (record.mode.empty() || record.mode == "-") {
                    record.mode = old.mode;
                    record.video_format = old.video_format;
                    record.hdr = old.hdr;
                    record.kbps = old.kbps;
                }
                if (record.rtt_ms < 0) record.rtt_ms = old.rtt_ms;
                if (record.sent_kbps < 0) record.sent_kbps = old.sent_kbps;
                if (record.uuid.empty()) {
                    record.uuid = old.uuid;
                    record.name = old.name;
                }
                record.seconds = std::max(record.seconds, old.seconds);
                record.recoveries = std::max(record.recoveries, old.recoveries);
                record.missing_frames = std::max(record.missing_frames, old.missing_frames);
            }
            seen[stream.key] = std::move(record);
        }
        std::vector<StreamRecord> ended;
        for (auto &[key, record] : live_) {
            if (seen.count(key)) continue;
            record.ended = wall_now;
            ended.push_back(record);
        }
        // Several that ended in the same look: the one that started first comes first.
        std::stable_sort(ended.begin(), ended.end(), [](const StreamRecord &a, const StreamRecord &b) { return a.seconds > b.seconds; });
        for (const StreamRecord &record : ended) add(record);
        live_ = std::move(seen);
        return ended;
    }

    // The records, oldest first.
    const std::deque<StreamRecord> &records() const { return records_; }

    // The file: one line per record, oldest first.
    std::string text() const {
        std::string out;
        for (const StreamRecord &record : records_) out += history_line(record) + '\n';
        return out;
    }

    // Reads a file's text; lines that are no record are left out, and of more than kKeep the last stay.
    void load(std::string_view text) {
        records_.clear();
        while (!text.empty()) {
            const std::size_t end = text.find('\n');
            if (const auto record = parse_history_line(text.substr(0, end))) add(*record);
            text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        }
    }

private:
    void add(const StreamRecord &record) {
        records_.push_back(record);
        while (records_.size() > kKeep) records_.pop_front();
    }

    std::map<std::string, StreamRecord> live_;
    std::deque<StreamRecord> records_;
};

}  // namespace prospero
