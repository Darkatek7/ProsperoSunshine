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

    // On or off: written as enabled=, a file from before the switch is on, and turning it off keeps the rest.
    assert(parsed->enabled);
    assert(format_login(login).find("\nenabled=1\n") != std::string::npos);
    const auto old_file = parse_login("user=a\nsalt=b\niterations=3\nhash=c\n");
    assert(old_file && old_file->enabled);
    Login off = login;
    off.enabled = false;
    assert(format_login(off).find("\nenabled=0\n") != std::string::npos);
    const auto parsed_off = parse_login(format_login(off));
    assert(parsed_off && !parsed_off->enabled && parsed_off->user == "owner" && parsed_off->hash == login.hash &&
           parsed_off->salt == login.salt && parsed_off->iterations == login.iterations);
    assert(parse_login("user=a\nsalt=b\niterations=3\nhash=c\nenabled=1\n")->enabled);
    assert(!parse_login("user=a\r\nsalt=b\r\niterations=3\r\nhash=c\r\nenabled=0\r\n")->enabled);
    assert(!parse_login("user=a\nsalt=b\niterations=3\nhash=c\nenabled=yes\n"));
    assert(!parse_login("user=a\nsalt=b\niterations=3\nhash=c\nenabled=\n"));
    assert(!parse_login("enabled=1\n"));

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

    // Remember me: hashed tokens, 30 days from the login however often used, at most eight.
    const auto digest = [](std::string_view text) { return "h(" + std::string(text) + ")"; };
    const long long day = 24 * 3600, w0 = 1'800'000'000;
    assert(Remembered::lifetime == 30 * day);
    Remembered remembered;
    remembered.login_id = login_id(login, digest);
    assert(remembered.login_id == "h(login:c2FsdA:" + login.hash + ")");
    assert(!remembered.use("t1", w0, digest));
    remembered.add(digest("t1"), w0);
    assert(remembered.entries().front().hash == "h(t1)");  // the token itself is never kept
    assert(remembered.use("t1", w0 + 1, digest));
    assert(!remembered.use("t2", w0 + 1, digest));
    assert(!remembered.use("", w0 + 1, digest));
    assert(remembered.use("t1", w0 + 30 * day - 1, digest));
    assert(!remembered.use("t1", w0 + 30 * day, digest));  // used the day before, gone all the same
    assert(remembered.size() == 0);
    for (int i = 0; i < 9; ++i) remembered.add(digest("r" + std::to_string(i)), w0 + i);
    assert(remembered.size() == Remembered::limit);
    assert(!remembered.use("r0", w0 + 10, digest));  // the one that expires first went
    assert(remembered.use("r1", w0 + 10, digest) && remembered.use("r8", w0 + 10, digest));
    Remembered same_second;  // a new login always gets in, also when all expire alike
    for (int i = 0; i < 9; ++i) same_second.add(digest("q" + std::to_string(i)), w0);
    assert(same_second.size() == Remembered::limit && same_second.use("q8", w0, digest));

    // The file round-trips, drops what expired, and counts only for the login it was written for.
    const std::string file = format_remembered(remembered);
    assert(file.starts_with("login=" + remembered.login_id + "\ntoken=h(r1) "));
    Remembered back = parse_remembered(file, remembered.login_id, w0 + 10);
    assert(back.size() == Remembered::limit && back.login_id == remembered.login_id);
    assert(back.use("r1", w0 + 10, digest) && back.use("r8", w0 + 10, digest) && !back.use("r0", w0 + 10, digest));
    assert(format_remembered(back) == file);
    back = parse_remembered(file, remembered.login_id, w0 + 30 * day + 4);  // r1 to r4 expired meanwhile
    assert(back.size() == 4 && !back.use("r4", w0 + 30 * day + 4, digest) && back.use("r5", w0 + 30 * day + 4, digest));
    Login changed = login;
    changed.salt = "bmV3";
    const std::string other_id = login_id(changed, digest);
    assert(other_id != remembered.login_id);
    back = parse_remembered(file, other_id, w0 + 10);
    assert(back.size() == 0 && back.login_id == other_id && !back.use("r8", w0 + 10, digest));
    assert(parse_remembered(file, "", w0 + 10).size() == 0);
    assert(parse_remembered("", remembered.login_id, w0).size() == 0);
    assert(parse_remembered("token=h(x) " + std::to_string(w0 + day) + "\n", remembered.login_id, w0).size() == 0);
    // Broken lines are skipped, the rest is read; a token can't last longer than a lifetime from now.
    const std::string messy = "login=" + remembered.login_id + "\r\ntoken=h(a) " + std::to_string(w0 + day) +
                              "\r\ntoken=h(b)\ntoken= 5\ntoken=h(c) 12x\ntoken=h(d) 99999999999999999999\ntoken=h(e) " +
                              std::to_string(w0 + 31 * day) + "\nnew=ignored\n";
    back = parse_remembered(messy, remembered.login_id, w0);
    assert(back.size() == 1 && back.use("a", w0, digest));
    // Eleven in a file (written by hand, say): the eight that last longest stay.
    std::string many = "login=" + remembered.login_id + "\n";
    for (int i = 0; i < 11; ++i) many += "token=h(m" + std::to_string(i) + ") " + std::to_string(w0 + day + i) + "\n";
    back = parse_remembered(many, remembered.login_id, w0);
    assert(back.size() == Remembered::limit && !back.use("m2", w0, digest) && back.use("m3", w0, digest) &&
           back.use("m10", w0, digest));
    many = "login=" + remembered.login_id + "\n";  // the same, longest first
    for (int i = 10; i >= 0; --i) many += "token=h(m" + std::to_string(i) + ") " + std::to_string(w0 + day + i) + "\n";
    back = parse_remembered(many, remembered.login_id, w0);
    assert(back.size() == Remembered::limit && !back.use("m2", w0, digest) && back.use("m3", w0, digest) &&
           back.use("m10", w0, digest));
    back.clear();
    assert(back.size() == 0 && format_remembered(back) == "login=" + remembered.login_id + "\n");

    // Cookies.
    assert(cookie("pss=abc", "pss") == "abc");
    assert(cookie("other=1; pss=abc; x=2", "pss") == "abc");
    assert(cookie("xpss=abc; pssx=1", "pss").empty());
    assert(cookie("", "pss").empty());

    std::cout << "PASS web_auth_test\n";
}
