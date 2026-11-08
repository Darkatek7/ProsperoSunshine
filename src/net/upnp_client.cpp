// SPDX-License-Identifier: GPL-3.0-only
#include "net/upnp_client.hpp"

#include "src/logging.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

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
// How long the host waits at stop for the routers to remove the mappings. Each miniupnpc call can take up to 3 s to connect
// and 5 s per read, so a router that accepts connections but doesn't answer would otherwise hold up "Restart host" and the
// restart of the services after rest mode.
constexpr auto removal_budget = 3s;
constexpr int upnp_error_only_permanent_leases = 725;  // OnlyPermanentLeasesSupported

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
std::vector<Router> find_routers(const std::stop_token &stop) {
    // All searches go out together and the answers are awaited once (searchalltypes). upnpDiscover() searches its types
    // one after the other and waits 2 s for each: 8 s when no router answers, which a stop can't interrupt.
    static const char *const types[] = {"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                                        "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
                                        "urn:schemas-upnp-org:service:WANIPConnection:1",
                                        "urn:schemas-upnp-org:service:WANPPPConnection:1", nullptr};
    std::vector<Router> routers;
    std::set<std::string> asked;  // a router answers for several types: its description is fetched once
    int error = 0;
    UPNPDev *devices = upnpDiscoverDevices(types, 2000, nullptr, nullptr, 0, 0, 2, &error, 1);
    for (UPNPDev *device = devices; device && !stop.stop_requested(); device = device->pNext) {
        if (!asked.insert(device->descURL).second) continue;
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
// Returns early when the host stops: between ports, and between the lookup and the add.
void map_ports(const Router &router, const std::string &client, std::vector<PortStatus> &ports, const std::stop_token &stop) {
    const char *control = router.control_url.c_str();
    const char *service = router.service_type.c_str();
    for (auto &port : ports) {
        if (stop.stop_requested()) return;
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
        if (stop.stop_requested()) return;
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
        // A router that only takes permanent mappings: one without a lease. It stays on the router until the host removes
        // it, so only for that answer, not for any failure (README, "Forwarding the ports by UPnP").
        if (added == upnp_error_only_permanent_leases && !stop.stop_requested())
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

// Removes the mappings the host added; returns a line for each the router refused. No logging here: it also runs on the
// removal thread, which may outlive the wait at stop.
std::vector<std::string> unmap(const Router &router, const std::vector<PortStatus> &ports) {
    std::vector<std::string> refused;
    for (const auto &port : ports) {
        if (!port.added) continue;  // also after a later pass failed: the mapping may still be there
        const std::string number = std::to_string(port.mapping.port);
        const char *proto = port.mapping.udp ? "UDP" : "TCP";
        const int removed =
            UPNP_DeletePortMapping(router.control_url.c_str(), router.service_type.c_str(), number.c_str(), proto, nullptr);
        if (removed != UPNPCOMMAND_SUCCESS && removed != 714)
            refused.push_back(router.host + ' ' + proto + ' ' + number + " not removed: " + error_text(removed));
    }
    return refused;
}

void unmap_now(const Router &router, const std::vector<PortStatus> &ports) {
    for (const auto &line : unmap(router, ports)) BOOST_LOG(warning) << "UPnP: "sv << line;
}

// The removal at stop runs on a thread of its own, and the host waits for it at most removal_budget. A run() that starts
// before it ends (the services start anew after rest mode) waits for it before its first pass, so the old removal never
// takes away what the new run maps.
// Never freed: a removal thread may still run while the process exits, after static objects are destroyed.
struct Removals {
    std::mutex mutex;
    std::condition_variable_any done;
    int running = 0;
};
Removals &removals() {
    static Removals *instance = new Removals;
    return *instance;
}

void remove_at_stop(const Router &inner, const Hop &inner_hop, const Router &outer, const Hop &outer_hop) {
    {
        std::lock_guard lock(removals().mutex);
        ++removals().running;
    }
    auto refused = std::make_shared<std::promise<std::vector<std::string>>>();
    auto result = refused->get_future();
    std::thread([inner, inner_ports = inner_hop.ports, outer, outer_ports = outer_hop.ports,
                 has_inner = !inner_hop.router.empty(), has_outer = !outer_hop.router.empty(), refused] {
        std::vector<std::string> lines;
        if (has_outer) lines = unmap(outer, outer_ports);
        if (has_inner) {
            auto more = unmap(inner, inner_ports);
            lines.insert(lines.end(), more.begin(), more.end());
        }
        refused->set_value(std::move(lines));
        std::lock_guard lock(removals().mutex);
        --removals().running;
        removals().done.notify_all();
    }).detach();
    if (result.wait_for(removal_budget) != std::future_status::ready) {
        BOOST_LOG(warning) << "UPnP: the router is slow to remove the mappings; the host goes on, and they lapse within the hour"sv;
        return;
    }
    for (const auto &line : result.get()) BOOST_LOG(warning) << "UPnP: "sv << line;
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
    {
        std::unique_lock lock(removals().mutex);
        if (!removals().done.wait(lock, stop, [] { return removals().running == 0; })) return;
    }
    BOOST_LOG(info) << "UPnP: looking for the router to forward Moonlight's ports (upnp in sunshine.conf)"sv;

    Router inner, outer;
    int inner_misses = 0, outer_misses = 0;
    std::mutex wait_mutex;
    std::condition_variable_any wake;
    for (;;) {
        const Status before = status;
        const auto routers = find_routers(stop);
        if (stop.stop_requested()) break;
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
            if (!status.inner.router.empty() && status.inner.router != routers[i].host) unmap_now(inner, status.inner.ports);
            inner = routers[i];
            keep_or_reset(status.inner, inner.host, mappings);
            status.inner.external = external_address(inner);
            status.inner.external_private = private_ipv4(status.inner.external).value_or(false);
            map_ports(inner, inner.lan_address, status.inner.ports, stop);
            log_hop("console's", status.inner, before.inner);
            if (stop.stop_requested()) break;

            // Behind two routers: the one in front, if it answered too, forwards the same ports to the inner router.
            std::vector<std::string> hosts;
            for (const auto &router : routers) hosts.push_back(router.host);
            const int o = status.inner.external_private ? outer_router(status.inner.external, inner.host, hosts) : -1;
            if (o >= 0) {
                outer_misses = 0;
                if (!status.outer.router.empty() && status.outer.router != routers[o].host) unmap_now(outer, status.outer.ports);
                outer = routers[o];
                keep_or_reset(status.outer, outer.host, mappings);
                status.outer.external = external_address(outer);
                status.outer.external_private = private_ipv4(status.outer.external).value_or(false);
                map_ports(outer, status.inner.external, status.outer.ports, stop);
            } else if (!status.inner.external_private) {
                // The console's router is on the internet now: nothing in front to forward through.
                if (!status.outer.router.empty()) unmap_now(outer, status.outer.ports);
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
    // Whatever the last pass found: also after the routers stopped answering, the mappings the host added are worth a try.
    if (!status.inner.router.empty() || !status.outer.router.empty()) {
        BOOST_LOG(info) << "UPnP: removing the port mappings"sv;
        remove_at_stop(inner, status.inner, outer, status.outer);
    }
    publish(Status{});
}

Status status() {
    std::lock_guard lock(status_mutex);
    return current;
}

}  // namespace prospero::upnp
