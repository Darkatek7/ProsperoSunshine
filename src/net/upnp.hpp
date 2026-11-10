// SPDX-License-Identifier: GPL-3.0-only
// Optional UPnP port mapping (`upnp` in sunshine.conf, off by default): the decisions and the status the web page shows,
// without sockets. The client that talks to the router is upnp_client.cpp. Unlike Sunshine's upnp.cpp, a port the router
// already forwards to another device is left alone and reported, never deleted: two hosts on one network would otherwise
// take the ports from each other.
#pragma once
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prospero::upnp {

// One port the host asks the router to forward, to the same port on the console.
struct Mapping {
    uint16_t port = 0;
    bool udp = false;
    std::string name;  // shown in the router's list after "ProsperoSunshine "
};

// Moonlight's six ports, from the host's ports after Base port: never the web page or the control socket, which answer
// only the LAN and Tailscale.
inline std::vector<Mapping> stream_mappings(uint16_t https, uint16_t http, uint16_t rtsp, uint16_t video,
                                            uint16_t control, uint16_t audio) {
    return {{https, false, "HTTPS"}, {http, false, "HTTP"},     {rtsp, false, "RTSP"},
            {video, true, "Video"},  {control, true, "Control"}, {audio, true, "Audio"}};
}

// The description the host gives its mappings: "ProsperoSunshine " and the port's name.
inline constexpr std::string_view label_prefix = "ProsperoSunshine ";

// What the router has for a port before the host maps it, from GetSpecificPortMappingEntry.
enum class Existing {
    none,     // no entry (UPnP error 714, or an empty answer)
    mine,     // forwarded here by the host (its description): renew it, and remove it when the host stops
    to_us,    // forwarded here by someone else, by hand for example: leave it, it already works
    other,    // forwarded to another address: leave it and report it
    unknown,  // the router failed to answer: try to map anyway
};

// `client` is where the router forwards the port, `target` where the host wants it (the console, or the inner router).
// Routers report hand-made rules with any lease (0, or 4294967295 on some), so only the description tells the host's own
// mappings from them.
inline Existing classify(int error, std::string_view client, std::string_view description, std::string_view target) {
    if (error == 714) return Existing::none;
    if (error != 0) return Existing::unknown;
    if (client.empty()) return Existing::none;
    if (client != target) return Existing::other;
    return description.starts_with(label_prefix) ? Existing::mine : Existing::to_us;
}

// A dotted IPv4 address the internet can't reach: private, carrier-grade NAT, link-local, loopback or unspecified.
// Empty when the text is no IPv4 address.
inline std::optional<bool> private_ipv4(std::string_view text) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    char tail = 0;
    const std::string copy(text);
    if (std::sscanf(copy.c_str(), "%3u.%3u.%3u.%3u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 ||
        d > 255)
        return std::nullopt;
    return a == 10 || a == 127 || a == 0 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
           (a == 169 && b == 254) || (a == 100 && b >= 64 && b <= 127);
}

// Whether the host's own mapping must be removed before it is added again. Some routers accept a repeated AddPortMapping
// but keep the old lease (a TP-Link Archer C5400X does), so the mapping would lapse after an hour for up to one refresh
// interval. Removed and added again, it gets a full lease. Only near the end, so a stream rarely sees the gap.
inline bool renew_by_removing(std::string_view lease_left) {
    unsigned long seconds = 0;
    if (lease_left.empty() || lease_left.size() > 10) return false;
    for (char c : lease_left) {
        if (c < '0' || c > '9') return false;
        seconds = seconds * 10 + static_cast<unsigned>(c - '0');
    }
    return seconds > 0 && seconds < 600;  // 0 is a permanent mapping, from a router that refused the lease
}

// The leading bits two dotted IPv4 addresses share, or -1 when either is no IPv4 address.
inline int shared_prefix(std::string_view a, std::string_view b) {
    const auto parse = [](std::string_view text) -> std::optional<uint32_t> {
        unsigned p[4]{};
        char tail = 0;
        const std::string copy(text);
        if (std::sscanf(copy.c_str(), "%3u.%3u.%3u.%3u%c", &p[0], &p[1], &p[2], &p[3], &tail) != 4) return std::nullopt;
        uint32_t value = 0;
        for (unsigned part : p) {
            if (part > 255) return std::nullopt;
            value = value << 8 | part;
        }
        return value;
    };
    const auto x = parse(a), y = parse(b);
    if (!x || !y) return -1;
    int bits = 0;
    for (uint32_t diff = *x ^ *y; bits < 32 && !(diff & 0x80000000u); diff <<= 1) ++bits;
    return bits;
}

// The host of a description URL such as "http://192.168.1.100:1900/rootDesc.xml".
inline std::string url_host(std::string_view url) {
    if (const auto scheme = url.find("://"); scheme != std::string_view::npos) url.remove_prefix(scheme + 3);
    return std::string(url.substr(0, url.find_first_of(":/")));
}

enum class Outcome { pending, open, taken, failed };
enum class State { off, searching, no_router, ready };

struct PortStatus {
    Mapping mapping;
    Outcome outcome = Outcome::pending;
    std::string detail;     // the other device's address (taken), or the router's error (failed)
    bool added = false;     // mapped by the host, so removed again when it stops
    bool permanent = false; // added without a lease, as the router takes no other (UPnP error 725)
};

// A mapping the host added without a lease. It stays on the router until someone removes it, also after a crash, a power cut
// or a start with upnp off, when the host doesn't remove it at stop. So the host notes each in a file in appdata
// (upnp_permanent.txt, one per line) and removes the ones still noted at its next start (upnp_client.cpp).
struct PermanentMapping {
    std::string control_url;   // the router's control URL, from its description
    std::string service_type;  // the WAN connection service the mapping was added on
    std::string client;        // where the port went: the console, or the inner router
    bool udp = false;
    uint16_t port = 0;

    bool operator==(const PermanentMapping &) const = default;
};

// "<control url> <service type> <client> <TCP|UDP> <port>"; empty when a field would not stay one word.
inline std::string format_permanent(const PermanentMapping &m) {
    const auto word = [](const std::string &text) {
        if (text.empty()) return false;
        for (const char c : text)
            if (static_cast<unsigned char>(c) <= ' ' || c == 0x7f) return false;
        return true;
    };
    if (!word(m.control_url) || !word(m.service_type) || !word(m.client) || m.port == 0) return {};
    return m.control_url + ' ' + m.service_type + ' ' + m.client + (m.udp ? " UDP " : " TCP ") + std::to_string(m.port);
}

// The lines of the file; a line that isn't one of format_permanent()'s is left out.
inline std::vector<PermanentMapping> parse_permanent(std::string_view text) {
    std::vector<PermanentMapping> list;
    while (!text.empty()) {
        const size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        std::vector<std::string> words;
        while (!line.empty()) {
            const size_t space = line.find(' ');
            if (space != 0) words.emplace_back(line.substr(0, space));
            line = space == std::string_view::npos ? std::string_view() : line.substr(space + 1);
        }
        if (words.size() != 5 || (words[3] != "TCP" && words[3] != "UDP") || words[4].empty() || words[4].size() > 5) continue;
        unsigned long port = 0;
        bool digits = true;
        for (const char c : words[4]) {
            digits = digits && c >= '0' && c <= '9';
            port = port * 10 + static_cast<unsigned>(c - '0');
        }
        if (!digits || port == 0 || port > 65535) continue;
        list.push_back({words[0], words[1], words[2], words[3] == "UDP", static_cast<uint16_t>(port)});
    }
    return list;
}

// The file's text for a list: one line each, none twice.
inline std::string format_permanent_list(const std::vector<PermanentMapping> &list) {
    std::string text;
    std::vector<PermanentMapping> written;
    for (const auto &m : list) {
        const std::string line = format_permanent(m);
        if (line.empty()) continue;
        bool seen = false;
        for (const auto &w : written) seen = seen || w == m;
        if (seen) continue;
        written.push_back(m);
        text += line + '\n';
    }
    return text;
}

// The noted list after some mappings were added and others removed. A router has one mapping per port and protocol, so that is
// what identifies one; a mapping added again replaces its old note (the console may have another address now).
inline std::vector<PermanentMapping> merge_permanent(std::vector<PermanentMapping> list, const std::vector<PermanentMapping> &added,
                                                     const std::vector<PermanentMapping> &removed) {
    const auto same_port = [](const PermanentMapping &a, const PermanentMapping &b) {
        return a.control_url == b.control_url && a.service_type == b.service_type && a.udp == b.udp && a.port == b.port;
    };
    std::vector<PermanentMapping> kept;
    for (const auto &m : list) {
        bool replaced = false;
        for (const auto &r : removed) replaced = replaced || same_port(m, r);
        for (const auto &a : added) replaced = replaced || same_port(m, a);
        if (!replaced) kept.push_back(m);
    }
    for (const auto &a : added) {
        bool twice = false;
        for (const auto &k : kept) twice = twice || same_port(k, a);
        if (!twice) kept.push_back(a);
    }
    return kept;
}

// Whether a mapping noted earlier is one of the ports the host maps now: the run renews those itself, by port and protocol,
// as it doesn't know yet which router answers. The others are left from an earlier Base port, or from a run with upnp on
// when it's off now.
inline bool mapped_now(const PermanentMapping &m, const std::vector<Mapping> &mappings) {
    for (const auto &now : mappings)
        if (now.port == m.port && now.udp == m.udp) return true;
    return false;
}

// One router and what it forwards. `router` is empty while there is none.
struct Hop {
    std::string router;             // the router's own address in the network
    std::string external;           // its internet side, as it reports it
    bool external_private = false;  // that address is private: another router or the provider's NAT sits in front
    std::vector<PortStatus> ports;
};

// The router the console is attached to, and behind two routers the one in front of it, when that one answers too: its
// ports go to the inner router's address on its side, so the stream crosses both.
struct Status {
    State state = State::off;
    Hop inner;
    Hop outer;
};

// The router in front of `inner_external` among the routers that answered, by address: the one sharing the longest
// prefix with it, at least 16 bits (one network), and not the inner router itself. -1 when there is none.
inline int outer_router(std::string_view inner_external, std::string_view inner_router, const std::vector<std::string> &hosts) {
    int best = -1, best_bits = 15;
    for (int i = 0; i < static_cast<int>(hosts.size()); ++i) {
        if (hosts[i] == inner_router) continue;
        const int bits = shared_prefix(inner_external, hosts[i]);
        if (bits > best_bits) best = i, best_bits = bits;
    }
    return best;
}

inline const char *name(Outcome outcome) {
    switch (outcome) {
    case Outcome::open: return "open";
    case Outcome::taken: return "taken";
    case Outcome::failed: return "failed";
    case Outcome::pending: break;
    }
    return "pending";
}

inline const char *name(State state) {
    switch (state) {
    case State::searching: return "searching";
    case State::no_router: return "no_router";
    case State::ready: return "ready";
    case State::off: break;
    }
    return "off";
}

// One line for the log when a port's outcome changed, e.g. "UDP 47999 taken by 192.168.1.20".
inline std::string describe(const PortStatus &port) {
    std::string line = std::string(port.mapping.udp ? "UDP " : "TCP ") + std::to_string(port.mapping.port) + ' ';
    switch (port.outcome) {
    case Outcome::open: line += port.added ? "mapped" : "already forwarded there, left as it is"; break;
    case Outcome::taken: line += "forwarded to another device (" + port.detail + "), left as it is"; break;
    case Outcome::failed: line += "not mapped: " + port.detail; break;
    case Outcome::pending: line += "pending"; break;
    }
    return line;
}

}  // namespace prospero::upnp
