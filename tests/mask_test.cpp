// SPDX-License-Identifier: GPL-3.0-only
// The masker every posted file uses (app/src/psapp/mask.hpp): what an address, a MAC address, a host name and a key become, that
// the same value has the same tag, and what must stay.
#include "../app/src/psapp/mask.hpp"
#include <cassert>
#include <cstdio>
#include <string>

using psapp::mask::Masker;
using psapp::mask::Scope;

int main() {
    // ---- IPv4: the tag says what it was --------------------------------------------------------------------------------------
    {
        Masker m;
        assert(m.apply("console 192.168.1.20 port 9021") == "console <lan-1> port 9021");
        assert(m.apply("10.0.0.1:47989") == "<lan-2>:47989");
        assert(m.apply("172.16.5.5 172.31.255.1 172.32.0.1") == "<lan-3> <lan-4> <public-1>");
        assert(m.apply("from 100.64.0.5.") == "from <tailscale-1>.");
        assert(m.apply("100.127.255.254 100.128.0.1") == "<tailscale-2> <public-2>");
        assert(m.apply("169.254.12.1") == "<link-1>");
        assert(m.apply("203.0.113.9 and 198.51.100.7") == "<public-3> and <public-4>");
        // The same address, the same tag: in another line too, with another port, and written with a leading zero.
        assert(m.apply("again 192.168.1.20:5555 and 192.168.1.020") == "again <lan-1>:5555 and <lan-1>");
        assert(m.apply("203.0.113.9") == "<public-3>");
        // What names nobody stays: loopback (a Tailscale forward), "any", broadcast, multicast, netmasks.
        for (const char *kept : {"127.0.0.1", "127.1.2.3:47995", "0.0.0.0", "255.255.255.255", "224.0.0.251", "255.255.255.0", "0.6.0.1"})
            assert(m.apply(kept) == kept);
    }
    // ---- What isn't an address ---------------------------------------------------------------------------------------------------
    {
        Masker m;
        for (const char *kept : {"firmware 0x10200006 (10.20)", "TSC 1.59627 ticks/ns", "host version=0.1.7 commit=ddd51c45", "59.94 fps",
                                 "app 01.000.008", "record 2c8020000000000 28000000", "QP at 0xc4/0xc8 20/24", "256.1.1.1", "1.2.3", "1.2.3.4.5",
                                 "1.2.3.4567", "12:34:56", "deadbeef:00:11:22:33:44", "[2026-11-06 12:00:00.123]: Info: x", "std::string",
                                 "boost::asio", "payload :: v=0", "a:b:c", "12345::6", "aa:bb:cc:dd:ee", "RTSP/1.0 200 OK"})
            assert(m.apply(kept) == kept);
    }
    // ---- IPv6 ------------------------------------------------------------------------------------------------------------------
    {
        Masker m;
        assert(m.apply("[2001:db8:0:0:0:0:2:1]:47989 and 2001:db8::5") == "[<public6-1>]:47989 and <public6-2>");
        assert(m.apply("client 2001:DB8::5 asked") == "client <public6-2> asked");  // upper case is the same address
        assert(m.apply("[fd7a:115c:a1e0::1234]:47989") == "[<tailscale6-1>]:47989");
        assert(m.apply("fd12:3456:789a::1") == "<lan6-1>");
        assert(m.apply("fe80::1%em0") == "<link6-1>%em0");
        assert(m.apply("at 2001:db8:0:0:0:0:2:1.") == "at <public6-1>.");
        assert(m.apply("fe80::1: failed") == "<link6-1>: failed");
        // Sunshine writes a peer as <address>:<port> without brackets: after eight groups or when the last group is too long
        // for one the port is cut off the address; within "::" a short one can't be told from a last group and goes with it.
        Masker peer;
        assert(peer.apply("from 2001:db8::5:50035 [x]") == "from <public6-1>:50035 [x]");
        assert(peer.apply("RAISE: fe80::1:50935 :: type") == "RAISE: <link6-1>:50935 :: type");
        assert(peer.apply("2001:db8::5:50036") == "<public6-1>:50036");
        assert(peer.apply("2001:db8:1:2:3:4:5:6:50035") == "<public6-2>:50035");
        assert(peer.apply("1:50035 and ab:1234 and 12:34:56:50035 and 1:2:3:4:5:6:7") == "1:50035 and ab:1234 and 12:34:56:50035 and 1:2:3:4:5:6:7");
        assert(peer.apply("[2001:db8:1:2:3:4:5:6:5]") == "[<public6-2>:5]");  // a short port after eight groups too
        // Loopback, "any" and multicast stay.
        for (const char *kept : {"::1", "::", "ff02::fb", "[::1]:47990"}) assert(m.apply(kept) == kept);
        // IPv4-mapped: the IPv4 address, with the tag of the plain one.
        Masker mapped;
        assert(mapped.apply("client ::ffff:192.168.1.5 asked") == "client <lan-1> asked");
        assert(mapped.apply("192.168.1.5 and ::FFFF:192.168.1.5") == "<lan-1> and <lan-1>");
        assert(mapped.apply("::ffff:203.0.113.4") == "<public-1>");
        assert(mapped.apply("::ffff:127.0.0.1") == "::ffff:127.0.0.1");
        assert(mapped.apply("64:ff9b::198.51.100.1.") == "<public6-1>.");
    }
    // ---- MAC addresses ---------------------------------------------------------------------------------------------------------
    {
        Masker m;
        assert(m.apply("mac 00:1A:2b:3C:4d:5E end") == "mac <mac-1> end");
        assert(m.apply("mac a0-b1-c2-d3-e4-f5") == "mac <mac-2>");
        assert(m.apply("again 00:1a:2b:3c:4d:5e or 00-1a-2b-3c-4d-5e") == "again <mac-1> or <mac-1>");
        assert(m.apply("00:1a:2b:3c:4d:5e:6f") == "00:1a:2b:3c:4d:5e:6f");  // seven pairs: not a MAC
    }
    // ---- Scope::internet: only what is public -------------------------------------------------------------------------------
    {
        Masker m(Scope::internet);
        assert(m.apply("pairing refused for 203.0.113.9: from the internet") == "pairing refused for <public-1>: from the internet");
        assert(m.apply("UPnP: router 192.168.1.1, internet side 198.51.100.77") == "UPnP: router 192.168.1.1, internet side <public-2>");
        assert(m.apply("client 192.168.1.5 100.64.1.1 169.254.1.1 fe80::1 fd7a:115c:a1e0::5 mac 00:1a:2b:3c:4d:5e") ==
               "client 192.168.1.5 100.64.1.1 169.254.1.1 fe80::1 fd7a:115c:a1e0::5 mac 00:1a:2b:3c:4d:5e");
        assert(m.apply("[2001:db8::7]:1 and ::ffff:203.0.113.9") == "[<public6-1>]:1 and <public-1>");
        assert(m.apply("rtsp://deck.local:48010/") == "rtsp://deck.local:48010/");  // host names are for Scope::all
        assert(m.apply("Host :: ps5.example.org") == "Host :: ps5.example.org");
    }
    // ---- Host names (Scope::all) -----------------------------------------------------------------------------------------------
    {
        Masker m;
        assert(m.apply("target :: rtsp://deck.local:48010") == "target :: rtsp://<host-1>:48010");
        assert(m.apply("rtspenc://Deck.Local:48010/x") == "rtspenc://<host-1>:48010/x");  // case doesn't matter
        assert(m.apply("Host -- ps5.tail1234.ts.net:47984") == "Host -- <host-2>:47984");
        assert(m.apply("Host :: ps5.tail1234.ts.net") == "Host :: <host-2>");
        assert(m.apply("Host: ps5.tail1234.ts.net\r") == "Host: <host-2>\r");
        assert(m.apply("Found Host: deck.local") == "Found Host: <host-1>");
        assert(Masker().apply("Host: deck:48010\r") == "Host: <host-1>:48010\r");
        // The report's own "Host: ..." line is no header.
        for (const char *kept : {"Host: running, version 0.6.0, commit 8fa42cd5", "Host: running", "Host: not running", "host: QUIT answered \"OK bye \", host stopped",
                                 "Host: running streaming"})
            assert(m.apply(kept) == kept);
        assert(m.apply("x-forwarded-host :: home.example.org") == "x-forwarded-host :: <host-3>");
        assert(m.apply(std::string("https://user:pw") + "@home.example.org:8443/path?x=1") == "https://<userinfo>@<host-3>:8443/path?x=1");
        // An address stays an address (and gets its tag), "Host: 0.0.0.0" is what Sunshine reads for the audio quality.
        assert(m.apply("Host -- 192.168.1.9:47984") == "Host -- <lan-1>:47984");
        assert(m.apply("Found Host: 0.0.0.0") == "Found Host: 0.0.0.0");
        assert(m.apply("target :: rtsp://[2001:db8::1]:48010") == "target :: rtsp://[<public6-1>]:48010");
        assert(m.apply("rtsp://192.168.1.9:48010") == "rtsp://<lan-1>:48010");
        // Not hosts.
        for (const char *kept : {"Hosts: 3", "hostname -- deck", "CSeq: 4", "://", "a://", "Info: Host stays"})
            assert(m.apply(kept) == kept);
    }
    // ---- stand_in, reserve, limits ---------------------------------------------------------------------------------------------
    {
        Masker m;
        assert(m.stand_in("id", "abc") == "<id-1>");
        assert(m.stand_in("name", "abc") == "<name-1>");  // another category counts on its own
        assert(m.stand_in("id", "abc") == "<id-1>");
        assert(m.stand_in("id", "def") == "<id-2>");
        // Tags already in a text (a log the host masked) are not given out again.
        Masker later;
        later.reserve("seen <lan-7> and <id-3> and <public6-12>, <x-> <y-z> <lan-> <1>");
        assert(later.apply("192.168.1.1") == "<lan-8>");
        assert(later.stand_in("id", "k") == "<id-4>");
        assert(later.apply("2001:db8::1") == "<public6-13>");
        // A masker that has seen `limit` values gives the next ones the tag without a number, and still knows the old ones.
        Masker small(Scope::all, 2);
        assert(small.apply("192.168.1.1 192.168.1.2 192.168.1.3 192.168.1.1") == "<lan-1> <lan-2> <lan> <lan-1>");
    }
    // ---- Long input stays cheap and whole ---------------------------------------------------------------------------------------
    {
        Masker m;
        const std::string line = std::string(5000, ':') + std::string(5000, 'f') + "1.2.3." + std::string(3000, '9');
        assert(m.apply(line).size() > 1000);
        std::string many;
        for (int i = 1; i <= 300; ++i) many += "10.0." + std::to_string(i % 200) + "." + std::to_string(i % 250) + " ";
        assert(m.apply(many).find("10.0.") == std::string::npos);
    }
    std::puts("PASS: masker (IPv4, IPv6, MAC with kinds and stable tags, host names, scopes, reserve, limit)");
    return 0;
}
