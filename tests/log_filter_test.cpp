// SPDX-License-Identifier: GPL-3.0-only
// What reaches sunshine.log and the kernel log (src/log/log_filter.hpp): secrets hidden and public addresses masked in every
// build; the debug build also masks the home's addresses, names and the client's ids, and holds floods back.
#include "log/log_filter.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>

using namespace prospero::logs;
using namespace std::chrono_literals;

int main() {
    // Sunshine's print_req at debug level: the stream key and the pairing handshake are hidden, the rest stays.
    assert(hide_secrets("rikey -- 0123456789ABCDEF0123456789ABCDEF") == "rikey -- <hidden>");
    assert(hide_secrets("rikeyid -- 1234") == "rikeyid -- <hidden>");
    assert(hide_secrets("clientcert -- 2d2d2d2d2d424547") == "clientcert -- <hidden>");
    assert(hide_secrets("salt -- 00112233") == "salt -- <hidden>");
    assert(hide_secrets("Cookie :: session=abc") == "Cookie :: <hidden>");
    assert(hide_secrets("Authorization -- Basic dXNlcjpwYXNz") == "Authorization -- <hidden>");
    assert(hide_secrets("mode -- 1920x1080x60") == "mode -- 1920x1080x60");
    assert(hide_secrets("uniqueid -- 0123456789ABCDEF") == "uniqueid -- 0123456789ABCDEF");
    // What ties the UDP packets to a stream: a stranger who knows it could take the video; in Sunshine's request dump and in the
    // raw RTSP message of that dump, and in the line that logs a ping.
    assert(hide_secrets("X-SS-Ping-Payload: 1B2C3D4E5F") == "X-SS-Ping-Payload: <hidden>");
    assert(hide_secrets("X-SS-Connect-Data: 305419896") == "X-SS-Connect-Data: <hidden>");
    assert(hide_secrets("x-ss-ping-payload :: 1B2C") == "x-ss-ping-payload :: <hidden>");
    assert(hide_secrets("Session: DEADBEEFCAFE;timeout = 90") == "Session: DEADBEEFCAFE;timeout = 90");
    assert(hide_secrets("Received ping [v2] from 192.168.1.9:50035 [0100000043414537]") == "Received ping [v2] from 192.168.1.9:50035 [<hidden>]");
    assert(hide_secrets("Received non-ping from 192.168.1.9:1 [00ff]") == "Received non-ping from 192.168.1.9:1 [<hidden>]");
    assert(hide_secrets("Received ping [v1] from 192.168.1.9:1 [") == "Received ping [v1] from 192.168.1.9:1 [");
    assert(hide_secrets("Control: PIN submitted for a pending pairing") == "Control: PIN submitted for a pending pairing");
    assert(hide_secrets("a line about the rikey without a value") == "a line about the rikey without a value");
    // The client's certificate (nvhttp, debug level), whole or cut off.
    assert(hide_secrets("-----BEGIN CERTIFICATE-----\nMIIC\nAbCd\n-----END CERTIFICATE-----\n") == "<certificate>\n");
    assert(hide_secrets("cert: -----BEGIN PRIVATE KEY-----\nMIIE") == "cert: <certificate>");
    assert(hide_secrets("x\nsalt -- 0011\ny") == "x\nsalt -- <hidden>\ny");

    const auto t0 = Clock::now();
    // What a client calls itself in the arguments of a request (the debug build hides them behind stable tags).
    {
        psapp::mask::Masker masker;
        assert(hide_identifiers(masker, "uniqueid -- 0123456789ABCDEF") == "uniqueid -- <id-1>");
        assert(hide_identifiers(masker, "UUID -- 1b4e28ba-2fa1-11d2-883f-0016d3cca427") == "UUID -- <id-2>");
        assert(hide_identifiers(masker, "uniqueid -- 0123456789ABCDEF") == "uniqueid -- <id-1>");
        assert(hide_identifiers(masker, "x\ndevicename -- Deck\ny") == "x\ndevicename -- <name-1>\ny");
        assert(hide_identifiers(masker, "devicename :: Deck") == "devicename :: <name-1>");
        assert(hide_identifiers(masker, "devicename -- ") == "devicename -- ");
        assert(hide_identifiers(masker, "mode -- 1920x1080x60") == "mode -- 1920x1080x60");
        assert(hide_identifiers(masker, "the uniqueid is a thing") == "the uniqueid is a thing");
    }
    // The normal build: secrets hidden, the home's addresses kept, public ones masked, nothing held back.
    {
        Filter normal;
        for (int i = 0; i < 20; i++) {
            const auto out = normal.take(3, "SSL Verification error :: self-signed certificate", t0 + 1ms * i);
            assert(out.size() == 1 && out[0].message == "SSL Verification error :: self-signed certificate");
        }
        assert((normal.take(2, "client 192.168.1.5 connected", t0) == std::vector<Record>{{2, "client 192.168.1.5 connected"}}));
        assert((normal.take(1, "rikey -- 00", t0) == std::vector<Record>{{1, "rikey -- <hidden>"}}));
        // What the router tells (UPnP) and who tried to pair: the public address goes, the one of the home stays, and the
        // tag says it was public; the same address has the same tag in every record.
        assert((normal.take(2, "UPnP: outer router 192.168.1.1, internet side 203.0.113.9", t0) ==
                std::vector<Record>{{2, "UPnP: outer router 192.168.1.1, internet side <public-1>"}}));
        assert((normal.take(2, "Pairing refused for 203.0.113.9: it isn't from the LAN", t0) ==
                std::vector<Record>{{2, "Pairing refused for <public-1>: it isn't from the LAN"}}));
        assert((normal.take(2, "client [2001:db8::7]:5000 and 100.64.1.2", t0) == std::vector<Record>{{2, "client [<public6-1>]:5000 and 100.64.1.2"}}));
        assert((normal.take(2, "Host -- deck.local:47984 devicename -- Deck", t0) == std::vector<Record>{{2, "Host -- deck.local:47984 devicename -- Deck"}}));
        assert((normal.take(2, "devicename -- Deck", t0) == std::vector<Record>{{2, "devicename -- Deck"}}));
    }
    // The debug build.
    {
        Filter debug({.mask_addresses = true, .limit = true, .burst = 3, .window = 10s, .kinds = 4});
        assert((debug.take(2, "client 192.168.1.5 connected", t0) == std::vector<Record>{{2, "client <lan-1> connected"}}));
        assert((debug.take(2, "Pairing refused for 203.0.113.9 after 192.168.1.5", t0) ==
                std::vector<Record>{{2, "Pairing refused for <public-1> after <lan-1>"}}));
        assert((debug.take(2, "uniqueid -- 0123456789ABCDEF", t0) == std::vector<Record>{{2, "uniqueid -- <id-1>"}}));
        assert((debug.take(2, "devicename -- Deck", t0) == std::vector<Record>{{2, "devicename -- <name-1>"}}));
        assert((debug.take(2, "target :: rtsp://deck.local:48010", t0) == std::vector<Record>{{2, "target :: rtsp://<host-1>:48010"}}));
        assert((debug.take(2, "Received ping [v2] from 192.168.1.5:50035 [0100]", t0) ==
                std::vector<Record>{{2, "Received ping [v2] from <lan-1>:50035 [<hidden>]"}}));
        // Several filters of one run share a masker: the kernel log and sunshine.log name an address alike.
        const auto shared = std::make_shared<psapp::mask::Masker>(psapp::mask::Scope::all);
        Filter file({.mask_addresses = true}, shared), klog({.mask_addresses = true}, shared);
        assert((file.take(2, "from 10.1.2.3", t0) == std::vector<Record>{{2, "from <lan-1>"}}));
        assert((klog.take(2, "also 10.9.9.9 and 10.1.2.3", t0) == std::vector<Record>{{2, "also <lan-2> and <lan-1>"}}));
        // Three lines of a kind pass, the next ones of that kind are held back; another kind passes meanwhile.
        int passed = 0;
        for (int i = 0; i < 10; i++) passed += static_cast<int>(debug.take(3, "frame " + std::to_string(100 + i) + " late", t0 + 1s).size());
        assert(passed == 3);
        assert((debug.take(2, "other", t0 + 2s) == std::vector<Record>{{2, "other"}}));
        // After the window, the next line first says what was held back, with the last one held.
        const auto after = debug.take(1, "other again", t0 + 12s);
        assert(after.size() == 2);
        assert((after[0] == Record{3, "Held back 7 similar lines in 11 s, the last one: frame 109 late"}));
        assert((after[1] == Record{1, "other again"}));
        // A new window lets the kind pass again.
        assert(debug.take(3, "frame 200 late", t0 + 12s).size() == 1);
        // flush_next: everything held back comes out before the next line, whatever the window.
        for (int i = 0; i < 5; i++) (void)debug.take(4, "error 7", t0 + 13s);
        debug.flush_next();
        const auto flushed = debug.take(2, "host stopping", t0 + 14s);
        assert(flushed.size() == 2 && flushed[0].severity == 4 && flushed[0].message.rfind("Held back 2 similar lines", 0) == 0);
        assert(flushed[1].message == "host stopping");
        // More kinds than remembered: the one unused longest goes, with its summary first.
        Filter small({.mask_addresses = false, .limit = true, .burst = 1, .window = 60s, .kinds = 2});
        (void)small.take(2, "a", t0);
        (void)small.take(2, "a", t0);  // held
        (void)small.take(2, "b", t0 + 1s);
        const auto evicted = small.take(2, "c", t0 + 2s);
        assert(evicted.size() == 2 && evicted[0].message == "Held back 1 similar line in 2 s, the last one: a");
        assert(evicted[1].message == "c");
    }
    assert(line_kind("frame 1201 lost after 33 ms") == "frame # lost after # ms");
    std::puts("PASS: log filter (secrets hidden and public addresses masked always; the home's addresses, names and ids masked and floods held back in the debug build)");
    return 0;
}
