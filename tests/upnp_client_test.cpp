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
static int adds, deletes, lookup_error, add_error, delete_error, generic_error;
static bool broken_port, only_permanent, conflict_on_add, conflict_on_permanent, lose_permanent_reply, fail_sync;
extern "C" int fsync(int fd) {
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
    if (lookup_error) return lookup_error;
    auto it = rules.find({url, port, proto});
    if (it == rules.end()) return 714;
    const auto &r = it->second;
    copy(client, r.client); copy(internal, broken_port ? "0" : r.internal_port); copy(desc, r.description);
    copy(enabled, r.enabled); copy(lease, r.lease);
    return 0;
}
int UPNP_GetGenericPortMappingEntry(const char *url, const char *, const char *index, char *external, char *client,
                                  char *internal, char *proto, char *desc, char *enabled, char *remote, char *lease) {
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
    ++adds;
    if (add_error) return add_error;
    if (only_permanent && std::string_view(lease) != "0") return 725;
    if (std::string_view(lease) == "0") {
        const PermanentMapping identity{url, service, client, std::string_view(proto) == "UDP",
                                         static_cast<uint16_t>(std::stoi(port)), desc};
        auto noted = read_permanent();
        assert(std::find(noted.begin(), noted.end(), identity) != noted.end()); // intent exists before the call
    }
    rules[{url, port, proto}] = {0, client, internal, desc, "1", lease, port, proto, ""};
    if (lose_permanent_reply && std::string_view(lease) == "0") return -1;
    return conflict_on_add || (conflict_on_permanent && std::string_view(lease) == "0") ? 718 : 0;
}
int UPNP_DeletePortMapping(const char *url, const char *, const char *port, const char *proto, const char *) {
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
        adds = deletes = lookup_error = add_error = delete_error = generic_error = 0;
        broken_port = only_permanent = conflict_on_add = conflict_on_permanent = lose_permanent_reply = fail_sync = false;
        platf::test_appdata = base;
        std::filesystem::remove(permanent_file());
    };
    const auto map = [&] { map_ports(router, router.lan_address, ports, {}, owned); };
    // Identical manual rules, prefix matches, wrong ports, disabled rules and lookup failures must never be mutated.
    for (int variant = 0; variant < 8; ++variant) {
        reset(); auto entry = original;
        if (variant == 1) entry.description += " manual";
        if (variant == 2) entry.internal_port = "48000";
        if (variant == 3) entry.client = "198.51.100.99";
        if (variant == 4) entry.enabled = "0";
        if (variant == 5) lookup_error = 501;
        if (variant == 6) entry.client.clear();
        if (variant == 7) entry.internal_port.clear();
        entry.lease = "5"; rules[key] = entry;
        map(); unmap(owned);
        assert(adds == 0 && deletes == 0 && owned.empty() && rules.size() == 1);
    }
    // A real creation authorizes renewal, but only while the full current identity still matches.
    reset(); map(); assert(owned.size() == 1 && adds == 1);
    rules[key].lease = "100"; map(); assert(deletes == 1 && adds == 2);
    rules[key].description = "manual replacement"; unmap(owned);
    assert(deletes == 1 && rules.size() == 1 && owned.size() == 1);
    rules[key] = original; rules[key].internal_port = "48000"; unmap(owned);
    assert(deletes == 1 && rules.size() == 1);
    rules[key] = original; delete_error = 501; unmap(owned); assert(owned.size() == 1);
    delete_error = 0; unmap(owned); assert(owned.empty() && rules.empty());
    // Unknown lookup / failed delete must not fall through to AddPortMapping, even for an owned expiring rule.
    reset(); map(); lookup_error = 501; map(); assert(adds == 1 && owned.size() == 1);
    lookup_error = 0; rules[key].lease = "10"; delete_error = 501; map(); assert(adds == 1 && rules.size() == 1);
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
    owned = {{journal[0], true}}; rules[key].description += " replacement"; unmap(owned);
    assert(deletes == 0 && read_permanent() == journal);
    rules[key].description = original.description; unmap(owned); assert(rules.empty() && read_permanent().empty());
    reset(); only_permanent = true; conflict_on_permanent = true; map();
    assert(owned.empty() && read_permanent().empty()); unmap(owned); assert(deletes == 0);
    // A timed-out permanent add retains intent without claiming runtime ownership.
    reset(); only_permanent = true; lose_permanent_reply = true; map(); journal = read_permanent();
    assert(owned.empty() && journal.size() == 1 && rules.size() == 1);
    owned.push_back({journal[0], true}); unmap(owned);
    assert(owned.empty() && rules.empty() && read_permanent().empty());
    // Legacy records have no complete description: retain them rather than guessing from a prefix.
    reset(); auto legacy = PermanentMapping{router.control_url, router.service_type, router.lan_address, false, 47989};
    assert(change_permanent({legacy}, {})); owned.push_back({legacy, true}); rules[key] = original; unmap(owned);
    assert(deletes == 0 && read_permanent().size() == 1);
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
