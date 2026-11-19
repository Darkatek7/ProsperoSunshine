// SPDX-License-Identifier: GPL-3.0-only
// Optional UPnP port mapping (`upnp` in sunshine.conf, off by default): the decisions and the status the web page shows,
// without sockets. The client that talks to the router is upnp_client.cpp. Unlike Sunshine's upnp.cpp, a port the router
// already forwards to another device is left alone and reported, never deleted: two hosts on one network would otherwise
// take the ports from each other.
#pragma once
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <optional>
#include <sstream>
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

// A description the host gives one of its ports: the prefix and one of stream_mappings()'s names.
inline bool host_label(std::string_view description) {
    if (!description.starts_with(label_prefix)) return false;
    description.remove_prefix(label_prefix.size());
    for (const auto &m : stream_mappings(1, 2, 3, 4, 5, 6))
        if (description == m.name) return true;
    return false;
}

// What the router has for a port before the host maps it, from GetSpecificPortMappingEntry.
enum class Existing {
    none,     // no entry: NoSuchEntryInArray (714), or an empty answer
    mine,     // exact identity match; management ALSO requires a local ownership record
    to_us,    // forwarded here by someone else, by hand for example: leave it, it already works
    other,    // forwarded to another address: leave it and report it
    unknown,  // the router failed to answer: never renew or remove on that; adding is still tried (see map_ports)
};

// `client` is where the router forwards the port, `target` where the host wants it (the console, or the inner router).
// Matching text is not proof of creation: callers must also require a recorded full identity before mutating a rule.
inline Existing classify(int error, std::string_view client, std::string_view internal_port, std::string_view description,
                         std::string_view target, uint16_t port, std::string_view label) {
    if (error == 714) return Existing::none;
    if (error != 0) return Existing::unknown;
    // miniupnpc 2.3.3 returns 0 as soon as the answer has a NewInternalClient element, even an empty one: some routers
    // answer that way for a free port.
    if (client.empty() && internal_port.empty()) return Existing::none;
    if (client.empty() || internal_port.empty()) return Existing::unknown;
    if (client != target || internal_port != std::to_string(port)) return Existing::other;
    return !label.empty() && description == label ? Existing::mine : Existing::to_us;
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

// A router's internet side that is a public IPv4 address: nothing in front of it to forward through. False for a private
// one, and also for no address at all: a failed GetExternalIPAddress leaves the text empty, which says nothing about what
// is in front.
inline bool public_ipv4(std::string_view text) { return private_ipv4(text) == std::optional<bool>(false); }

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
    std::string detail = {}; // the other device's address (taken), or the router's error (failed)
    bool added = false;     // mapped by the host, so removed again when it stops
    bool permanent = false; // added without a lease, as the router takes no other (UPnP error 725)
};

// A mapping the host added, noted in a file in appdata (upnp_permanent.txt, one per line) until the host removed it. A
// mapping without a lease stays on the router until someone removes it, also after a crash, a power cut or a start with
// upnp off, so its line is written before it is added. A mapping with a lease is noted once it is added: when the host
// couldn't remove it at stop (a crash, a removal past the budget, rest mode taking the network first), the next run
// knows it as its own and renews it instead of leaving it to lapse. Records of ports the next run doesn't map are removed
// at its start (upnp_client.cpp). The name of the file is kept from when it held permanent mappings only.
struct PermanentMapping {
    std::string control_url;   // the router's control URL, from its description
    std::string service_type;  // the WAN connection service the mapping was added on
    std::string client;        // where the port went: the console, or the inner router
    bool udp = false;
    uint16_t port = 0;
    std::string description = {};   // empty only in records from 0.6.x (upnp_client.cpp, upgrade_legacy)

    bool operator==(const PermanentMapping &) const = default;
};

// Legacy five-field lines are retained; new lines append the complete quoted description.
inline std::string format_permanent(const PermanentMapping &m) {
    const auto word = [](const std::string &text) {
        if (text.empty()) return false;
        for (const char c : text)
            if (static_cast<unsigned char>(c) <= ' ' || c == 0x7f) return false;
        return true;
    };
    if (!word(m.control_url) || !word(m.service_type) || !word(m.client) || m.port == 0) return {};
    std::string line = m.control_url + ' ' + m.service_type + ' ' + m.client + (m.udp ? " UDP " : " TCP ") + std::to_string(m.port);
    if (m.description.find_first_of("\r\n") != std::string::npos) return {};
    if (!m.description.empty()) {
        std::ostringstream quoted;
        quoted << ' ' << std::quoted(m.description);
        line += quoted.str();
    }
    return line;
}

// The lines of the file; a line that isn't one of format_permanent()'s is left out.
inline std::vector<PermanentMapping> parse_permanent(std::string_view text) {
    std::vector<PermanentMapping> list;
    while (!text.empty()) {
        const size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        std::istringstream entry{std::string(line)};
        PermanentMapping m;
        std::string protocol, port;
        if (!(entry >> m.control_url >> m.service_type >> m.client >> protocol >> port)) continue;
        if (protocol != "TCP" && protocol != "UDP") continue;
        if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos) continue;
        const auto number = std::stoul(port);
        if (number == 0 || number > 65535) continue;
        m.port = static_cast<uint16_t>(number);
        m.udp = protocol == "UDP";
        entry >> std::ws;
        if (!entry.eof()) {
            if (entry.peek() != '"' || !(entry >> std::quoted(m.description))) continue;
            entry >> std::ws;
            if (!entry.eof() || m.description.empty()) continue;
        }
        if (!format_permanent(m).empty()) list.push_back(std::move(m));
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

// Merge by FULL identity. A new destination must not erase an unresolved record for an older destination.
inline std::vector<PermanentMapping> merge_permanent(std::vector<PermanentMapping> list, const std::vector<PermanentMapping> &added,
                                                     const std::vector<PermanentMapping> &removed) {
    std::vector<PermanentMapping> kept;
    for (const auto &m : list) {
        bool replaced = false;
        for (const auto &r : removed) replaced = replaced || m == r;
        for (const auto &a : added) replaced = replaced || m == a;
        if (!replaced) kept.push_back(m);
    }
    for (const auto &a : added) {
        bool twice = false;
        for (const auto &k : kept) twice = twice || k == a;
        if (!twice) kept.push_back(a);
    }
    return kept;
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
    bool tied = false;
    for (int i = 0; i < static_cast<int>(hosts.size()); ++i) {
        if (hosts[i] == inner_router) continue;
        const int bits = shared_prefix(inner_external, hosts[i]);
        if (bits > best_bits) {
            best = i;
            best_bits = bits;
            tied = false;
        } else if (bits == best_bits && best >= 0 && hosts[i] != hosts[best]) {
            tied = true;
        }
    }
    return tied ? -1 : best;
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
