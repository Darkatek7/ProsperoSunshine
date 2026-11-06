// SPDX-License-Identifier: GPL-3.0-only
// What happens to a log record before it is written to sunshine.log, the kernel log or a remote log.
// - Always: no secret is written. At debug level Sunshine logs every query argument of a request (the stream's AES key
//   `rikey` from /launch, the pairing handshake from /pair) and the client's certificate; their values become <hidden>
//   and a PEM block <certificate>. A web page's login (Authorization, Cookie, password) is hidden the same way.
// - Debug build: addresses are masked as in the firmware probe's report (probe_scrub.hpp), and a flood of similar lines
//   is held back: per kind of line (the text with its numbers taken out), `burst` lines pass in each `window`, and the
//   next line after the window says how many were held back and shows the last one.
// Portable, tested in tests/log_filter_test.cpp.
#pragma once
#include "probe_scrub.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prospero::logs {

using Clock = std::chrono::steady_clock;

// Names of query arguments and headers whose values are secrets.
inline bool secret_name(std::string_view name) {
    static constexpr std::string_view secrets[] = {
        "rikey",         "rikeyid",       "salt",          "clientcert",    "clientchallenge", "serverchallengeresp",
        "clientpairingsecret", "pin",     "password",      "authorization", "cookie",          "set-cookie",
        "proxy-authorization"};
    for (const std::string_view secret : secrets) {
        if (name.size() != secret.size()) continue;
        bool same = true;
        for (size_t i = 0; i < name.size() && same; i++)
            same = std::tolower(static_cast<unsigned char>(name[i])) == secret[i];
        if (same) return true;
    }
    return false;
}

// One line of a record: "name -- value" (Sunshine's print_req) or "name :: value" (its RTSP dump) with a secret name gets
// <hidden> as its value.
inline std::string hide_line(std::string line) {
    for (const std::string_view separator : {std::string_view(" -- "), std::string_view(" :: ")}) {
        const size_t at = line.find(separator);
        if (at == std::string::npos) continue;
        size_t first = 0;
        while (first < at && std::isspace(static_cast<unsigned char>(line[first]))) first++;
        if (secret_name(std::string_view(line).substr(first, at - first))) return line.substr(0, at + separator.size()) + "<hidden>";
    }
    return line;
}

// The record without secrets: a PEM block (BEGIN to END, or to the end when cut off) becomes <certificate>, a secret
// argument's value <hidden>.
inline std::string hide_secrets(std::string message) {
    for (size_t begin; (begin = message.find("-----BEGIN ")) != std::string::npos;) {
        size_t end = message.find("-----END ", begin);
        if (end != std::string::npos) end = message.find("-----", end + 9);
        end = end == std::string::npos ? message.size() : end + 5;
        message.replace(begin, end - begin, "<certificate>");
    }
    std::string out;
    size_t at = 0;
    while (at <= message.size()) {
        size_t end = message.find('\n', at);
        if (end == std::string::npos) end = message.size();
        out += hide_line(message.substr(at, end - at));
        if (end < message.size()) out += '\n';
        at = end + 1;
    }
    return out;
}

// The kind of a line: its text with every run of digits as one '#', cut to 160 characters, so that "frame 1201 lost"
// and "frame 1202 lost" count as the same line.
inline std::string line_kind(std::string_view message) {
    std::string kind;
    for (size_t i = 0; i < message.size() && kind.size() < 160; i++) {
        if (std::isdigit(static_cast<unsigned char>(message[i]))) {
            kind += '#';
            while (i + 1 < message.size() && std::isdigit(static_cast<unsigned char>(message[i + 1]))) i++;
        } else {
            kind += message[i];
        }
    }
    return kind;
}

struct Record {
    int severity;
    std::string message;
    bool operator==(const Record &) const = default;
};

struct Options {
    bool mask_addresses = false;  // the debug build: probe::scrub on every record
    bool limit = false;           // the debug build: hold back floods of similar lines
    int burst = 10;               // lines of one kind that pass in a window: the stats line of four streams every 5 s does
    std::chrono::milliseconds window{10000};
    size_t kinds = 64;  // kinds remembered at once; the one unused longest goes first
};

class Filter {
public:
    explicit Filter(Options options = {}) : options_(options) {}

    // The records to write for one record: the summaries of kinds whose window ended with lines held back (or all of them
    // after flush_next()), then the record itself unless it is held back.
    std::vector<Record> take(int severity, std::string message, Clock::time_point now) {
        message = hide_secrets(std::move(message));
        if (options_.mask_addresses) message = probe::scrub(std::move(message));
        std::vector<Record> out;
        if (!options_.limit) {
            out.push_back({severity, std::move(message)});
            return out;
        }
        const bool all = flush_.exchange(false);
        for (Kind &kind : kinds_)
            if (kind.held && (all || now - kind.window_start >= options_.window)) summarize(kind, now, out);
        const std::string name = line_kind(message);
        auto it = std::find_if(kinds_.begin(), kinds_.end(), [&](const Kind &k) { return k.name == name; });
        if (it == kinds_.end()) {
            if (kinds_.size() >= options_.kinds) {
                auto oldest = std::min_element(kinds_.begin(), kinds_.end(),
                                               [](const Kind &a, const Kind &b) { return a.last_seen < b.last_seen; });
                if (oldest->held) summarize(*oldest, now, out);
                kinds_.erase(oldest);
            }
            kinds_.push_back(Kind{name, now, now, 0, 0, 0, {}});
            it = kinds_.end() - 1;
        }
        Kind &kind = *it;
        kind.last_seen = now;
        if (now - kind.window_start >= options_.window) {
            kind.window_start = now;
            kind.passed = 0;
        }
        if (kind.passed < options_.burst) {
            kind.passed++;
            out.push_back({severity, std::move(message)});
        } else {
            kind.held++;
            kind.held_severity = std::max(kind.held_severity, severity);
            kind.last = std::move(message);
        }
        return out;
    }

    // The next take() first writes every summary still due, whatever its window: before the host stops.
    void flush_next() { flush_.store(true); }

private:
    struct Kind {
        std::string name;
        Clock::time_point window_start, last_seen;
        int passed = 0, held = 0, held_severity = 0;
        std::string last;
    };

    static void summarize(Kind &kind, Clock::time_point now, std::vector<Record> &out) {
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now - kind.window_start).count();
        out.push_back({kind.held_severity, "Held back " + std::to_string(kind.held) + " similar line" + (kind.held == 1 ? "" : "s") +
                                               " in " + std::to_string(std::max<long long>(1, seconds)) +
                                               " s, the last one: " + kind.last});
        kind.held = 0;
        kind.held_severity = 0;
        kind.last.clear();
        kind.window_start = now;
        kind.passed = 0;
    }

    Options options_;
    std::vector<Kind> kinds_;
    std::atomic<bool> flush_{false};  // set from another thread than the one that writes
};

}  // namespace prospero::logs
