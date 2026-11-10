// SPDX-License-Identifier: GPL-3.0-only
// The web page's login (src/web/web_auth.hpp): the files the host reads at start (web_login.txt, the remembered tokens), the
// Cookie header of a request, and the bounded tables behind the login: sessions, remembered tokens, failed logins.
#include "fuzz_common.hpp"
#include "web/web_auth.hpp"

#include <chrono>
#include <set>

using namespace prospero::web;

namespace {

// A fixed hash that is injective enough for the tests: the text itself in hex.
std::string hex_of(std::string_view text) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (const char c : text) {
        out += digits[static_cast<unsigned char>(c) >> 4];
        out += digits[static_cast<unsigned char>(c) & 15];
    }
    return out;
}

std::string hex_word(FuzzedDataProvider &in, size_t most) {
    std::string word;
    for (size_t i = 0, n = in.ConsumeIntegralInRange<size_t>(1, most); i < n; ++i) word += "0123456789abcdef"[in.ConsumeIntegralInRange<int>(0, 15)];
    return word;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider in(data, size);

    // The login file as it may be found on disk: anything.
    const std::string file = in.ConsumeRandomLengthString(300);
    if (const auto login = parse_login(file)) {
        FUZZ_CHECK(!login->user.empty() && !login->salt.empty() && !login->hash.empty() && login->iterations > 0);
        FUZZ_CHECK(login->iterations <= 1000000000u);
        FUZZ_CHECK(login->user.find('\n') == std::string::npos);
    }
    // One the host wrote: the file reads back as it was.
    {
        Login written;
        written.user = "u" + hex_word(in, 20);
        written.salt = hex_word(in, 64);
        written.hash = hex_word(in, 64);
        written.iterations = in.ConsumeIntegralInRange<unsigned>(1, 100000000u);
        written.enabled = in.ConsumeBool();
        FUZZ_CHECK(valid_user(written.user));
        const auto back = parse_login(format_login(written));
        FUZZ_CHECK(back && back->user == written.user && back->salt == written.salt && back->hash == written.hash &&
                   back->iterations == written.iterations && back->enabled == written.enabled);
        // check() with the stand-in hash accepts exactly the right user and password.
        written.hash = hex_of("pw--" + written.salt);
        const auto hash = [](std::string_view password, const std::string &salt, unsigned) { return hex_of(std::string(password.substr(0, 3)) + "-" + salt); };
        FUZZ_CHECK(check(written, written.user, "pw-xyz", hash));
        FUZZ_CHECK(!check(written, written.user + "x", "pw-xyz", hash));
        FUZZ_CHECK(!check(written, written.user, "px-xyz", hash));
    }
    FUZZ_CHECK(valid_user(file) == (!file.empty() && file.size() <= 32 && file.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") == std::string::npos));
    (void)valid_password(file);

    // The remembered-tokens file, read for a login id, at some time.
    {
        const long long now = in.ConsumeIntegralInRange<long long>(0, 4000000000LL);
        const std::string id = hex_word(in, 16);
        std::string text = in.ConsumeRandomLengthString(400);
        if (in.ConsumeBool()) text = "login=" + id + "\n" + text;  // so the tokens count
        const Remembered parsed = parse_remembered(text, id, now);
        FUZZ_CHECK(parsed.size() <= Remembered::limit);
        for (const auto &e : parsed.entries()) FUZZ_CHECK(e.expires > now && e.expires <= now + Remembered::lifetime && !e.hash.empty());
        // Written and read again, the same tokens stay.
        const Remembered again = parse_remembered(format_remembered(parsed), id, now);
        FUZZ_CHECK(again.size() == parsed.size());
    }
    // Remembered tokens added by the host: bounded, and the newest one is always in.
    {
        Remembered remembered;
        long long now = 1000;
        std::string newest;
        for (int i = 0; i < 40 && in.remaining_bytes() > 0; ++i) {
            now += in.ConsumeIntegralInRange<long long>(0, 3600LL * 24 * 10);
            newest = hex_word(in, 8) + std::to_string(i);
            if (in.ConsumeBool()) remembered.add(newest, now);
            else remembered.keep(hex_word(in, 8), now + in.ConsumeIntegralInRange<long long>(-5, Remembered::lifetime));
            FUZZ_CHECK(remembered.size() <= Remembered::limit);
        }
        if (!newest.empty()) {
            remembered.add(newest, now);
            FUZZ_CHECK(remembered.use("anything", now, [&](std::string_view) { return newest; }));
            FUZZ_CHECK(!remembered.use("", now, [&](std::string_view) { return newest; }));
        }
    }

    // A Cookie header: the value read for a name is a piece of the header.
    {
        const std::string header = in.ConsumeRandomLengthString(200), name = in.ConsumeRandomLengthString(6);
        const std::string value = cookie(header, name);
        FUZZ_CHECK(value.size() <= header.size());
        if (!value.empty()) FUZZ_CHECK(header.find(value) != std::string::npos && value.find(';') == std::string::npos);
        const std::string own = "q" + hex_word(in, 5), word = hex_word(in, 8);
        FUZZ_CHECK(cookie("a=1; " + own + "=" + word + "; z=2", own) == word);
    }

    // Sessions: at most 8, the last login is in, and an idle one is gone after 12 hours.
    {
        Sessions sessions;
        auto now = Clock::time_point{} + std::chrono::hours(1);
        std::set<std::string> live;
        for (int i = 0; i < 60 && in.remaining_bytes() > 0; ++i) {
            now += std::chrono::seconds(in.ConsumeIntegralInRange<int>(0, 3 * 3600));
            const std::string token = hex_word(in, 4);
            switch (in.ConsumeIntegralInRange<int>(0, 3)) {
            case 0: sessions.add(token, now); FUZZ_CHECK(sessions.use(token, now)); break;
            case 1: (void)sessions.use(token, now); break;
            case 2: sessions.remove(token); FUZZ_CHECK(!sessions.use(token, now)); break;
            default: if (in.ConsumeBool()) { sessions.clear(); FUZZ_CHECK(sessions.size() == 0); } break;
            }
            FUZZ_CHECK(sessions.size() <= Sessions::limit);
        }
        FUZZ_CHECK(!sessions.use("", now));
        now += Sessions::idle;
        FUZZ_CHECK(!sessions.use("ffff", now));
        FUZZ_CHECK(sessions.size() == 0);
    }

    // Lockout: the wait never passes 15 minutes, a good login clears it, and the table stays small.
    {
        Lockout lockout;
        auto now = Clock::time_point{} + std::chrono::hours(1);
        for (int i = 0; i < 400 && in.remaining_bytes() > 0; ++i) {
            now += std::chrono::seconds(in.ConsumeIntegralInRange<int>(0, 120));
            const std::string address = "10.0.0." + std::to_string(in.ConsumeIntegralInRange<int>(0, 300));
            if (in.ConsumeIntegralInRange<int>(0, 9) == 0) {
                lockout.succeeded(address);
                FUZZ_CHECK(lockout.wait(address, now) == std::chrono::seconds(0));
            } else {
                lockout.failed(address, now);
            }
            const auto wait = lockout.wait(address, now);
            FUZZ_CHECK(wait >= std::chrono::seconds(0) && wait <= Lockout::max_wait);
        }
    }
    return 0;
}
