// SPDX-License-Identifier: GPL-3.0-only
// Cross-check a broken GetSpecificPortMappingEntry response without relaxing ownership.
#pragma once
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace upnp_identity {
struct Entry {
    int result = -1;
    std::string client = {}, internal_port = {}, description = {}, enabled = {}, lease = {};
    std::string external_port = {}, protocol = {}, remote_host = {};
};
struct Expected {
    std::string_view client, internal_port, description, external_port, protocol;
};
inline std::optional<std::uint32_t> seconds(std::string_view text) {
    if (text.empty()) return {};
    std::uint32_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return {};
    return value;
}
inline bool wildcard(std::string_view remote) { return remote.empty() || remote == "0.0.0.0"; }
inline bool key_matches(const Entry &entry, const Expected &wanted) {
    return entry.external_port == wanted.external_port && entry.protocol == wanted.protocol && wildcard(entry.remote_host);
}
inline bool broken_specific_matches(const Entry &entry, const Expected &wanted) {
    return entry.result == 0 && entry.internal_port == "0" && entry.client == wanted.client &&
           entry.description == wanted.description && entry.enabled == "1" && seconds(entry.lease).has_value();
}
inline bool generic_matches(const Entry &entry, const Expected &wanted) {
    return entry.result == 0 && key_matches(entry, wanted) && entry.client == wanted.client &&
           entry.internal_port == wanted.internal_port && entry.enabled == "1" && seconds(entry.lease).has_value();
}
// Only an already recorded rule can use this fallback. Generic descriptions on affected routers contain stale bytes,
// so the exact description must come from TWO successful specific queries; generic queries supply the missing port.
// Complete bounded enumeration rejects duplicate wildcard keys and errors. Re-read the selected index to catch table
// changes during enumeration. All uncertainty returns an error, never absence or permission to mutate.
template<class Specific, class Generic>
Entry lookup(const Expected &wanted, bool recorded, Specific specific, Generic generic) {
    auto first = specific();
    if (!recorded || !broken_specific_matches(first, wanted)) return first;
    constexpr std::size_t limit = 1024;
    std::optional<std::size_t> selected;
    Entry candidate;
    bool complete = false;
    for (std::size_t index = 0; index < limit; ++index) {
        auto entry = generic(index);
        if (entry.result == 713) { complete = true; break; }
        if (entry.result != 0) return {};
        const auto port = seconds(entry.external_port);
        if (!port || *port == 0 || *port > 65535 || (entry.protocol != "TCP" && entry.protocol != "UDP")) return {};
        if (!key_matches(entry, wanted)) continue;
        if (selected || !generic_matches(entry, wanted)) return {};
        selected = index;
        candidate = std::move(entry);
    }
    if (!complete || !selected) return {};
    auto last = specific();
    if (!broken_specific_matches(last, wanted)) return {};
    auto current = generic(*selected);
    if (!generic_matches(current, wanted)) return {};
    // Never confuse a permanent rule with a finite one. Use the shortest reported finite lease for renewal.
    auto duration = *seconds(first.lease);
    for (const auto *entry : {&candidate, &last, &current}) {
        const auto value = *seconds(entry->lease);
        if ((value == 0) != (duration == 0)) return {};
        if (value < duration) duration = value;
    }
    last.internal_port = std::string(wanted.internal_port);
    last.lease = std::to_string(duration);
    return last;
}
} // namespace upnp_identity
