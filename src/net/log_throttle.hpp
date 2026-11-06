// SPDX-License-Identifier: GPL-3.0-only
// Moonlight asks every host it knows for /serverinfo every few seconds, over HTTPS with its client certificate. A
// client that is not paired fails the certificate check each time, and Sunshine logged a warning for every poll.
// This decides which of those repeats get a line: the first one per key (a certificate fingerprint), then at most one
// per interval. It keeps a bounded number of keys and forgets the one seen longest ago. Not thread-safe; the caller
// holds its own lock (patches/sunshine/0021-throttle-verify-log.patch).
#pragma once
#include <chrono>
#include <cstddef>
#include <string>
#include <unordered_map>

namespace prospero {

class LogThrottle {
public:
    using clock = std::chrono::steady_clock;

    LogThrottle(clock::duration interval, std::size_t capacity) : interval_(interval), capacity_(capacity) {}

    // Whether a line for this key is due at `now`. When it is, `held_back` gets how many were not logged since the
    // last line for the key.
    bool due(const std::string &key, clock::time_point now, std::size_t &held_back) {
        held_back = 0;
        auto it = entries_.find(key);
        if (it == entries_.end()) {
            if (capacity_ == 0) return true;
            if (entries_.size() >= capacity_) evict_stalest();
            entries_.emplace(key, Entry{now, now, 0});
            return true;
        }
        Entry &entry = it->second;
        entry.seen = now;
        if (now - entry.logged < interval_) {
            ++entry.held_back;
            return false;
        }
        held_back = entry.held_back;
        entry.held_back = 0;
        entry.logged = now;
        return true;
    }

    std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        clock::time_point logged; // when the last line for the key was due
        clock::time_point seen;   // when the key last came by
        std::size_t held_back;
    };

    void evict_stalest() {
        auto stalest = entries_.begin();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.seen < stalest->second.seen) stalest = it;
        }
        if (stalest != entries_.end()) entries_.erase(stalest);
    }

    clock::duration interval_;
    std::size_t capacity_;
    std::unordered_map<std::string, Entry> entries_;
};

}
