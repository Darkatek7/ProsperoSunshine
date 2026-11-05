// SPDX-License-Identifier: GPL-3.0-only
#include "web/web_auth.hpp"
#include <cassert>
#include <iostream>
using namespace prospero::web;
using namespace std::chrono_literals;

static std::string fake_hash(std::string_view password, std::string_view salt, unsigned iterations) {
    return std::string(password) + "/" + std::string(salt) + "/" + std::to_string(iterations);
}

int main() {
    // The login file round-trips and rejects incomplete ones.
    Login login{"owner", "c2FsdA", 200000, fake_hash("correct horse", "c2FsdA", 200000)};
    const auto parsed = parse_login(format_login(login));
    assert(parsed && parsed->user == "owner" && parsed->salt == "c2FsdA" && parsed->iterations == 200000 &&
           parsed->hash == login.hash);
    assert(parse_login("user=a\r\nsalt=b\r\niterations=3\r\nhash=c\r\nnew=ignored\r\n"));
    assert(!parse_login(""));
    assert(!parse_login("user=a\nsalt=b\nhash=c\n"));
    assert(!parse_login("user=a\nsalt=b\niterations=x\nhash=c\n"));
    assert(!parse_login("user=a\nsalt=b\niterations=99999999999\nhash=c\n"));

    // Names and passwords.
    assert(valid_user("owner") && valid_user("my.name_1-2"));
    assert(!valid_user("") && !valid_user("two words") && !valid_user("a/b") && !valid_user(std::string(33, 'a')));
    assert(valid_password("12345678") && valid_password("with spaces ok"));
    assert(!valid_password("short") && !valid_password(std::string(129, 'a')) && !valid_password("tab\there!"));

    // Checking a login.
    assert(check(login, "owner", "correct horse", fake_hash));
    assert(!check(login, "owner", "wrong horse", fake_hash));
    assert(!check(login, "Owner", "correct horse", fake_hash));
    assert(same("abc", "abc") && !same("abc", "abd") && !same("abc", "ab"));

    // Sessions: idle expiry, use keeps them alive, at most eight.
    const auto t0 = Clock::time_point{} + 1000h;
    Sessions sessions;
    sessions.add("a", t0);
    assert(sessions.use("a", t0 + 11h));
    assert(sessions.use("a", t0 + 22h));  // kept alive by the use before
    assert(!sessions.use("a", t0 + 35h));
    assert(!sessions.use("", t0));
    for (int i = 0; i < 9; ++i) sessions.add("s" + std::to_string(i), t0 + std::chrono::seconds(i));
    assert(sessions.size() == Sessions::limit);
    assert(!sessions.use("s0", t0 + 10s));
    assert(sessions.use("s8", t0 + 10s));
    sessions.remove("s8");
    assert(!sessions.use("s8", t0 + 10s));
    sessions.clear();
    assert(sessions.size() == 0);

    // Lockout: four free tries, then 30 s, 60 s, ... up to 15 minutes.
    Lockout lockout;
    for (int i = 0; i < 4; ++i) lockout.failed("192.168.1.5", t0);
    assert(lockout.wait("192.168.1.5", t0) == 0s);
    lockout.failed("192.168.1.5", t0);
    assert(lockout.wait("192.168.1.5", t0) == 30s);
    assert(lockout.wait("192.168.1.5", t0 + 29s) == 1s);
    assert(lockout.wait("192.168.1.5", t0 + 30s) == 0s);
    assert(lockout.wait("192.168.1.6", t0) == 0s);
    lockout.failed("192.168.1.5", t0 + 30s);
    assert(lockout.wait("192.168.1.5", t0 + 30s) == 60s);
    for (int i = 0; i < 20; ++i) lockout.failed("192.168.1.5", t0 + 1h);
    assert(lockout.wait("192.168.1.5", t0 + 1h) == 900s);
    lockout.succeeded("192.168.1.5");
    assert(lockout.wait("192.168.1.5", t0 + 1h) == 0s);
    for (size_t i = 0; i < Lockout::limit + 10; ++i) lockout.failed("a" + std::to_string(i), t0 + std::chrono::seconds(i));

    // Many addresses share 20 failures; then every address waits, until a good login.
    Lockout shared;
    for (int i = 0; i < 20; ++i) shared.failed("10.1.0." + std::to_string(i), t0);
    assert(shared.wait("10.9.9.9", t0) == 0s);
    shared.failed("10.1.1.1", t0);
    assert(shared.wait("10.9.9.9", t0) == 30s);
    shared.succeeded("10.1.1.1");
    assert(shared.wait("10.9.9.9", t0) == 0s);

    // A full table never drops a locked address to make room.
    Lockout full;
    for (int i = 0; i < 5; ++i) full.failed("locked", t0);
    for (size_t i = 0; i < Lockout::limit + 5; ++i) {
        full.failed("b" + std::to_string(i), t0);
        full.succeeded("none");  // keeps the shared limit out of this check
    }
    assert(full.wait("locked", t0) == 30s);

    // Cookies.
    assert(cookie("pss=abc", "pss") == "abc");
    assert(cookie("other=1; pss=abc; x=2", "pss") == "abc");
    assert(cookie("xpss=abc; pssx=1", "pss").empty());
    assert(cookie("", "pss").empty());

    std::cout << "PASS web_auth_test\n";
}
