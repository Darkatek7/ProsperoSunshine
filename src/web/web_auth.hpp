// SPDX-License-Identifier: GPL-3.0-only
// The web page's login (GitHub issue 21): the stored login, browser sessions and the lockout after failed logins. Portable and
// header-only, tested on the PC (tests/web_auth_test.cpp); the hashing and the random tokens come from the caller, so the
// host can use OpenSSL and the tests a fixed function.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace prospero::web {

using Clock = std::chrono::steady_clock;

// The login the owner sets in the app. The password is kept only as a salted PBKDF2-SHA256 hash. Turning the page off keeps
// the login (enabled false), so it can be turned on again without typing it anew.
struct Login {
    std::string user;
    std::string salt;
    unsigned iterations = 0;
    std::string hash;  // hex
    bool enabled = true;
};

// One "key=value" line each; unknown keys are ignored, so the format can grow.
inline std::string format_login(const Login &login) {
    return "user=" + login.user + "\nsalt=" + login.salt + "\niterations=" + std::to_string(login.iterations) +
           "\nhash=" + login.hash + "\nenabled=" + (login.enabled ? "1" : "0") + "\n";
}

inline std::optional<Login> parse_login(std::string_view text) {
    Login login;
    while (!text.empty()) {
        const size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "user") login.user = value;
        else if (key == "salt") login.salt = value;
        else if (key == "hash") login.hash = value;
        else if (key == "enabled") {
            // A file from before the switch has no such line and stays on.
            if (value != "0" && value != "1") return std::nullopt;
            login.enabled = value == "1";
        }
        else if (key == "iterations") {
            unsigned n = 0;
            for (const char c : value) {
                if (c < '0' || c > '9' || n > 100000000u) return std::nullopt;
                n = n * 10 + static_cast<unsigned>(c - '0');
            }
            login.iterations = n;
        }
    }
    if (login.user.empty() || login.salt.empty() || login.hash.empty() || login.iterations == 0) return std::nullopt;
    return login;
}

// A user name is one word: letters, digits and . _ - (it travels in the app's control line).
inline bool valid_user(std::string_view user) {
    if (user.empty() || user.size() > 32) return false;
    return std::all_of(user.begin(), user.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    });
}

// At least 8 characters, printable ASCII (spaces allowed), at most 128.
inline bool valid_password(std::string_view password) {
    if (password.size() < 8 || password.size() > 128) return false;
    return std::all_of(password.begin(), password.end(), [](char c) { return c >= 0x20 && c < 0x7f; });
}

// Compares without stopping at the first difference, so the time doesn't tell how much of a hash matched.
inline bool same(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

// Whether user and password match the login; hash(password, salt, iterations) gives the hex hash.
template <typename Hash>
bool check(const Login &login, std::string_view user, std::string_view password, Hash &&hash) {
    const std::string computed = hash(password, login.salt, login.iterations);
    // Hash first and compare both, so a wrong user name takes as long as a wrong password.
    const bool user_ok = same(user, login.user);
    return same(computed, login.hash) && user_ok;
}

// The browser sessions: a random token in a cookie, forgotten after 12 hours without a request. At most 8 at a time;
// a ninth login drops the one used longest ago.
class Sessions {
public:
    static constexpr auto idle = std::chrono::hours(12);
    static constexpr size_t limit = 8;

    void add(const std::string &token, Clock::time_point now) {
        expire(now);
        if (sessions_.size() >= limit) {
            const auto oldest = std::min_element(sessions_.begin(), sessions_.end(),
                                                 [](const auto &a, const auto &b) { return a.second < b.second; });
            sessions_.erase(oldest);
        }
        sessions_[token] = now;
    }
    // Whether the token belongs to a live session; using it keeps it alive.
    bool use(const std::string &token, Clock::time_point now) {
        expire(now);
        const auto it = sessions_.find(token);
        if (token.empty() || it == sessions_.end()) return false;
        it->second = now;
        return true;
    }
    void remove(const std::string &token) { sessions_.erase(token); }
    void clear() { sessions_.clear(); }
    size_t size() const { return sessions_.size(); }

private:
    void expire(Clock::time_point now) {
        std::erase_if(sessions_, [now](const auto &s) { return now - s.second >= idle; });
    }
    std::map<std::string, Clock::time_point> sessions_;
};

// Failed logins per address: the fifth in a row locks that address for 30 s, and every further one doubles the wait, up to
// 15 minutes. A good login clears it. On top, all addresses together get 20 failures in a row before every address waits
// the same way, so a machine that changes its address (or uses many) doesn't get more guesses; the owner's next good login
// clears that too. At most 256 addresses are kept; when full, the one seen longest ago that isn't locked goes, and while
// all are locked a new address counts only towards the shared limit.
class Lockout {
public:
    static constexpr int free_tries = 4;
    static constexpr int shared_free_tries = 20;
    static constexpr auto first_wait = std::chrono::seconds(30);
    static constexpr auto max_wait = std::chrono::minutes(15);
    static constexpr size_t limit = 256;

    // How long the address has to wait before it may try again; zero when it may try now.
    std::chrono::seconds wait(const std::string &address, Clock::time_point now) const {
        auto until = shared_.until;
        if (const auto it = entries_.find(address); it != entries_.end()) until = std::max(until, it->second.until);
        if (until <= now) return std::chrono::seconds(0);
        return std::chrono::ceil<std::chrono::seconds>(until - now);
    }
    void failed(const std::string &address, Clock::time_point now) {
        count(shared_, shared_free_tries, now);
        if (!entries_.contains(address) && entries_.size() >= limit) {
            auto oldest = entries_.end();
            for (auto it = entries_.begin(); it != entries_.end(); ++it) {
                if (it->second.until > now) continue;  // a locked address stays locked
                if (oldest == entries_.end() || it->second.seen < oldest->second.seen) oldest = it;
            }
            if (oldest == entries_.end()) return;
            entries_.erase(oldest);
        }
        count(entries_[address], free_tries, now);
    }
    void succeeded(const std::string &address) {
        entries_.erase(address);
        shared_ = {};
    }

private:
    struct Entry {
        int failures = 0;
        Clock::time_point until{};
        Clock::time_point seen{};
    };
    static void count(Entry &e, int free, Clock::time_point now) {
        e.seen = now;
        if (++e.failures <= free) return;
        auto wait = std::chrono::duration_cast<Clock::duration>(first_wait);
        for (int i = free + 1; i < e.failures && wait < max_wait; ++i) wait *= 2;
        e.until = now + std::min(wait, std::chrono::duration_cast<Clock::duration>(max_wait));
    }
    std::map<std::string, Entry> entries_;
    Entry shared_;
};

// The value of one cookie in a Cookie header ("a=1; pss=xyz"), or empty.
inline std::string cookie(std::string_view header, std::string_view name) {
    while (!header.empty()) {
        const size_t end = header.find(';');
        std::string_view part = header.substr(0, end);
        header = end == std::string_view::npos ? std::string_view() : header.substr(end + 1);
        while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
        if (part.size() > name.size() && part.substr(0, name.size()) == name && part[name.size()] == '=')
            return std::string(part.substr(name.size() + 1));
    }
    return {};
}

}  // namespace prospero::web
