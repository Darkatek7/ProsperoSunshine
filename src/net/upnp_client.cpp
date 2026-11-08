// SPDX-License-Identifier: GPL-3.0-only
#include "net/upnp_client.hpp"

#include "src/logging.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>

#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>

using namespace std::literals;

namespace prospero::upnp {

namespace {

constexpr auto refresh_interval = 120s;
// Passes in a row without an answer before a router counts as gone. Discovery answers get lost now and then (the ISP router
// here misses one every few passes), and one lost answer must not drop working mappings.
constexpr int misses_before_gone = 3;
constexpr const char *lease_seconds = "3600";  // renewed every 2 minutes; gone within the hour if the host dies

std::mutex status_mutex;
Status current;

void publish(const Status &status) {
    std::lock_guard lock(status_mutex);
    current = status;
}

std::string error_text(int error) {
    const char *text = strupnperror(error);
    return text ? std::string(text) + " (" + std::to_string(error) + ')' : "error " + std::to_string(error);
}

// A router that answered: its control URL and service, kept to remove the mappings when the host stops.
struct Router {
    std::string host;
    std::string control_url;
    std::string service_type;
    std::string lan_address;  // the console's address as seen on the way to it
};

// Every router that answered discovery and describes a WAN connection service.
std::vector<Router> find_routers() {
    std::vector<Router> routers;
    int error = 0;
    UPNPDev *devices = upnpDiscover(2000, nullptr, nullptr, 0, 0, 2, &error);
    for (UPNPDev *device = devices; device; device = device->pNext) {
        UPNPUrls urls{};
        IGDdatas data{};
        char lan[64]{};
        if (!UPNP_GetIGDFromUrl(device->descURL, &urls, &data, lan, sizeof lan)) continue;
        if (urls.controlURL && *urls.controlURL && *data.first.servicetype)
            routers.push_back({url_host(device->descURL), urls.controlURL, data.first.servicetype, lan});
        FreeUPNPUrls(&urls);
    }
    freeUPNPDevlist(devices);
    return routers;
}

// The router the console is attached to. Not UPNP_GetValidIGD's choice: behind two routers, the outer one's discovery
// often reaches the console too, and as the one with the public address miniupnpc prefers it. It can't reach the
// console's address, though, and a mapping there to the console takes the ports away from the forwarding the inner
// router relies on. The router the console is attached to is the one whose address shares the longest prefix with the
// console's; the first of equals wins.
int inner_router(const std::vector<Router> &routers) {
    int best = -1, best_bits = -2;
    for (int i = 0; i < static_cast<int>(routers.size()); ++i) {
        const int bits = shared_prefix(routers[i].lan_address, routers[i].host);
        if (bits > best_bits) best = i, best_bits = bits;
    }
    return best;
}

std::string external_address(const Router &router) {
    char external[64]{};
    UPNP_GetExternalIPAddress(router.control_url.c_str(), router.service_type.c_str(), external);
    return external;
}

// One pass over the ports on one router, forwarding them to `client`: the console on the inner router, the inner router's
// address on the outer one.
void map_ports(const Router &router, const std::string &client, std::vector<PortStatus> &ports) {
    const char *control = router.control_url.c_str();
    const char *service = router.service_type.c_str();
    for (auto &port : ports) {
        const std::string number = std::to_string(port.mapping.port);
        const char *proto = port.mapping.udp ? "UDP" : "TCP";
        char other[64]{}, internal_port[8]{}, description[80]{}, enabled[8]{}, lease[16]{};
        const int lookup = UPNP_GetSpecificPortMappingEntry(control, service, number.c_str(), proto, nullptr, other,
                                                            internal_port, description, enabled, lease);
        switch (classify(lookup, other, description, client)) {
        case Existing::other:
            port.outcome = Outcome::taken;
            port.detail = other;
            port.added = false;
            continue;
        case Existing::to_us:
            port.outcome = Outcome::open;
            port.detail.clear();
            port.added = false;
            continue;
        case Existing::mine:
            if (renew_by_removing(lease)) UPNP_DeletePortMapping(control, service, number.c_str(), proto, nullptr);
            break;
        case Existing::none:
        case Existing::unknown:
            break;
        }
        const std::string label = std::string(label_prefix) + port.mapping.name;
        int added = UPNP_AddPortMapping(control, service, number.c_str(), number.c_str(), client.c_str(), label.c_str(),
                                        proto, nullptr, lease_seconds);
        // Some routers refuse to add a mapping that is already there, even the same one (718 ConflictInMappingEntry from
        // the ISP router here), and the first lookup can fail on a busy router: ask again. If the port already goes to
        // the target, by the host's own mapping or by someone else's, it is open.
        if (added != UPNPCOMMAND_SUCCESS) {
            char again[64]{}, again_port[8]{}, again_description[80]{}, again_enabled[8]{}, again_lease[16]{};
            const int check = UPNP_GetSpecificPortMappingEntry(control, service, number.c_str(), proto, nullptr, again,
                                                               again_port, again_description, again_enabled, again_lease);
            const Existing now = classify(check, again, again_description, client);
            if (now == Existing::mine) added = UPNPCOMMAND_SUCCESS;
            if (now == Existing::to_us) {
                port.outcome = Outcome::open;
                port.detail.clear();
                port.added = false;
                continue;
            }
        }
        // 725 OnlyPermanentLeasesSupported, and old routers that refuse any lease: a permanent one, removed at stop.
        if (added != UPNPCOMMAND_SUCCESS)
            added = UPNP_AddPortMapping(control, service, number.c_str(), number.c_str(), client.c_str(), label.c_str(),
                                        proto, nullptr, "0");
        if (added == UPNPCOMMAND_SUCCESS) {
            port.outcome = Outcome::open;
            port.detail.clear();
            port.added = true;
        } else {
            port.outcome = Outcome::failed;
            port.detail = error_text(added);
        }
    }
}

void unmap(const Router &router, const std::vector<PortStatus> &ports) {
    for (const auto &port : ports) {
        if (!port.added) continue;  // also after a later pass failed: the mapping may still be there
        const std::string number = std::to_string(port.mapping.port);
        const char *proto = port.mapping.udp ? "UDP" : "TCP";
        const int removed =
            UPNP_DeletePortMapping(router.control_url.c_str(), router.service_type.c_str(), number.c_str(), proto, nullptr);
        if (removed != UPNPCOMMAND_SUCCESS && removed != 714)
            BOOST_LOG(warning) << "UPnP: "sv << router.host << ' ' << proto << ' ' << number << " not removed: "sv
                               << error_text(removed);
    }
}

// Log lines for what changed since the last pass.
void log_hop(const char *which, const Hop &hop, const Hop &before) {
    if (hop.router.empty()) {
        if (!before.router.empty()) BOOST_LOG(info) << "UPnP: the "sv << which << " router stopped answering"sv;
        return;
    }
    if (hop.router != before.router || hop.external != before.external) {
        BOOST_LOG(info) << "UPnP: "sv << which << " router "sv << hop.router << ", internet side "sv
                        << (hop.external.empty() ? "unknown"s : hop.external)
                        << (hop.external_private ? " (private: another router or the provider's NAT is in front)"sv : ""sv);
    }
    for (std::size_t i = 0; i < hop.ports.size(); ++i) {
        const auto &port = hop.ports[i];
        const bool same = i < before.ports.size() && before.router == hop.router && port.outcome == before.ports[i].outcome &&
                          port.detail == before.ports[i].detail;
        if (same) continue;
        if (port.outcome == Outcome::open)
            BOOST_LOG(info) << "UPnP: "sv << hop.router << ' ' << describe(port);
        else
            BOOST_LOG(warning) << "UPnP: "sv << hop.router << ' ' << describe(port);
    }
}

std::vector<PortStatus> fresh(const std::vector<Mapping> &mappings) {
    std::vector<PortStatus> ports;
    for (const auto &mapping : mappings) ports.push_back({mapping});
    return ports;
}

// Carries a hop's port results over to the next pass while it is the same router, so added mappings are renewed and
// removed at stop; a different router starts afresh.
void keep_or_reset(Hop &hop, const std::string &router, const std::vector<Mapping> &mappings) {
    if (hop.router != router) {
        hop = Hop{};
        hop.router = router;
        hop.ports = fresh(mappings);
    }
}

}  // namespace

void run(std::stop_token stop, std::vector<Mapping> mappings) {
    Status status;
    status.state = State::searching;
    publish(status);
    BOOST_LOG(info) << "UPnP: looking for the router to forward Moonlight's ports (upnp in sunshine.conf)"sv;

    Router inner, outer;
    int inner_misses = 0, outer_misses = 0;
    std::mutex wait_mutex;
    std::condition_variable_any wake;
    for (;;) {
        const Status before = status;
        const auto routers = find_routers();
        const int i = inner_router(routers);
        if (i < 0) {
            if (status.inner.router.empty() || ++inner_misses >= misses_before_gone) {
                if (before.state != State::no_router)
                    BOOST_LOG(warning) << "UPnP: no router answered; forward the ports by hand or turn UPnP on in the router"sv;
                status.state = State::no_router;
            }
        } else {
            inner_misses = 0;
            status.state = State::ready;
            if (!status.inner.router.empty() && status.inner.router != routers[i].host) unmap(inner, status.inner.ports);
            inner = routers[i];
            keep_or_reset(status.inner, inner.host, mappings);
            status.inner.external = external_address(inner);
            status.inner.external_private = private_ipv4(status.inner.external).value_or(false);
            map_ports(inner, inner.lan_address, status.inner.ports);
            log_hop("console's", status.inner, before.inner);

            // Behind two routers: the one in front, if it answered too, forwards the same ports to the inner router.
            std::vector<std::string> hosts;
            for (const auto &router : routers) hosts.push_back(router.host);
            const int o = status.inner.external_private ? outer_router(status.inner.external, inner.host, hosts) : -1;
            if (o >= 0) {
                outer_misses = 0;
                if (!status.outer.router.empty() && status.outer.router != routers[o].host) unmap(outer, status.outer.ports);
                outer = routers[o];
                keep_or_reset(status.outer, outer.host, mappings);
                status.outer.external = external_address(outer);
                status.outer.external_private = private_ipv4(status.outer.external).value_or(false);
                map_ports(outer, status.inner.external, status.outer.ports);
            } else if (!status.inner.external_private) {
                // The console's router is on the internet now: nothing in front to forward through.
                if (!status.outer.router.empty()) unmap(outer, status.outer.ports);
                status.outer = Hop{};
            } else if (!status.outer.router.empty() && ++outer_misses >= misses_before_gone) {
                status.outer = Hop{};  // gone: what it still forwards lapses within the hour
            }
            // Else the router in front didn't answer this time: its mappings stay, and the next pass renews them.
            log_hop("outer", status.outer, before.outer);
        }
        publish(status);
        std::unique_lock lock(wait_mutex);
        if (wake.wait_for(lock, stop, refresh_interval, [] { return false; }) || stop.stop_requested()) break;
    }
    if (status.state == State::ready) {
        BOOST_LOG(info) << "UPnP: removing the port mappings"sv;
        if (!status.outer.router.empty()) unmap(outer, status.outer.ports);
        unmap(inner, status.inner.ports);
    }
    publish(Status{});
}

Status status() {
    std::lock_guard lock(status_mutex);
    return current;
}

}  // namespace prospero::upnp
