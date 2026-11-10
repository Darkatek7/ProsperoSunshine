// SPDX-License-Identifier: GPL-3.0-only
// What happens to a log record before it is written to sunshine.log, the kernel log or a remote log.
// - Always: no secret is written. At debug level Sunshine logs every query argument of a request (the stream's AES key
//   `rikey` from /launch, the pairing handshake from /pair) and the client's certificate; their values become <hidden>
//   and a PEM block <certificate>. The tokens that tie a UDP packet to a stream (the RTSP ping payload and connect data) and
//   a web page's login (Authorization, Cookie, password) are hidden the same way.
// - Always: public addresses (IPv4 and IPv6) become <public-N>: the router's external address from UPnP, a stranger's
//   pairing attempt, a client that streams over the internet. Nothing else in the log is as telling, and it is what a user
//   posts without thinking.
// - Debug build (mask_addresses): everything of the home too, as the diagnostic report masks it (app/src/psapp/mask.hpp):
//   LAN, Tailscale and link-local addresses, MAC addresses and host names of URLs and Host headers become tags such as
//   <lan-1> that name the same address alike in every file of the run, and the identifiers a client sends (uniqueid, uuid, and
//   the device name of a pairing) become <id-N> and <name-N>. A flood of similar lines is held back: per kind of line (the
//   text with its numbers taken out), `burst` lines pass in each `window`, and the next line after the window says how many
//   were held back and shows the last one.
// Portable, tested in tests/log_filter_test.cpp.
#pragma once
#include "../../app/src/psapp/mask.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <memory>
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
        "proxy-authorization", "x-ss-ping-payload", "x-ss-connect-data"};
    for (const std::string_view secret : secrets) {
        if (name.size() != secret.size()) continue;
        bool same = true;
        for (size_t i = 0; i < name.size() && same; i++)
            same = std::tolower(static_cast<unsigned char>(name[i])) == secret[i];
        if (same) return true;
    }
    return false;
}

// One line of a record: "name -- value" (Sunshine's print_req), "name :: value" (its RTSP dump) or "name: value" (the raw RTSP
// message in that dump) with a secret name gets <hidden> as its value. The line that logs a UDP ping ("Received ping [v2] from
// <address> [<payload>]") has the payload that identifies the stream in brackets: hidden too.
inline std::string hide_line(std::string line) {
    for (const std::string_view separator : {std::string_view(" -- "), std::string_view(" :: "), std::string_view(": ")}) {
        const size_t at = line.find(separator);
        if (at == std::string::npos) continue;
        size_t first = 0;
        while (first < at && std::isspace(static_cast<unsigned char>(line[first]))) first++;
        if (secret_name(std::string_view(line).substr(first, at - first))) return line.substr(0, at + separator.size()) + "<hidden>";
    }
    if (line.rfind("Received ping [", 0) == 0 || line.rfind("Received non-ping from ", 0) == 0) {
        const size_t open = line.rfind(" [");
        if (open != std::string::npos && !line.empty() && line.back() == ']') return line.substr(0, open + 2) + "<hidden>]";
    }
    return line;
}

// The record without secrets: a PEM block (BEGIN to END, or to the end when cut off) becomes <certificate>, a secret
// argument's value <hidden>.
inline std::string hide_secrets(std::string message) {
    message = psapp::mask::hide_pem(std::move(message));
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

// What a client calls itself, in the arguments Sunshine dumps at debug level: the id its app made up (a Moonlight app keeps one
// for good), the id of the request and the name of the device in a pairing (often a person's name). The debug build hides
// them behind tags that stay the same for the same value, so "one client asked five times" still shows.
inline const char *identifier_category(std::string_view name) {
    struct Entry {
        std::string_view name;
        const char *category;
    };
    static constexpr Entry entries[] = {{"uniqueid", "id"}, {"uuid", "id"}, {"devicename", "name"}};
    for (const Entry &entry : entries) {
        if (name.size() != entry.name.size()) continue;
        bool same = true;
        for (size_t i = 0; i < name.size() && same; i++) same = std::tolower(static_cast<unsigned char>(name[i])) == entry.name[i];
        if (same) return entry.category;
    }
    return nullptr;
}

inline std::string hide_identifiers(psapp::mask::Masker &masker, const std::string &message) {
    std::string out;
    size_t at = 0;
    while (at <= message.size()) {
        size_t end = message.find('\n', at);
        if (end == std::string::npos) end = message.size();
        std::string line = message.substr(at, end - at);
        for (const std::string_view separator : {std::string_view(" -- "), std::string_view(" :: ")}) {
            const size_t sep = line.find(separator);
            if (sep == std::string::npos) continue;
            size_t first = 0;
            while (first < sep && std::isspace(static_cast<unsigned char>(line[first]))) first++;
            if (const char *category = identifier_category(std::string_view(line).substr(first, sep - first))) {
                const std::string value = line.substr(sep + separator.size());
                if (!value.empty()) line = line.substr(0, sep + separator.size()) + masker.stand_in(category, value);
                break;
            }
        }
        out += line;
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
    bool mask_addresses = false;  // the debug build: the addresses and names of the home too, not only the public ones
    bool limit = false;           // the debug build: hold back floods of similar lines
    int burst = 10;               // lines of one kind that pass in a window: the stats line of four streams every 5 s does
    std::chrono::milliseconds window{10000};
    size_t kinds = 64;  // kinds remembered at once; the one unused longest goes first
};

class Filter {
public:
    // `masker` is shared by the filters of one run (sunshine.log, the kernel log, host_diag.log), so that one address has one tag
    // in all of them; without one the filter makes its own, of the scope its options ask for.
    explicit Filter(Options options = {}, std::shared_ptr<psapp::mask::Masker> masker = {})
        : options_(options),
          masker_(masker ? std::move(masker)
                         : std::make_shared<psapp::mask::Masker>(options.mask_addresses ? psapp::mask::Scope::all : psapp::mask::Scope::internet)) {}

    // The records to write for one record: the summaries of kinds whose window ended with lines held back (or all of them
    // after flush_next()), then the record itself unless it is held back.
    std::vector<Record> take(int severity, std::string message, Clock::time_point now) {
        message = hide_secrets(std::move(message));
        if (options_.mask_addresses) message = hide_identifiers(*masker_, message);
        message = masker_->apply(message);
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
    std::shared_ptr<psapp::mask::Masker> masker_;
    std::vector<Kind> kinds_;
    std::atomic<bool> flush_{false};  // set from another thread than the one that writes
};

}  // namespace prospero::logs
