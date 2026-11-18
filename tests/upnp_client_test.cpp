// SPDX-License-Identifier: GPL-3.0-only
// Exercise the production client with fake MiniUPnPc I/O; no router or network is contacted.
#include "../src/net/upnp_client.cpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <tuple>
#include <sys/syscall.h>
using namespace prospero::upnp;
using upnp_identity::Entry;
using Key = std::tuple<std::string, std::string, std::string>;
static std::map<Key, Entry> rules;
static int adds, deletes, lookup_error, add_error, delete_error, generic_error, fsyncs;
static bool broken_port, only_permanent, conflict_on_add, conflict_on_permanent, lose_permanent_reply, fail_sync, empty_answer;
// The removal at stop runs one thread per router: the fake router state is shared, so every call takes this lock.
static std::recursive_mutex stub_mutex;
// drop_read: the read (specific or generic, counted in read_calls) that fails once on the way, as a router dropping a request.
static int read_calls, drop_read;
// drop_add: the add (counted in adds) that fails once with -3; drop_add_lost: it reached the router, only the reply was lost.
static int drop_add;
static bool drop_add_lost;
// Every read on slow_url waits slow_delay first (outside the lock), as a router that checks each port slowly.
static std::string slow_url;
static std::chrono::milliseconds slow_delay{0};
static bool dropped(const char *url) {
    if (slow_url == url) std::this_thread::sleep_for(slow_delay);
    std::lock_guard lock(stub_mutex);
    return ++read_calls == drop_read;
}
extern "C" int fsync(int fd) {
    ++fsyncs;
    if (fail_sync) { errno = EIO; return -1; }
    return static_cast<int>(::syscall(SYS_fsync, fd));
}
static void copy(char *out, const std::string &value) { std::strcpy(out, value.c_str()); }
UPNPDev *upnpDiscoverDevices(const char *const *, int, const char *, const char *, int, int, unsigned char, int *, int) { return nullptr; }
int UPNP_GetIGDFromUrl(const char *, UPNPUrls *, IGDdatas *, char *, int) { return 0; }
void FreeUPNPUrls(UPNPUrls *) {}
void freeUPNPDevlist(UPNPDev *) {}
int UPNP_GetExternalIPAddress(const char *, const char *, char *) { return -1; }
const char *strupnperror(int) { return "fake error"; }
extern "C" void ps_diag_log(const char *, ...) {}
int UPNP_GetSpecificPortMappingEntry(const char *url, const char *, const char *port, const char *proto, const char *,
                                   char *client, char *internal, char *desc, char *enabled, char *lease) {
    if (dropped(url)) return -3;
    std::lock_guard lock(stub_mutex);
    if (lookup_error) return lookup_error;
    auto it = rules.find({url, port, proto});
    // miniupnpc 2.3.3 returns 0 when a router answers a free port with an empty NewInternalClient.
    if (it == rules.end() && empty_answer) {
        for (char *out : {client, internal, desc, enabled, lease}) copy(out, "");
        return 0;
    }
    if (it == rules.end()) return 714;
    const auto &r = it->second;
    copy(client, r.client); copy(internal, broken_port ? "0" : r.internal_port); copy(desc, r.description);
    copy(enabled, r.enabled); copy(lease, r.lease);
    return 0;
}
int UPNP_GetGenericPortMappingEntry(const char *url, const char *, const char *index, char *external, char *client,
                                  char *internal, char *proto, char *desc, char *enabled, char *remote, char *lease) {
    if (dropped(url)) return -3;
    std::lock_guard lock(stub_mutex);
    if (generic_error) return generic_error;
    auto offset = std::stoul(index);
    for (const auto &[key, r] : rules) {
        if (std::get<0>(key) != url) continue;
        if (offset-- != 0) continue;
        copy(external, std::get<1>(key)); copy(proto, std::get<2>(key)); copy(client, r.client);
        copy(internal, r.internal_port); copy(desc, "stale generic description bytes");
        copy(enabled, r.enabled); copy(remote, r.remote_host); copy(lease, r.lease);
        return 0;
    }
    return 713;
}
int UPNP_AddPortMapping(const char *url, const char *service, const char *port, const char *internal,
                       const char *client, const char *desc, const char *proto, const char *, const char *lease) {
    std::lock_guard lock(stub_mutex);
    ++adds;
    if (add_error) return add_error;
    if (only_permanent && std::string_view(lease) != "0") return 725;
    if (std::string_view(lease) == "0") {
        const PermanentMapping identity{url, service, client, std::string_view(proto) == "UDP",
                                         static_cast<uint16_t>(std::stoi(port)), desc};
        auto noted = read_permanent();
        assert(std::find(noted.begin(), noted.end(), identity) != noted.end()); // intent exists before the call
    }
    if (adds == drop_add && !drop_add_lost) return -3;
    // As the spec asks: a port forwarded to another client is refused, never overwritten.
    if (const auto it = rules.find({url, port, proto}); it != rules.end() && it->second.client != client) return 718;
    rules[{url, port, proto}] = {0, client, internal, desc, "1", lease, port, proto, ""};
    if (adds == drop_add) return -3;
    if (lose_permanent_reply && std::string_view(lease) == "0") return -1;
    return conflict_on_add || (conflict_on_permanent && std::string_view(lease) == "0") ? 718 : 0;
}
int UPNP_DeletePortMapping(const char *url, const char *, const char *port, const char *proto, const char *) {
    std::lock_guard lock(stub_mutex);
    ++deletes;
    if (delete_error) return delete_error;
    rules.erase({url, port, proto});
    return 0;
}

int main() {
    char directory[] = "/tmp/prospero-upnp-test-XXXXXX";
    const auto temporary = ::mkdtemp(directory);
    assert(temporary);
    platf::test_appdata = temporary;
    const auto base = platf::test_appdata;
    const Router router{"router.example", "http://router.example/control", "WANIPConnection:1", "198.51.100.20"};
    const Mapping mapping{47989, false, "HTTP"};
    const Key key{router.control_url, "47989", "TCP"};
    const Entry original{0, router.lan_address, "47989", "ProsperoSunshine HTTP", "1", "3600", "47989", "TCP", ""};
    std::vector<PortStatus> ports;
    OwnedMappings owned;
    const auto reset = [&] {
        rules.clear(); owned.clear(); ports = {{mapping}};
        adds = deletes = lookup_error = add_error = delete_error = generic_error = fsyncs = 0;
        broken_port = only_permanent = conflict_on_add = conflict_on_permanent = lose_permanent_reply = fail_sync = false;
        empty_answer = false;
        read_calls = 0; drop_read = -1; slow_url.clear(); slow_delay = std::chrono::milliseconds(0);
        drop_add = -1; drop_add_lost = false;
        platf::test_appdata = base;
        std::filesystem::remove(permanent_file());
    };
    const auto map = [&] { map_ports(router, router.lan_address, ports, {}, owned); };
    // Identical manual rules, prefix matches, wrong ports and disabled rules must never be mutated. A failed or broken
    // lookup still tries the add, which the router refuses for a port forwarded to another device: nothing changes.
    for (int variant = 0; variant < 7; ++variant) {
        reset(); auto entry = original;
        if (variant == 1) entry.description += " manual";
        if (variant == 2) entry.internal_port = "48000";
        if (variant == 3) entry.client = "198.51.100.99";
        if (variant == 4) entry.enabled = "0";
        if (variant == 5) { lookup_error = 501; entry.client = "198.51.100.99"; }
        if (variant == 6) entry.client.clear();
        entry.lease = "5"; rules[key] = entry;
        map(); unmap(owned);
        assert(adds == (variant >= 5 ? 1 : 0) && deletes == 0 && owned.empty() && rules.size() == 1);
        assert(rules[key].client == entry.client && rules[key].lease == "5" && read_permanent().empty());
    }
    // An empty answer for a free port (miniupnpc 2.3.3 returns 0 for it) is no entry: the port is mapped.
    reset(); empty_answer = true; map();
    assert(adds == 1 && owned.size() == 1 && ports[0].outcome == Outcome::open && ports[0].added);
    // Gone from the router (it restarted) and answered empty: the record is retired, not retained for good.
    rules.clear(); assert(unmap(owned).empty() && owned.empty() && deletes == 0 && read_permanent().empty());
    // A router that fails the lookup (401, 501) still gets the add; only its success makes the mapping the host's.
    reset(); lookup_error = 501; map();
    assert(adds == 1 && owned.size() == 1 && ports[0].outcome == Outcome::open && read_permanent().size() == 1);
    reset(); lookup_error = 501; add_error = 501; map();
    assert(adds == 1 && owned.empty() && ports[0].outcome == Outcome::failed && read_permanent().empty());
    // A real creation authorizes renewal, but only while the full current identity still matches.
    reset(); map(); assert(owned.size() == 1 && adds == 1);
    rules[key].lease = "100"; map(); assert(deletes == 1 && adds == 2);
    rules[key].description = "manual replacement"; unmap(owned);
    assert(deletes == 1 && rules.size() == 1 && owned.size() == 1);
    rules[key] = original; rules[key].internal_port = "48000"; unmap(owned);
    assert(deletes == 1 && rules.size() == 1);
    rules[key] = original; delete_error = 501; unmap(owned); assert(owned.size() == 1);
    delete_error = 0; unmap(owned); assert(owned.empty() && rules.empty());
    // An unknown lookup never renews by removing or removes an owned rule; the add is sent again. A failed delete before
    // a renewal keeps the rule as it is.
    reset(); map(); lookup_error = 501; map(); assert(adds == 2 && deletes == 0 && owned.size() == 1);
    unmap(owned); assert(deletes == 0 && owned.size() == 1);
    lookup_error = 0; rules[key].lease = "10"; delete_error = 501; map(); assert(adds == 2 && rules.size() == 1);
    // An add conflict is not proof of creation, even when the other rule has our exact description.
    reset(); conflict_on_add = true; map(); assert(owned.empty()); unmap(owned); assert(deletes == 0 && rules.size() == 1);
    // Permanent creation is refused if the journal cannot be written. Never send the permanent add in that case.
    reset(); only_permanent = true; platf::test_appdata = base / "missing"; map();
    assert(adds == 1 && rules.empty() && owned.empty());
    reset(); only_permanent = true; fail_sync = true; map();
    assert(adds == 1 && rules.empty() && owned.empty()); fail_sync = false;
    // Crash recovery knows the exact description and destination; replacements keep the recovery intent.
    reset(); only_permanent = true; map(); assert(adds == 2 && owned.size() == 1 && owned[0].permanent);
    auto journal = read_permanent(); assert(journal.size() == 1 && !journal[0].description.empty());
    owned = {{journal[0], true, true}}; rules[key].description += " replacement"; unmap(owned);
    assert(deletes == 0 && read_permanent() == journal);
    rules[key].description = original.description; unmap(owned); assert(rules.empty() && read_permanent().empty());
    reset(); only_permanent = true; conflict_on_permanent = true; map();
    assert(owned.empty() && read_permanent().empty()); unmap(owned); assert(deletes == 0);
    // A timed-out permanent add retains intent without claiming runtime ownership.
    reset(); only_permanent = true; lose_permanent_reply = true; map(); journal = read_permanent();
    assert(owned.empty() && journal.size() == 1 && rules.size() == 1);
    owned.push_back({journal[0], true, true}); unmap(owned);
    assert(owned.empty() && rules.empty() && read_permanent().empty());
    // A 0.6.x record has no description. It is adopted only when the router still forwards that exact port to that exact
    // client under one of the host's own descriptions; anything else keeps the record as it is.
    reset(); auto legacy = PermanentMapping{router.control_url, router.service_type, router.lan_address, false, 47989};
    for (const char *description : {"ProsperoSunshine HTTP manual", "ProsperoSunshine ", "Sunshine - HTTP"}) {
        reset(); assert(change_permanent({legacy}, {})); owned.push_back({legacy, true, true});
        rules[key] = original; rules[key].description = description; rules[key].lease = "0"; unmap(owned);
        assert(deletes == 0 && owned.size() == 1 && read_permanent() == std::vector<PermanentMapping>{legacy});
    }
    reset(); assert(change_permanent({legacy}, {})); owned.push_back({legacy, true, true});
    rules[key] = original; rules[key].internal_port = "48000"; unmap(owned);
    assert(deletes == 0 && read_permanent() == std::vector<PermanentMapping>{legacy});
    // At a start the record is upgraded and, as no longer mapped then, removed.
    reset(); assert(change_permanent({legacy}, {})); rules[key] = original; rules[key].lease = "0";
    owned = recover({mapping}); assert(owned.empty() && rules.empty() && deletes == 1 && read_permanent().empty());
    // Not reachable at the start (rest mode): the first pass upgrades it into the mapping it renews, with one line.
    reset(); assert(change_permanent({legacy}, {})); rules[key] = original; rules[key].lease = "0";
    lookup_error = 501; owned = recover({mapping}); assert(owned.size() == 1 && deletes == 0);
    lookup_error = 0; map();
    assert(adds == 0 && deletes == 0 && owned.size() == 1 && owned[0].permanent && ports[0].added && ports[0].permanent);
    journal = read_permanent();
    assert(journal.size() == 1 && journal[0].description == original.description && owned[0].identity == journal[0]);
    unmap(owned); assert(rules.empty() && read_permanent().empty());

    // A mapping with a lease that a run couldn't remove at stop (rest mode takes the network first) is noted, and the
    // next run renews it and removes it at its own stop. Without the line, an identical rule stays someone else's.
    reset(); map(); journal = read_permanent();
    assert(journal.size() == 1 && journal[0].description == original.description && owned[0].journaled);
    owned.clear(); lookup_error = 501; owned = recover({mapping});
    assert(owned.size() == 1 && owned[0].identity == journal[0] && deletes == 0);
    lookup_error = 0; rules[key].lease = "100"; ports = {{mapping}}; map();
    assert(deletes == 1 && adds == 2 && ports[0].added && ports[0].outcome == Outcome::open);
    unmap(owned, {}, nullptr, true); assert(rules.empty() && owned.empty() && read_permanent().empty());
    // The same at a start with the router there: kept and renewed, not removed and added again.
    reset(); map(); owned = recover({mapping}); assert(owned.size() == 1 && deletes == 0);
    map(); assert(deletes == 0 && adds == 2 && owned.size() == 1 && ports[0].added);
    // A record of a port this run doesn't map (an earlier Base port) is removed at the start; so is every record with
    // UPnP off.
    reset(); map(); owned = recover({{48989, false, "HTTP"}});
    assert(owned.empty() && rules.empty() && deletes == 1 && read_permanent().empty());
    reset(); map(); remove_leftovers({}); assert(rules.empty() && deletes == 1 && read_permanent().empty());

    // Renewing an owned mapping writes nothing: neither a permanent one nor one with a lease, also when renewed by
    // removing it first.
    reset(); only_permanent = true; map(); assert(owned[0].permanent); fsyncs = 0; map(); map();
    assert(fsyncs == 0 && adds == 2 && read_permanent().size() == 1);
    reset(); map(); fsyncs = 0; map(); rules[key].lease = "100"; map(); assert(fsyncs == 0 && deletes == 1 && adds == 3);

    // A retained record is logged when its reason changes, not every pass; the removal at stop logs it every time.
    reset(); map(); rules[key].description = "manual replacement";
    assert(unmap(owned).size() == 1 && unmap(owned).empty() && unmap(owned).empty());
    delete_error = 501; rules[key].description = original.description;
    assert(unmap(owned).size() == 1 && unmap(owned).empty());
    assert(unmap(owned, {}, nullptr, true).size() == 1 && unmap(owned, {}, nullptr, true).size() == 1);
    delete_error = 0; assert(unmap(owned).empty() && owned.empty());

    // A router that drops a request now and then (an ISP router here: about one SOAP call in 250). On a router that
    // reports internal port 0 each port is checked against the whole table; wherever the dropped read falls, it is sent
    // again: the port stays open, also when the router answers 718 to the renewal as that one does, and is removed at stop.
    for (int drop = 1; drop <= 12; ++drop) {
        reset(); map(); broken_port = true; conflict_on_add = true;
        read_calls = 0; drop_read = drop; map();
        assert(ports[0].outcome == Outcome::open && ports[0].added && owned.size() == 1 && deletes == 0);
        read_calls = 0; drop_read = drop; assert(unmap(owned).empty() && owned.empty() && rules.empty());
    }

    // A leased add that fails on the way is sent once more, whether the request or only its reply was lost. A renewal ends
    // open and the host's, also on a router that answers 718 to a repeated add. A new mapping is the host's after a 0; a
    // 718 to the repeat doesn't make it the host's. The permanent add is never repeated, and its record stays.
    for (const bool lost : {false, true}) {
        for (const bool conflict : {false, true}) {
            reset(); map(); conflict_on_add = conflict; drop_add = adds + 1; drop_add_lost = lost; map();
            assert(adds == 3 && ports[0].outcome == Outcome::open && ports[0].added && owned.size() == 1);
            reset(); conflict_on_add = conflict; drop_add = 1; drop_add_lost = lost; map();
            assert(adds == 2 && ports[0].outcome == Outcome::open && owned.size() == (conflict ? 0u : 1u));
        }
        reset(); only_permanent = true; drop_add = 2; drop_add_lost = lost; map();
        assert(adds == 2 && owned.empty() && ports[0].outcome == Outcome::failed && read_permanent().size() == 1);
    }

    // At stop each router is cleaned on its own thread: a slow one doesn't use up the fast one's budget.
    reset();
    const Router slow{"slow.example", "http://slow.example/control", router.service_type, "203.0.113.30"};
    map_ports(slow, slow.lan_address, ports, {}, owned); ports = {{mapping}}; map();
    assert(owned.size() == 2 && owned[0].identity.control_url == slow.control_url);
    slow_url = slow.control_url; slow_delay = std::chrono::milliseconds(4000);
    const auto started = std::chrono::steady_clock::now();
    remove_at_stop(owned);
    const auto waited = std::chrono::steady_clock::now() - started;
    {
        std::lock_guard lock(stub_mutex);
        assert(rules.count(key) == 0 && rules.count({slow.control_url, "47989", "TCP"}) == 1);
    }
    assert(waited >= removal_budget && waited < removal_budget + 1s);
    {
        std::unique_lock lock(removals().mutex);
        removals().done.wait(lock, [] { return removals().running == 0; });
    }
    assert(rules.empty() && read_permanent().empty());
    // Status resets and gateway changes cannot discard ownership of previous destinations.
    reset(); map(); ports = {{mapping}};
    const Router second{"second.example", "http://second.example/control", router.service_type, "203.0.113.20"};
    map_ports(second, second.lan_address, ports, {}, owned); assert(owned.size() == 2);
    ports.clear(); unmap(owned); assert(rules.empty() && deletes == 2);
    reset(); map();
    map_ports(router, "198.51.100.21", ports, {}, owned);
    assert(owned.size() == 1 && rules[key].client == "198.51.100.21" && deletes == 1);
    // Real-router regression: specific query port=0, generic correct port but corrupted description.
    reset(); map(); broken_port = true; rules[key].lease = "100"; map();
    assert(adds == 2 && deletes == 1 && owned.size() == 1);
    generic_error = 501; unmap(owned); assert(deletes == 1 && owned.size() == 1);
    generic_error = 0; rules[key].internal_port = "48000"; unmap(owned); assert(deletes == 1);
    rules[key].internal_port = "47989"; unmap(owned); assert(deletes == 2 && owned.empty());
    reset(); broken_port = true; rules[key] = original; map(); unmap(owned); assert(adds == 0 && deletes == 0);

    // Adversarial cross-query cases: duplicates, restricted remote hosts, incomplete enumeration, changed identity,
    // shifting indexes, invalid leases and conflicting finite/permanent lifetimes never authorize management.
    const upnp_identity::Expected expected{original.client, original.internal_port, original.description, "47989", "TCP"};
    for (int variant = 0; variant < 10; ++variant) {
        auto specific = original; specific.internal_port = "0";
        int queries = 0, generics = 0;
        auto result = upnp_identity::lookup(expected, true, [&] {
            ++queries;
            auto value = specific;
            if (variant == 3 && queries == 2) value.description += " replacement";
            return value;
        }, [&](std::size_t index) {
            ++generics;
            auto value = original;
            if (variant == 0) { value.external_port = "1"; return value; } // no end marker, hits bound
            if (variant == 1 && index == 1) return value; // duplicate wildcard key
            if (index > 0) { value.result = variant == 2 ? 501 : 713; return value; }
            if (variant == 4) value.remote_host = "203.0.113.99";
            if (variant == 5) value.lease = "0";
            if (variant == 6) value.lease = "invalid";
            if (variant == 7 && generics > 2) value.client = "203.0.113.99";
            if (variant == 8) value.enabled = "0";
            if (variant == 9) value.protocol = "UDP";
            return value;
        });
        assert(result.result != 0);
    }
    reset(); std::filesystem::remove_all(base);
    std::cout << "upnp_client_test: production ownership, recovery and broken-router regression cases passed\n";
}
