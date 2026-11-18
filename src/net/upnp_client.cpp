// SPDX-License-Identifier: GPL-3.0-only
#include "net/upnp_client.hpp"
#include "net/upnp_lookup.hpp"

#include "platform/ps5/diag.h"
#include "src/logging.h"
#include "src/platform/common.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
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
// After a pass that found no router, during the run's first minute: the next pass comes sooner. After rest mode the
// services start anew before the network is back, and the first pass finds nothing (seen on 13.00); without this the ports
// stayed closed until the next pass, two minutes later. "No router answered" is logged only once that minute is over.
constexpr auto quick_retry = 15s;
constexpr auto quick_period = 60s;
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

// A read that failed on the way (UPNPCOMMAND_HTTP_ERROR) is sent once more. A router can drop a request now and then (an
// ISP router here fails about one SOAP call in 250), and a router that reports internal port 0 is checked against its
// whole table, ~40 calls per port: one dropped call left that port failed for a pass while its rule was there all along.
// Only reads are repeated.
constexpr int upnp_http_error = -3;
template<class Read>
upnp_identity::Entry read_twice(Read read) {
    auto entry = read();
    return entry.result == upnp_http_error ? read() : entry;
}

// The fallback is restricted to recorded identities and cross-checks both router query forms.
upnp_identity::Entry read_mapping(const std::string &control, const std::string &service,
                                 const std::string &target, const std::string &number,
                                 const std::string &protocol, const std::string &label, bool recorded) {
    const auto specific = [&] {
        return read_twice([&] {
            char client[64]{}, port[6]{}, description[80]{}, enabled[4]{}, lease[16]{};
            const int result = UPNP_GetSpecificPortMappingEntry(control.c_str(), service.c_str(), number.c_str(),
                protocol.c_str(), nullptr, client, port, description, enabled, lease);
            return upnp_identity::Entry{result, client, port, description, enabled, lease};
        });
    };
    const auto generic = [&](std::size_t index) {
        return read_twice([&] {
            char external[6]{}, client[64]{}, port[6]{}, proto[4]{}, description[80]{}, enabled[4]{}, remote[64]{}, lease[16]{};
            const auto text = std::to_string(index);
            const int result = UPNP_GetGenericPortMappingEntry(control.c_str(), service.c_str(), text.c_str(),
                external, client, port, proto, description, enabled, remote, lease);
            return upnp_identity::Entry{result, client, port, description, enabled, lease, external, proto, remote};
        });
    };
    return upnp_identity::lookup({target, number, label, number, protocol}, recorded, specific, generic);
}

// A router that answered: its control URL and service, kept to remove the mappings when the host stops.
struct Router {
    std::string host;
    std::string control_url;
    std::string service_type;
    std::string lan_address;  // the console's address as seen on the way to it
};

// The mappings the host added without a lease, noted until it removed them (upnp.hpp, PermanentMapping). Never freed: the
// removal thread may still note while the process exits.
std::mutex &permanent_mutex() {
    static std::mutex *instance = new std::mutex;
    return *instance;
}

std::filesystem::path permanent_file() { return platf::appdata() / "upnp_permanent.txt"; }

std::vector<PermanentMapping> read_permanent() {
    std::ifstream in(permanent_file());
    std::ostringstream text;
    text << in.rdbuf();
    return parse_permanent(text.str());
}

// Commit recovery intent durably BEFORE a permanent add. A failed write must not expose an unrecorded port.
bool change_permanent(const std::vector<PermanentMapping> &added, const std::vector<PermanentMapping> &removed) {
    if (added.empty() && removed.empty()) return true;
    std::lock_guard lock(permanent_mutex());
    std::error_code error;
    const bool exists = std::filesystem::exists(permanent_file(), error);
    if (error || (exists && !std::ifstream(permanent_file()).good())) return false;
    const auto before = read_permanent();
    const auto after = merge_permanent(before, added, removed);
    const auto path = permanent_file();
    const auto temporary = path.string() + ".tmp";
    for (const auto &entry : after) if (format_permanent(entry).empty()) return false;
    const auto text = format_permanent_list(after);
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    std::size_t offset = 0;
    bool ok = true;
    while (offset < text.size()) {
        const auto count = ::write(fd, text.data() + offset, text.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { ok = false; break; }
        offset += static_cast<std::size_t>(count);
    }
    if (ok) ok = ::fsync(fd) == 0;
    if (::close(fd) != 0) ok = false;
    if (!ok || ::rename(temporary.c_str(), path.c_str()) != 0) return false;
    const int parent = ::open(path.parent_path().c_str(), O_RDONLY);
    if (parent < 0) return false;
    ok = ::fsync(parent) == 0;
    if (::close(parent) != 0) ok = false;
    return ok;
}

struct OwnedMapping {
    PermanentMapping identity;
    bool permanent = false;  // added without a lease
    bool journaled = false;  // its line is in upnp_permanent.txt, retired when the mapping is gone
    std::string reported = {};  // the last line unmap() logged for it, so a retained record isn't logged every pass
};
using OwnedMappings = std::vector<OwnedMapping>;

// Every mapping noted by an earlier run, as the host's own: only its own mappings get a line.
OwnedMappings load_owned() {
    OwnedMappings owned;
    std::lock_guard lock(permanent_mutex());
    for (auto &m : read_permanent()) owned.push_back({std::move(m), false, true});
    return owned;
}

auto find_owned(OwnedMappings &owned, const PermanentMapping &identity) {
    return std::find_if(owned.begin(), owned.end(), [&](const auto &item) { return item.identity == identity; });
}

upnp_identity::Entry read_mapping(const PermanentMapping &m, bool recorded) {
    return read_mapping(m.control_url, m.service_type, m.client, std::to_string(m.port), m.udp ? "UDP" : "TCP",
                        m.description, recorded);
}

Existing classify(const upnp_identity::Entry &entry, const PermanentMapping &wanted) {
    return prospero::upnp::classify(entry.result, entry.client, entry.internal_port, entry.description, wanted.client, wanted.port,
                    wanted.description);
}

// A record from 0.6.x has no description. It is adopted once when the router still forwards that exact port to that exact
// client under one of the host's own descriptions: the line is rewritten with it, and from then on the record is managed
// like any other. Anything less (no answer, another client or port, a description the host doesn't give) keeps it as it is.
bool upgrade_legacy(OwnedMapping &record, const upnp_identity::Entry &entry) {
    const auto &m = record.identity;
    if (!m.description.empty() || entry.result != 0 || entry.client != m.client ||
        entry.internal_port != std::to_string(m.port) || !host_label(entry.description))
        return false;
    auto upgraded = m;
    upgraded.description = entry.description;
    if (!change_permanent({upgraded}, {m})) return false;
    record.identity = std::move(upgraded);
    return true;
}

// Remove only fresh matches. Keep ambiguous records for retry. A record that is refused again for the same reason is
// logged once, unless `every_time` (the removal at stop).
std::vector<std::string> unmap(OwnedMappings &owned, const std::string &control = {}, const PermanentMapping *replacement = nullptr,
                               bool every_time = false) {
    std::vector<std::string> refused;
    const auto refuse = [&](OwnedMapping &record, std::string line) {
        if (every_time || record.reported != line) refused.push_back(line);
        record.reported = std::move(line);
        return false;
    };
    // Whether the record is done with: removed from the router (or gone from it) and retired from the file.
    const auto settle = [&](OwnedMapping &record) {
        const auto &m = record.identity;
        if (!control.empty() && m.control_url != control) return false;
        if (replacement && (m.service_type != replacement->service_type || m.port != replacement->port ||
                            m.udp != replacement->udp || m == *replacement)) return false;
        const auto entry = read_mapping(m, !m.description.empty());
        // Upgraded into the very mapping being renewed: it is the record of that mapping now, not one to replace.
        if (upgrade_legacy(record, entry) && replacement && m == *replacement) return false;
        const auto state = classify(entry, m);
        if (state != Existing::none && (m.description.empty() || state != Existing::mine))
            return refuse(record, std::to_string(m.port) + " ownership could not be confirmed; retained");
        if (state == Existing::mine) {
            const auto number = std::to_string(m.port);
            const int removed = UPNP_DeletePortMapping(m.control_url.c_str(), m.service_type.c_str(), number.c_str(),
                                                       m.udp ? "UDP" : "TCP", nullptr);
            if (removed != 0 && removed != 714) return refuse(record, number + " not removed: " + error_text(removed));
        }
        if (record.journaled && !change_permanent({}, {m}))
            return refuse(record, std::to_string(m.port) + " recovery record could not be retired");
        return true;
    };
    for (auto it = owned.begin(); it != owned.end();) it = settle(*it) ? owned.erase(it) : std::next(it);
    return refused;
}

// The records an earlier run left, at the start of a run. Those of the ports this run maps stay the host's, and its passes
// renew them: after rest mode the previous run's mappings are often still on the router. The others (an earlier Base port,
// a record from 0.6.x) are removed now; what can't be removed stays recorded, and is tried again at stop.
OwnedMappings recover(const std::vector<Mapping> &mappings) {
    OwnedMappings owned = load_owned(), stale;
    for (auto it = owned.begin(); it != owned.end();) {
        const auto &m = it->identity;
        const bool mapped_now = std::any_of(mappings.begin(), mappings.end(), [&](const Mapping &mapping) {
            return mapping.port == m.port && mapping.udp == m.udp && m.description == std::string(label_prefix) + mapping.name;
        });
        if (mapped_now) {
            ++it;
            continue;
        }
        stale.push_back(std::move(*it));
        it = owned.erase(it);
    }
    for (const auto &line : unmap(stale)) BOOST_LOG(warning) << "UPnP: "sv << line;
    std::move(stale.begin(), stale.end(), std::back_inserter(owned));
    return owned;
}

void unmap_now(OwnedMappings &owned, const Router &router) {
    for (const auto &line : unmap(owned, router.control_url)) BOOST_LOG(warning) << "UPnP: "sv << line;
}

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
void map_ports(const Router &router, const std::string &client, std::vector<PortStatus> &ports,
               const std::stop_token &stop, OwnedMappings &owned) {
    for (auto &port : ports) {
        if (stop.stop_requested()) return;
        const std::string number = std::to_string(port.mapping.port);
        const char *proto = port.mapping.udp ? "UDP" : "TCP";
        const PermanentMapping wanted{router.control_url, router.service_type, client, port.mapping.udp,
                                      port.mapping.port, std::string(label_prefix) + port.mapping.name};
        // An address change must not overwrite or forget the old identity. Remove it only if still verified as ours.
        for (const auto &line : unmap(owned, router.control_url, &wanted)) BOOST_LOG(warning) << "UPnP: "sv << line;
        const auto recorded = find_owned(owned, wanted);
        const bool previously_owned = recorded != owned.end();
        port.added = previously_owned;
        port.permanent = previously_owned && recorded->permanent;
        auto entry = read_mapping(wanted, previously_owned);
        const auto state = classify(entry, wanted);
        if (state == Existing::other) {
            port.outcome = Outcome::taken;
            port.detail = entry.client;
            continue;
        }
        if (state == Existing::to_us || (state == Existing::mine && !previously_owned)) {
            port.added = false;
            port.outcome = entry.enabled == "1" ? Outcome::open : Outcome::failed;
            port.detail = entry.enabled == "1" ? "" : "existing mapping is disabled; unchanged";
            continue;
        }
        if (stop.stop_requested()) return;
        if (state == Existing::mine) {
            // Noted once: a mapping an earlier run added and recorded, or one whose line couldn't be written then.
            if (!recorded->journaled) recorded->journaled = change_permanent({wanted}, {});
            if (entry.lease == "0") {
                if (!recorded->journaled) {
                    port.outcome = Outcome::failed;
                    port.detail = "recovery record could not be written";
                    continue;
                }
                recorded->permanent = port.permanent = true;
                port.outcome = entry.enabled == "1" ? Outcome::open : Outcome::failed;
                continue;
            }
            if (renew_by_removing(entry.lease)) {
                const int removed = UPNP_DeletePortMapping(router.control_url.c_str(), router.service_type.c_str(),
                                                           number.c_str(), proto, nullptr);
                if (removed != 0 && removed != 714) {
                    port.outcome = Outcome::failed;
                    port.detail = error_text(removed);
                    continue;
                }
            }
        }
        // No entry, the host's own to renew, or no answer (Existing::unknown): some routers don't implement the lookup
        // (401, 501). The add is still tried then: a router must refuse with 718 a port forwarded to another client, and
        // only a successful add makes the mapping the host's.
        if (stop.stop_requested()) return;
        const bool noted = previously_owned && recorded->journaled;
        bool permanent = false, intent_written = false;
        const auto add_leased = [&] {
            return UPNP_AddPortMapping(router.control_url.c_str(), router.service_type.c_str(), number.c_str(), number.c_str(),
                                       client.c_str(), wanted.description.c_str(), proto, nullptr, lease_seconds);
        };
        int added = add_leased();
        // A leased add that failed on the way is sent once more, like a read. If the first one did reach the router, the
        // repeat answers 0 or 718, and a 718 never makes a new mapping the host's: at worst it lapses with its lease, as
        // without the repeat. The permanent add below is not repeated: a 718 there would retire the record of a rule that
        // exists.
        if (added == upnp_http_error && !stop.stop_requested()) added = add_leased();
        if (added == upnp_error_only_permanent_leases && !stop.stop_requested()) {
            if (!noted) {
                if (!change_permanent({wanted}, {})) {
                    port.outcome = Outcome::failed;
                    port.detail = "permanent mapping refused: recovery intent could not be synced";
                    continue;
                }
                intent_written = true;
            }
            if (stop.stop_requested()) {
                if (intent_written) change_permanent({}, {wanted});
                return;
            }
            added = UPNP_AddPortMapping(router.control_url.c_str(), router.service_type.c_str(), number.c_str(),
                                        number.c_str(), client.c_str(), wanted.description.c_str(), proto, nullptr, "0");
            permanent = added == 0;
        }
        if (added == 0) {
            // A mapping with a lease is noted now that it exists. If the line can't be written, the host still owns it
            // for this run and tries the line again next pass; a later run would leave it to lapse.
            const bool journaled = noted || intent_written || change_permanent({wanted}, {});
            if (previously_owned) {
                recorded->permanent = permanent;
                recorded->journaled = journaled;
            } else {
                owned.push_back({wanted, permanent, journaled});
            }
            port.outcome = Outcome::open;
            port.detail.clear();
            port.added = true;
            port.permanent = permanent;
        } else if (added == 718) {
            // A conflict proves no creation. Never adopt a matching pre-existing rule or retain a new creation intent.
            if (intent_written && !change_permanent({}, {wanted}))
                BOOST_LOG(warning) << "UPnP: unused recovery intent could not be retired"sv;
            entry = read_mapping(wanted, previously_owned);
            const auto now = classify(entry, wanted);
            port.outcome = entry.enabled == "1" && (now == Existing::mine || now == Existing::to_us)
                               ? Outcome::open : Outcome::failed;
            port.detail = port.outcome == Outcome::open ? "" : error_text(added);
        } else {
            // A lost response after a permanent add may hide success: retain the durable intent for next-start recovery.
            port.outcome = Outcome::failed;
            port.detail = error_text(added);
        }
    }
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

// One thread per router, all within the same budget: a slow router must not use up a fast one's time. A router that
// reports internal port 0 is checked against its whole table (~0.7 s per port on an ISP router here), and removing its six
// mappings first left the inner router's untouched.
void remove_at_stop(OwnedMappings owned) {
    std::map<std::string, OwnedMappings> by_router;
    for (auto &record : owned) by_router[record.identity.control_url].push_back(std::move(record));
    std::vector<std::future<std::vector<std::string>>> results;
    for (auto &[control, records] : by_router) {
        {
            std::lock_guard lock(removals().mutex);
            ++removals().running;
        }
        auto refused = std::make_shared<std::promise<std::vector<std::string>>>();
        results.push_back(refused->get_future());
        std::thread([records = std::move(records), refused]() mutable {
            auto lines = unmap(records, {}, nullptr, true);
            refused->set_value(std::move(lines));
            std::lock_guard lock(removals().mutex);
            --removals().running;
            removals().done.notify_all();
        }).detach();
    }
    const auto deadline = std::chrono::steady_clock::now() + removal_budget;
    std::vector<std::string> refused_lines;
    bool late = false;
    for (auto &result : results) {
        if (result.wait_until(deadline) != std::future_status::ready) {
            late = true;
            continue;
        }
        for (auto &line : result.get()) refused_lines.push_back(std::move(line));
    }
    // Also in host_diag.log, which is written at once: around rest mode the other logs can miss these lines.
    if (late) {
        ps_diag_log("ProsperoSunshine: host UPnP removal not done within %lld s\n", static_cast<long long>(removal_budget.count()));
        BOOST_LOG(warning) << "UPnP: the router is slow to remove the mappings; the host goes on, and what is left stays recorded for the next start"sv;
    } else {
        ps_diag_log("ProsperoSunshine: host UPnP removal done, %zu refused\n", refused_lines.size());
    }
    for (const auto &line : refused_lines) BOOST_LOG(warning) << "UPnP: "sv << line;
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

// Carries display status between passes; ownership lives separately and survives these resets.
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
    OwnedMappings owned = recover(mappings);

    Router inner, outer;
    int inner_misses = 0, outer_misses = 0;
    const auto started = std::chrono::steady_clock::now();
    const auto early = [&] { return std::chrono::steady_clock::now() - started < quick_period; };
    std::mutex wait_mutex;
    std::condition_variable_any wake;
    for (;;) {
        const Status before = status;
        const auto routers = find_routers(stop);
        if (stop.stop_requested()) break;
        const int i = inner_router(routers);
        if (i < 0) {
            if (status.inner.router.empty() ? !early() : ++inner_misses >= misses_before_gone) {
                if (before.state != State::no_router)
                    BOOST_LOG(warning) << "UPnP: no router answered; forward the ports by hand or turn UPnP on in the router"sv;
                status.state = State::no_router;
            }
        } else {
            inner_misses = 0;
            status.state = State::ready;
            if (!status.inner.router.empty() && status.inner.router != routers[i].host) unmap_now(owned, inner);
            inner = routers[i];
            keep_or_reset(status.inner, inner.host, mappings);
            status.inner.external = external_address(inner);
            status.inner.external_private = private_ipv4(status.inner.external).value_or(false);
            map_ports(inner, inner.lan_address, status.inner.ports, stop, owned);
            log_hop("console's", status.inner, before.inner);
            if (stop.stop_requested()) break;

            // Behind two routers: the one in front, if it answered too, forwards the same ports to the inner router.
            std::vector<std::string> hosts;
            for (const auto &router : routers) hosts.push_back(router.host);
            const int o = status.inner.external_private ? outer_router(status.inner.external, inner.host, hosts) : -1;
            if (o >= 0) {
                outer_misses = 0;
                if (!status.outer.router.empty() && status.outer.router != routers[o].host) unmap_now(owned, outer);
                outer = routers[o];
                keep_or_reset(status.outer, outer.host, mappings);
                status.outer.external = external_address(outer);
                status.outer.external_private = private_ipv4(status.outer.external).value_or(false);
                map_ports(outer, status.inner.external, status.outer.ports, stop, owned);
            } else if (!status.inner.external_private) {
                // The console's router is on the internet now: nothing in front to forward through.
                if (!status.outer.router.empty()) unmap_now(owned, outer);
                status.outer = Hop{};
            } else if (!status.outer.router.empty() && ++outer_misses >= misses_before_gone) {
                status.outer = Hop{};  // gone: what it still forwards lapses within the hour
            }
            // Else the router in front didn't answer this time: its mappings stay, and the next pass renews them.
            log_hop("outer", status.outer, before.outer);
        }
        publish(status);
        std::unique_lock lock(wait_mutex);
        const auto next = i < 0 && early() ? std::min<std::chrono::seconds>(quick_retry, refresh_interval) : refresh_interval;
        if (wake.wait_for(lock, stop, next, [] { return false; }) || stop.stop_requested()) break;
    }
    // Whatever the last pass found: also after the routers stopped answering, the mappings the host added are worth a try.
    if (!owned.empty()) {
        BOOST_LOG(info) << "UPnP: removing the port mappings"sv;
        remove_at_stop(std::move(owned));
    }
    publish(Status{});
}

Status status() {
    std::lock_guard lock(status_mutex);
    return current;
}

void remove_leftovers(std::stop_token stop) {
    // Serialize with previous shutdown cleanup, also when UPnP is disabled.
    {
        std::unique_lock lock(removals().mutex);
        if (!removals().done.wait(lock, stop, [] { return removals().running == 0; })) return;
    }
    if (stop.stop_requested()) return;
    recover({});  // no port mapped now: every record is a leftover
}

}  // namespace prospero::upnp
