// SPDX-License-Identifier: GPL-3.0-only
#include "net/upnp.hpp"
#include <cassert>
#include <iostream>
using namespace prospero::upnp;

int main() {
    // Moonlight's six ports, never the web page (base + 1) or the control socket (base + 6).
    const auto ports = stream_mappings(47984, 47989, 48010, 47998, 47999, 48000);
    assert(ports.size() == 6);
    int tcp = 0, udp = 0;
    for (const auto &m : ports) {
        assert(m.port != 47990 && m.port != 47995);
        (m.udp ? udp : tcp)++;
    }
    assert(tcp == 3 && udp == 3);
    assert(!ports[0].udp && ports[0].port == 47984 && ports[3].udp && ports[3].port == 47998);

    // Full endpoint and description matches still need a recorded creation before management.
    const auto check = [](int error, std::string_view client, std::string_view port, std::string_view label) {
        return classify(error, client, port, label, "198.51.100.88", 47998, "ProsperoSunshine Video");
    };
    assert(check(714, "", "", "") == Existing::none);
    assert(check(0, "", "", "") == Existing::none);  // miniupnpc 2.3.3 returns 0 for an empty NewInternalClient
    assert(check(0, "", "47998", "") == Existing::unknown);
    assert(check(0, "198.51.100.88", "", "") == Existing::unknown);
    assert(check(0, "198.51.100.88", "47998", "ProsperoSunshine Video") == Existing::mine);
    assert(check(0, "198.51.100.88", "47999", "ProsperoSunshine Video") == Existing::other);
    assert(check(0, "198.51.100.88", "47998", "ProsperoSunshine Video manual") == Existing::to_us);
    assert(check(0, "198.51.100.89", "47998", "ProsperoSunshine Video") == Existing::other);
    assert(check(501, "198.51.100.88", "47998", "ProsperoSunshine Video") == Existing::unknown);

    // Addresses the internet can't reach.
    for (const char *p : {"10.0.0.86", "192.168.1.1", "172.16.0.1", "172.31.255.255", "100.64.0.1", "100.127.1.1",
                          "169.254.1.1", "127.0.0.1", "0.0.0.0"})
        assert(private_ipv4(p) == std::optional<bool>(true));
    for (const char *p : {"8.8.8.8", "172.15.0.1", "172.32.0.1", "100.63.255.255", "100.128.0.1", "192.169.0.1", "11.0.0.1"})
        assert(private_ipv4(p) == std::optional<bool>(false));
    for (const char *p : {"", "1.2.3", "1.2.3.4.5", "256.1.1.1", "a.b.c.d", "1.2.3.4x", "::1"})
        assert(!private_ipv4(p));

    // Permanent mappings noted in a file: one line each, read back the same, nothing that isn't one word per field.
    const PermanentMapping inner{"http://router.example:5000/ctl/IPConn", "urn:schemas-upnp-org:service:WANIPConnection:1",
                                 "198.51.100.88", true, 47998};
    const PermanentMapping https{inner.control_url, inner.service_type, inner.client, false, 47984};
    assert(format_permanent(inner) ==
           "http://router.example:5000/ctl/IPConn urn:schemas-upnp-org:service:WANIPConnection:1 198.51.100.88 UDP 47998");
    const auto list = parse_permanent(format_permanent_list({inner, https, inner}));
    assert(list.size() == 2 && list[0] == inner && list[1] == https);
    assert(format_permanent({"http://a b", "s", "c", false, 1}).empty() && format_permanent({"u", "s", "", false, 1}).empty());
    assert(format_permanent({"u", "s", "c", false, 0}).empty() && format_permanent({"u", "s\nx", "c", false, 1}).empty());
    assert(parse_permanent("").empty());
    assert(parse_permanent("u s c TCP 0\nu s c TCP 65536\nu s c XYZ 1\nu s c TCP\nu s c TCP 1x\nu s c d TCP 1\n").empty());
    assert(parse_permanent("u s c TCP 65535").size() == 1 && parse_permanent("u s c TCP 65535")[0].port == 65535);
    assert(format_permanent_list({}).empty());
    // Notes added and removed: by router, port and protocol; a mapping added again replaces its note.
    PermanentMapping moved = inner;
    moved.client = "198.51.100.89";
    assert(merge_permanent({}, {inner, https}, {}) == (std::vector<PermanentMapping>{inner, https}));
    assert(merge_permanent({inner, https}, {moved}, {}) == (std::vector<PermanentMapping>{inner, https, moved}));
    assert(merge_permanent({inner, https}, {}, {{inner.control_url, inner.service_type, "", true, 47998}}).size() == 2);
    assert(merge_permanent({inner}, {}, {{"http://other.example/ctl", inner.service_type, "", true, 47998}}).size() == 1);
    assert(merge_permanent({inner, https}, {}, {inner, https}).empty());
    assert(merge_permanent({inner}, {inner, inner}, {}).size() == 1);

    // A lease near its end is renewed by removing the mapping first; a permanent one (0) or a fresh one is not.
    assert(renew_by_removing("599") && renew_by_removing("1") && renew_by_removing("120"));
    assert(!renew_by_removing("600") && !renew_by_removing("3416") && !renew_by_removing("0"));
    assert(!renew_by_removing("") && !renew_by_removing("4294967295") && !renew_by_removing("12a") && !renew_by_removing("-5"));

    // The router closest to the console: the longest shared prefix.
    assert(shared_prefix("192.168.249.88", "192.168.1.100") == 16);
    assert(shared_prefix("192.168.249.88", "10.0.0.1") == 0);
    assert(shared_prefix("192.168.1.5", "192.168.1.1") == 29);
    assert(shared_prefix("10.0.0.2", "10.0.0.2") == 32);
    assert(shared_prefix("192.168.1.5", "router.local") == -1);
    assert(shared_prefix("", "10.0.0.1") == -1);
    assert(url_host("http://192.168.1.100:1900/rootDesc.xml") == "192.168.1.100");
    assert(url_host("http://10.0.0.1:52136/rootDesc.xml") == "10.0.0.1");
    assert(url_host("http://192.168.63.12/desc") == "192.168.63.12");
    assert(url_host("192.168.63.12") == "192.168.63.12");

    // The router in front: on the inner router's internet side, at least one /16, never the inner router itself.
    const std::vector<std::string> hosts{"192.168.1.100", "10.0.0.1", "192.168.63.12"};
    assert(outer_router("10.0.0.86", "192.168.1.100", hosts) == 1);
    assert(outer_router("172.20.5.9", "192.168.1.100", hosts) == -1);  // the provider's NAT: nobody to ask
    assert(outer_router("192.168.1.7", "192.168.1.100", hosts) == 2);  // 192.168.63.12 shares 18 bits
    assert(outer_router("10.0.0.86", "10.0.0.1", {"10.0.0.1"}) == -1);

    assert(outer_router("198.51.100.86", "203.0.113.1", {"198.51.100.1", "198.51.100.2"}) == -1);
    auto labelled = inner;
    labelled.description = "ProsperoSunshine Video";
    assert(parse_permanent(format_permanent_list({labelled})).front() == labelled);
    assert(merge_permanent({inner, labelled}, {}, {labelled}) == std::vector<PermanentMapping>{inner});
    assert(parse_permanent("u s c TCP 1 \"unterminated").empty());

    // Log lines.
    PortStatus p{{47999, true, "Control"}, Outcome::taken, "192.168.1.20", false};
    assert(describe(p) == "UDP 47999 forwarded to another device (192.168.1.20), left as it is");
    p = {{47989, false, "HTTP"}, Outcome::open, "", true};
    assert(describe(p) == "TCP 47989 mapped");
    p.added = false;
    assert(describe(p) == "TCP 47989 already forwarded there, left as it is");
    p = {{48010, false, "RTSP"}, Outcome::failed, "ConflictInMappingEntry (718)", false};
    assert(describe(p) == "TCP 48010 not mapped: ConflictInMappingEntry (718)");
    assert(std::string(name(State::off)) == "off" && std::string(name(State::ready)) == "ready");
    assert(std::string(name(Outcome::taken)) == "taken");
    assert(Status{}.state == State::off && Status{}.outer.router.empty());

    std::cout << "upnp_test: ok\n";
    return 0;
}
