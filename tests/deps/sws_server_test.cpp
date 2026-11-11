// SPDX-License-Identifier: GPL-3.0-only
// Simple-Web-Server, the library that reads every request the host gets before a route or a login looks at it, as the host
// configures it (sws_harness.hpp: the GameStream servers, the web page's server, a server with nothing set). One request
// from anybody who reaches the port once ended the host: a chunked body cut off inside a chunk made the library call an
// unset on_error, std::bad_function_call left start() (patches/simple-web-server/0002). Every case below ends on the same
// property: start() has not left, and a plain request on a new connection is answered.
#include "sws_harness.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace sws_test;
using namespace std::string_view_literals;

namespace {

int failures = 0;
const char *section = "";

#define CHECK(cond, ...)                                                                                       \
    do {                                                                                                       \
        if (!(cond)) {                                                                                         \
            ++failures;                                                                                        \
            std::fprintf(stderr, "FAIL [%s] %s:%d: %s -- ", section, __FILE__, __LINE__, #cond);               \
            std::fprintf(stderr, __VA_ARGS__);                                                                 \
            std::fprintf(stderr, "\n");                                                                        \
        }                                                                                                      \
    } while (0)

// The property: the server is still there and still serves.
void survived(Instance &s, const std::string &what) {
    if (s.ended()) {
        // No point in asking a server whose start() has left; and a dead one makes every later case fail the same way.
        CHECK(false, "%s/%s: start() left (%s)", name(s.profile()), what.c_str(), s.failure().c_str());
        return;
    }
    CHECK(answered(s.port()), "%s/%s: a plain request was not answered", name(s.profile()), what.c_str());
    CHECK(!s.ended(), "%s/%s: start() left (%s)", name(s.profile()), what.c_str(), s.failure().c_str());
}

const std::string kChunkedHead = "POST /echo HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n";

enum class End { finish, reset, silent };
const char *end_name(End e) { return e == End::finish ? "FIN" : e == End::reset ? "RST" : "silent"; }

// Sends the bytes, ends the connection the given way (silent: waits for the server's own timeout), and reads what comes.
Client::Reply send_and_end(Instance &s, const std::string &bytes, End how, std::chrono::milliseconds wait = 2500ms) {
    Client c(s.port());
    CHECK(c.connected(), "connect");
    c.send(bytes);
    if (how == End::reset) {
        c.reset();
        return {};
    }
    if (how == End::finish) c.finish();
    return c.read(wait);
}

// 1. A chunked request whose connection ends in every place a chunked body can be cut. These are the bytes that ended the
// host: "1\r\nA" and then the connection closing.
void chunked_cut_off() {
    section = "chunked cut off";
    const std::vector<std::pair<std::string, std::string>> cuts = {
        {"no chunk at all", ""},
        {"inside a chunk", "1\r\nA"},
        {"inside a longer chunk", "10\r\nabc"},
        {"before the chunk's own CRLF", "1\r\nA\r"},
        {"between chunks", "1\r\nA\r\n"},
        {"inside the chunk size line", "ff"},
        {"after the size, before its LF", "5\r"},
        {"inside the last chunk's line", "1\r\nA\r\n0"},
        {"before the final blank line", "1\r\nA\r\n0\r\n"},
        {"a chunk of the largest size", "ffffffffffffffff\r\nabc"},
        {"a size that is not hex", "zz\r\nabc"},
        {"a size line with no end for 100 bytes", std::string(100, '1')},
        {"head cut off in the Transfer-Encoding line", ""},
    };
    for (Profile p : {Profile::gamestream, Profile::page, Profile::bare}) {
        Instance s(p, 1, 2);
        for (const auto &[what, rest] : cuts) {
            const std::string request = what.rfind("head cut off", 0) == 0 ? "POST /echo HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunk" : kChunkedHead + rest;
            for (End how : {End::finish, End::reset}) {
                send_and_end(s, request, how);
                survived(s, what + " " + end_name(how));
            }
        }
        // Left open and silent: the server's timeout closes the socket, which fails the read in the middle of the chunk.
        const auto before = s.handled.load();
        auto r = send_and_end(s, kChunkedHead + "1\r\nA", End::silent, 6000ms);
        CHECK(r.eof || r.reset, "%s: a silent chunked request is still open after the timeouts", name(p));
        CHECK(s.handled == before, "%s: a cut-off body reached a resource", name(p));
        survived(s, "chunked, silent until the timeout");
    }
}

// 2. Request bodies. Servers that take them read them right; the GameStream servers (accept_content = false) end the
// connection before reading a byte, whatever the framing says, and go on with the next connection.
void bodies() {
    section = "bodies";
    for (Profile p : {Profile::page, Profile::bare}) {
        Instance s(p);
        Client c(s.port());
        c.send("POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello");
        auto r = c.read(3000ms);
        CHECK(r.data.rfind("HTTP/1.1 200 OK", 0) == 0 && r.data.find("hello") != std::string::npos, "%s: a Content-Length body is read: '%s'", name(p), r.data.c_str());
        CHECK(s.content() == "hello", "%s: content '%s'", name(p), s.content().c_str());
        Client d(s.port());
        d.send(kChunkedHead + "5\r\nhello\r\n3\r\n yo\r\n0\r\n\r\n");
        d.finish();
        r = d.read(3000ms);
        CHECK(r.data.rfind("HTTP/1.1 200 OK", 0) == 0, "%s: a chunked body is answered: '%s'", name(p), r.data.c_str());
        CHECK(s.content() == "hello yo", "%s: chunks joined: '%s'", name(p), s.content().c_str());
        // Content too large for the limit (the page and the GameStream servers have one): the connection ends, and the
        // resource never sees it.
        if (p == Profile::page) {
            const auto before = s.handled.load();
            Client e(s.port());
            e.send("POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 70000\r\n\r\n");
            r = e.read(2500ms);
            CHECK(r.eof || r.reset, "page: a body over the limit ends the connection");
            CHECK(s.handled == before, "page: a body over the limit reached a resource");
        }
        survived(s, "bodies");
    }

    Instance s(Profile::gamestream);
    struct Case {
        const char *what;
        std::string request;
    };
    const std::vector<Case> refused = {
        {"Content-Length with the body", "POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhello"},
        {"Content-Length, the body never comes", "POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\n"},
        {"Content-Length 1 on a GET", "GET /serverinfo HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\n\r\n"},
        {"Content-Length over the limit", "POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 70000\r\n\r\n"},
        {"Content-Length of 2^64-1", "POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 18446744073709551615\r\n\r\n"},
        {"Content-Length of -1", "POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: -1\r\n\r\n"},
        {"chunked with a chunk", kChunkedHead + "5\r\nhello\r\n0\r\n\r\n"},
        {"chunked, empty", kChunkedHead + "0\r\n\r\n"},
        {"chunked, nothing after the head", kChunkedHead},
        {"chunked on a GET", "GET /serverinfo HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nA"},
    };
    for (const auto &c : refused) {
        const auto before = s.handled.load();
        Client k(s.port());
        // The server may close before the whole request has been sent.
        k.send(c.request);
        auto r = k.read(2500ms);
        CHECK(r.eof || r.reset, "gamestream/%s: the connection is not ended (timed out %d)", c.what, r.timed_out);
        // Nothing comes back (the library builds a 413 it never sends), and no resource ran.
        CHECK(r.data.empty(), "gamestream/%s: answered with '%s'", c.what, r.data.c_str());
        CHECK(s.handled == before, "gamestream/%s: the request reached a resource", c.what);
        survived(s, c.what);
    }
    // Content-Length: 0 announces no content.
    {
        Client k(s.port());
        k.send("GET /serverinfo HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        auto r = k.read(2500ms);
        CHECK(r.data.rfind("HTTP/1.1 200 OK", 0) == 0, "gamestream: Content-Length 0 is answered: '%s'", r.data.c_str());
    }
    // A connection that carried a refused request is gone; the one before it was answered first, the next request on a
    // new connection too.
    {
        Client k(s.port());
        k.send("GET /serverinfo HTTP/1.1\r\nHost: x\r\n\r\nPOST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\n\r\nabc");
        auto r = k.read(2500ms);
        CHECK(r.data.rfind("HTTP/1.1 200 OK", 0) == 0, "gamestream: the request before the refused one is answered: '%s'", r.data.c_str());
        CHECK(r.data.find("abc") == std::string::npos && r.eof, "gamestream: the refused one is not answered");
        survived(s, "pipelined, second refused");
    }
}

// 3. A head over the limit, and a head that doesn't end.
void heads() {
    section = "heads";
    for (Profile p : {Profile::gamestream, Profile::page}) {
        Instance s(p);
        const std::string name_ = name(p);
        // Over the 64 KiB limit with and without the end of the head, in a header and in the request line.
        const std::vector<std::pair<std::string, std::string>> big = {
            {"a header over the limit, ended", "GET /serverinfo HTTP/1.1\r\nX: " + std::string(70000, 'a') + "\r\n\r\n"},
            {"a header over the limit, not ended", "GET /serverinfo HTTP/1.1\r\nX: " + std::string(70000, 'a')},
            {"a request line over the limit", "GET /" + std::string(70000, 'a') + " HTTP/1.1\r\n\r\n"},
            {"many small headers over the limit", [] {
                 std::string r = "GET /serverinfo HTTP/1.1\r\n";
                 for (int i = 0; i < 9000; ++i) r += "h" + std::to_string(i) + ": v\r\n";
                 return r + "\r\n";
             }()},
        };
        for (const auto &[what, request] : big) {
            const auto before = s.handled.load();
            Client c(s.port());
            c.send(request);
            const auto r = c.read(3000ms);
            CHECK(r.eof || r.reset, "%s/%s: the connection is not ended", name_.c_str(), what.c_str());
            CHECK(r.data.empty(), "%s/%s: answered with '%s'", name_.c_str(), what.c_str(), r.data.substr(0, 40).c_str());
            CHECK(s.handled == before, "%s/%s: reached a resource", name_.c_str(), what.c_str());
            survived(s, what);
        }
        // A head that fits (just under the limit) is answered.
        Client c(s.port());
        c.send("GET /serverinfo HTTP/1.1\r\nConnection: close\r\nX: " + std::string(60000, 'a') + "\r\n\r\n");
        const auto r = c.read(3000ms);
        CHECK(r.data.rfind("HTTP/1.1 200 OK", 0) == 0, "%s: a 60 KB head is answered: '%s'", name_.c_str(), r.data.substr(0, 40).c_str());
    }
    {
        // A server with no limit takes a megabyte of head and goes on.
        Instance s(Profile::bare);
        Client c(s.port());
        c.send("GET /serverinfo HTTP/1.1\r\nConnection: close\r\nX: " + std::string(1 << 20, 'a') + "\r\n\r\n");
        const auto r = c.read(5000ms);
        CHECK(r.data.rfind("HTTP/1.1 200 OK", 0) == 0, "bare: a 1 MiB head is answered: '%s'", r.data.substr(0, 40).c_str());
        survived(s, "1 MiB head");
    }

    // A head that never ends: the server closes the connection when timeout_request is up, whichever way it was left.
    for (Profile p : {Profile::gamestream, Profile::page, Profile::bare}) {
        Instance s(p, 1, 30);
        const std::vector<std::pair<std::string, std::string>> stalls = {
            {"nothing sent", ""},
            {"half a request line", "GET /serv"},
            {"headers, no blank line", "GET /serverinfo HTTP/1.1\r\nHost: x\r\n"},
            {"three of the four terminator bytes", "GET /serverinfo HTTP/1.1\r\nHost: x\r\n\r"},
            {"the request line's CR only", "GET /serverinfo HTTP/1.1\r"},
        };
        for (const auto &[what, bytes] : stalls) {
            Client c(s.port());
            c.send(bytes);
            const auto start = std::chrono::steady_clock::now();
            const auto r = c.read(5000ms);
            const auto took = std::chrono::steady_clock::now() - start;
            CHECK(r.eof || r.reset, "%s/%s: still open after 5 s (timeout_request is 1 s)", name(p), what.c_str());
            CHECK(took < 4000ms, "%s/%s: closed only after %lld ms", name(p), what.c_str(), static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(took).count()));
            survived(s, std::string("stalled head: ") + what);
        }
        // Slow but steady does not get around the head's total time: a byte every 300 ms.
        Client slow(s.port());
        const std::string trickle = "GET /serverinfo HTTP/1.1\r\nHost: x\r\nX-Slow: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        const auto start = std::chrono::steady_clock::now();
        bool open = true;
        for (size_t i = 0; i < trickle.size() && open; ++i) {
            open = slow.send(std::string_view(trickle).substr(i, 1));
            std::this_thread::sleep_for(150ms);
            if (std::chrono::steady_clock::now() - start > 5s) break;
        }
        const auto r = slow.read(500ms);
        CHECK(r.eof || r.reset || !open, "%s: a head trickled in for %lld ms is still open", name(p),
              static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()));
        survived(s, "trickled head");
    }
}

// 4. A request line and headers with odd bytes. Whatever the answer is, the server is still there.
void odd_bytes() {
    section = "odd bytes";
    const std::vector<std::pair<std::string, std::string>> requests = {
        {"empty line first", "\r\nGET /serverinfo HTTP/1.1\r\n\r\n"},
        {"only the terminator", "\r\n\r\n"},
        {"bare LF line ends", "GET /serverinfo HTTP/1.1\nHost: x\n\n\r\n\r\n"},
        {"bare CR line ends", "GET /serverinfo HTTP/1.1\rHost: x\r\r\n\r\n"},
        {"NUL in the method", std::string("GE\0T /serverinfo HTTP/1.1\r\n\r\n"sv)},
        {"NUL in the path", std::string("GET /server\0info HTTP/1.1\r\n\r\n"sv)},
        {"NUL in a header name", std::string("GET /serverinfo HTTP/1.1\r\nHo\0st: x\r\n\r\n"sv)},
        {"NUL in a header value", std::string("GET /serverinfo HTTP/1.1\r\nHost: x\0y\r\n\r\n"sv)},
        {"bytes above 0x7f", "GET /\xff\xfe\x80 HTTP/1.1\r\nH\xc3\xb6st: \xe2\x82\xac\r\n\r\n"},
        {"control bytes", "GET /\x01\x02\x1b[0m HTTP/1.1\r\nHost: \x07\x08\r\n\r\n"},
        {"method only", "GET\r\n\r\n"},
        {"method and path, no version", "GET /serverinfo\r\n\r\n"},
        {"version with no slash", "GET /serverinfo HTTP\r\n\r\n"},
        {"version that is just a slash", "GET /serverinfo /\r\n\r\n"},
        {"version of another protocol", "GET /serverinfo RTSP/1.0\r\n\r\n"},
        {"version, then a slash at the line end", "GET /serverinfo HTTP/\r\n\r\n"},
        {"line ending in a lone slash", "GET / /\n\n\r\n\r\n"},
        {"two spaces after the method", "GET  /serverinfo HTTP/1.1\r\n\r\n"},
        {"tab instead of a space", "GET\t/serverinfo\tHTTP/1.1\r\n\r\n"},
        {"lower-case method", "get /serverinfo HTTP/1.1\r\n\r\n"},
        {"a method that does not exist", "BREW /serverinfo HTTP/1.1\r\n\r\n"},
        {"CONNECT with an authority", "CONNECT example.org:443 HTTP/1.1\r\nHost: example.org\r\n\r\n"},
        {"absolute-form target", "GET http://127.0.0.1/serverinfo HTTP/1.1\r\n\r\n"},
        {"a question mark and nothing after it", "GET /serverinfo? HTTP/1.1\r\n\r\n"},
        {"a question mark, then another", "GET /serverinfo?a=1?b=2 HTTP/1.1\r\n\r\n"},
        {"percent escapes that are cut or not hex", "GET /serverinfo?a=%&b=%4&c=%zz&d=%00&e=%-1 HTTP/1.1\r\n\r\n"},
        {"a query of only separators", "GET /serverinfo?&&&===& HTTP/1.1\r\n\r\n"},
        {"a header with no colon", "GET /serverinfo HTTP/1.1\r\nHost x\r\n\r\n"},
        {"a header line of just a colon", "GET /serverinfo HTTP/1.1\r\n:\r\n\r\n"},
        {"a header with an empty value", "GET /serverinfo HTTP/1.1\r\nHost:\r\nX:\r\n\r\n"},
        {"a header with spaces only", "GET /serverinfo HTTP/1.1\r\nHost:     \r\n\r\n"},
        {"folded header lines", "GET /serverinfo HTTP/1.1\r\nHost: x\r\n  continued\r\n\tmore\r\n\r\n"},
        {"the same header many times", [] {
             std::string r = "GET /serverinfo HTTP/1.1\r\n";
             for (int i = 0; i < 2000; ++i) r += "X: 1\r\n";
             return r + "\r\n";
         }()},
        {"Content-Length: abc", "POST /echo HTTP/1.1\r\nContent-Length: abc\r\n\r\n"},
        {"Content-Length: empty", "POST /echo HTTP/1.1\r\nContent-Length:\r\n\r\n"},
        {"Content-Length: 1e3", "POST /echo HTTP/1.1\r\nContent-Length: 1e3\r\n\r\n"},
        {"Content-Length: 5 and a space", "POST /echo HTTP/1.1\r\nContent-Length: 5 \r\n\r\nhello"},
        {"Content-Length: +5", "POST /echo HTTP/1.1\r\nContent-Length: +5\r\n\r\nhello"},
        {"Content-Length: 0x10", "POST /echo HTTP/1.1\r\nContent-Length: 0x10\r\n\r\n"},
        {"Content-Length: 2^64", "POST /echo HTTP/1.1\r\nContent-Length: 18446744073709551616\r\n\r\n"},
        {"Content-Length: 99999999999999999999999", "POST /echo HTTP/1.1\r\nContent-Length: 99999999999999999999999\r\n\r\n"},
        {"Content-Length: -1", "POST /echo HTTP/1.1\r\nContent-Length: -1\r\n\r\n"},
        {"two Content-Length headers", "POST /echo HTTP/1.1\r\nContent-Length: 3\r\nContent-Length: 5\r\n\r\nabcde"},
        {"Content-Length and chunked", "POST /echo HTTP/1.1\r\nContent-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nA\r\n0\r\n\r\n"},
        {"Transfer-Encoding: Chunked", "POST /echo HTTP/1.1\r\nTransfer-Encoding: Chunked\r\n\r\n1\r\nA\r\n0\r\n\r\n"},
        {"Transfer-Encoding: gzip, chunked", "POST /echo HTTP/1.1\r\nTransfer-Encoding: gzip, chunked\r\n\r\n1\r\nA\r\n0\r\n\r\n"},
        {"chunk extension", "POST /echo HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n1;ext=1\r\nA\r\n0\r\n\r\n"},
        {"chunk size with a sign", "POST /echo HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n-1\r\nA\r\n0\r\n\r\n"},
        {"chunk size 0x1", "POST /echo HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0x1\r\nA\r\n0\r\n\r\n"},
        {"chunk that is shorter than announced", "POST /echo HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nA\r\n0\r\n\r\n"},
        {"chunk without its CRLF", "POST /echo HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nAxx0\r\n\r\n"},
        {"chunk trailer", "POST /echo HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nA\r\n0\r\nTrailer: x\r\n\r\n"},
        {"Expect: 100-continue", "POST /echo HTTP/1.1\r\nExpect: 100-continue\r\nContent-Length: 5\r\n\r\n"},
        {"Upgrade: websocket", "GET /serverinfo HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n"},
        {"HTTP/0.9 style", "GET /serverinfo\r\n"},
        {"HTTP/2 preface", "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"},
        {"a TLS client hello", std::string("\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03"sv) + std::string(64, '\x42')},
        {"a PIN command for the control socket", "PIN 1234 name\r\n\r\n"},
        {"a keep-alive request, then garbage", std::string("GET /serverinfo HTTP/1.1\r\n\r\n\x00\xff garbage \r\n\r\n"sv)},
        {"a pipelined burst of 100 requests", [] {
             std::string r;
             for (int i = 0; i < 100; ++i) r += "GET /serverinfo HTTP/1.1\r\nHost: x\r\n\r\n";
             return r;
         }()},
        {"Connection: keep-alive on HTTP/1.0", "GET /serverinfo HTTP/1.0\r\nConnection: keep-alive\r\n\r\nGET /serverinfo HTTP/1.0\r\n\r\n"},
    };
    for (Profile p : {Profile::gamestream, Profile::page, Profile::bare}) {
        Instance s(p, 1, 2);
        for (const auto &[what, request] : requests) {
            for (End how : {End::finish, End::reset}) {
                send_and_end(s, request, how, 3000ms);
                survived(s, what + " " + end_name(how));
            }
        }
        // The same bytes sent one at a time, so that every read of the library ends inside the request.
        for (const char *what : {"POST /echo HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n", "GET /serverinfo?a=%41 HTTP/1.1\r\nHost: x\r\n\r\n"}) {
            Client c(s.port());
            for (const char *q = what; *q; ++q) c.send(std::string_view(q, 1));
            c.finish();
            const auto r = c.read(3000ms);
            CHECK(r.eof || r.reset || !r.data.empty(), "%s: a request in single bytes is neither answered nor ended", name(p));
            survived(s, "single bytes");
        }
    }
}

// 5. Many connections: opened and closed at once, opened and left, closed with a reset after half a request.
void many_connections() {
    section = "many connections";
    for (Profile p : {Profile::gamestream, Profile::page, Profile::bare}) {
        Instance s(p, 1, 2);
        for (int i = 0; i < 1000; ++i) {
            Client c(s.port());
            if (i % 3 == 1) c.send("GET /serverinfo HTTP/1.1\r\nHo");
            if (i % 3 == 2) c.send(kChunkedHead + "1\r\nA");
            if (i % 2) c.reset();
        }
        survived(s, "1000 connections opened and closed");
        // 200 at once, silent, then all closed (the server's file descriptors and timers).
        {
            std::vector<std::unique_ptr<Client>> open;
            for (int i = 0; i < 200; ++i) {
                open.push_back(std::make_unique<Client>(s.port()));
                CHECK(open.back()->connected(), "connection %d", i);
            }
            survived(s, "200 connections open at once");
            for (auto &c : open) c->close();
        }
        survived(s, "200 connections closed together");
        // And left open until the server's timeout closes every one of them.
        {
            std::vector<std::unique_ptr<Client>> open;
            for (int i = 0; i < 100; ++i) open.push_back(std::make_unique<Client>(s.port()));
            int ended = 0;
            for (auto &c : open) {
                const auto r = c->read(4000ms);
                if (r.eof || r.reset) ++ended;
            }
            CHECK(ended == 100, "%s: %d of 100 silent connections were closed by the timeout", name(p), ended);
            survived(s, "100 silent connections timed out");
        }
        // A client that reads only a little of a long answer and then resets.
        for (int i = 0; i < 20; ++i) {
            Client c(s.port());
            c.send("GET /big HTTP/1.1\r\nHost: x\r\n\r\n");
            c.read(300ms, 50ms, 100);
            c.reset();
        }
        survived(s, "a long answer left unread");
        // A resource that throws.
        {
            Client c(s.port());
            c.send("GET /throw HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
            c.read(1500ms);
            survived(s, "a resource that throws");
        }
        // Requests from several threads at once.
        {
            std::vector<std::thread> threads;
            std::atomic<int> good{0};
            for (int t = 0; t < 8; ++t) {
                threads.emplace_back([&, t] {
                    for (int i = 0; i < 50; ++i) {
                        if (i % 4 == t % 4) {
                            Client c(s.port());
                            c.send(kChunkedHead + "5\r\nab");
                            c.reset();
                        } else if (answered(s.port())) {
                            ++good;
                        }
                    }
                });
            }
            for (auto &t : threads) t.join();
            CHECK(good > 0, "%s: no request of 8 threads was answered", name(p));
            survived(s, "8 threads");
        }
    }
}

// 6. A keep-alive connection serves several requests; the server closes only what it must.
void keep_alive() {
    section = "keep-alive";
    for (Profile p : {Profile::gamestream, Profile::page, Profile::bare}) {
        Instance s(p);
        Client c(s.port());
        std::string all;
        for (int i = 0; i < 3; ++i) c.send("GET /serverinfo HTTP/1.1\r\nHost: x\r\n\r\n");
        c.send("GET /serverinfo HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
        const auto r = c.read(3000ms);
        size_t answers = 0;
        for (size_t pos = 0; (pos = r.data.find("HTTP/1.1 200 OK", pos)) != std::string::npos; ++pos) ++answers;
        CHECK(answers == 4, "%s: %zu of 4 pipelined requests answered", name(p), answers);
        CHECK(r.eof, "%s: the connection is closed after Connection: close", name(p));
        survived(s, "keep-alive");
    }
}

// 7. A chunk size so large that adding it to what has been read wraps around. The library checked
// "chunk_size + bytes read so far > limit" with 64-bit numbers: a size of 2^64-1 wraps to one less than the bytes read, the
// check passes, and the bytes that came with it were copied into a buffer that was already nearly full, which threw
// std::length_error out of start() (found by the fuzz target sws_server; patches/simple-web-server/0003).
void chunk_size_wraps() {
    section = "chunk size wraps";
    const std::string huge = "ffffffffffffffff\r\n";
    const std::vector<std::pair<std::string, std::string>> bodies = {
        {"a full buffer, then 2^64-1 and bytes", "fff0\r\n" + std::string(0xfff0, 'a') + "\r\n" + huge + std::string(100, 'b')},
        {"a nearly full buffer, then 2^64-1 and bytes", "ff00\r\n" + std::string(0xff00, 'a') + "\r\n" + huge + std::string(0x200, 'b')},
        {"one byte, then 2^64-1 and 70000 bytes", "1\r\nA\r\n" + huge + std::string(70000, 'b')},
        {"one byte, then 2^64-1 and nothing", "1\r\nA\r\n" + huge},
        {"2^64-1 first", huge + std::string(100, 'b')},
        {"2^64-2 after a chunk", "10\r\n" + std::string(16, 'a') + "\r\nfffffffffffffffe\r\nxx"},
        {"2^63 after a chunk", "10\r\n" + std::string(16, 'a') + "\r\n8000000000000000\r\nxx"},
    };
    for (Profile p : {Profile::gamestream, Profile::page, Profile::bare}) {
        for (const auto &[what, body] : bodies) {
            for (End how : {End::finish, End::reset}) {
                // A server of its own for each case: one that has ended would fail every case after it.
                Instance s(p, 1, 2);
                // Everything in one write, so that it arrives in the reads the library's leftover handling sees.
                Client c(s.port());
                c.send(kChunkedHead + body);
                if (how == End::reset)
                    c.reset();
                else {
                    c.finish();
                    c.read(1500ms, 300ms);
                }
                survived(s, what + " " + end_name(how));
            }
        }
    }
}

}  // namespace

int main() {
    chunked_cut_off();
    bodies();
    heads();
    odd_bytes();
    many_connections();
    keep_alive();
    chunk_size_wraps();
    if (failures) {
        std::fprintf(stderr, "FAIL: %d checks failed\n", failures);
        return 1;
    }
    std::puts("PASS: Simple-Web-Server as the host configures it (cut-off and refused bodies, heads over the limit or never ended, odd bytes, many connections)");
    return 0;
}
