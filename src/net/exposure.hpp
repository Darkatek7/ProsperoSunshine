// SPDX-License-Identifier: GPL-3.0-only
// What the host allows and tells, by where a request comes from. "Home" is the LAN and Tailscale as Sunshine's
// net::from_address counts them (private, carrier-grade NAT and link-local ranges, so also Tailscale's 100.64.0.0/10); every
// other address is the internet, which reaches Moonlight's ports once they are forwarded, by UPnP or by hand. The web page and
// the control socket answer only home already. Portable, tested on the PC (tests/exposure_test.cpp); the callers are
// nvhttp.cpp (patches 0029 to 0031) and control_server.cpp.
#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace prospero::exposure {

// Pairing goes over plain HTTP with a four-digit PIN, so it is for home only, unless the owner allows the internet
// (pair_over_internet in sunshine.conf).
constexpr bool pairing_allowed(bool from_home, bool over_internet_allowed) { return from_home || over_internet_allowed; }

// What Moonlight shows when the host refuses to pair from the internet (it adds the status code).
inline constexpr std::string_view pairing_refused_message =
    "This host pairs only at home: pair this device on the same network as the console or over Tailscale, "
    "or turn on Pair over the internet in the host's settings";

// The key of a pending pairing: the address it came from and the client's uniqueid. Moonlight sends the same uniqueid from
// every device, so keyed by the uniqueid alone, one request from a stranger held the key against the owner's Moonlight (409),
// and a later step from a stranger ended or used up the owner's pending pairing. With the address in the key, requests from
// another address neither find nor block it. An address has no space, so the key is unique per address and uniqueid.
inline std::string pairing_key(std::string_view address, std::string_view unique_id) {
    std::string key(address);
    key += ' ';
    key += unique_id;
    return key;
}

// Pending pairings one address may hold. A device pairs once at a time and Moonlight's uniqueid is the same on every device,
// so a second one from an address is rare; without a cap, one address could fill Sunshine's table of 32 and keep everybody
// else from pairing.
inline constexpr std::size_t max_pairings_per_address = 2;

constexpr bool pairing_address_full(std::size_t pending_from_address) {
    return pending_from_address >= max_pairings_per_address;
}

// Bytes kept of a device name a pairing request brings (Sunshine's MAX_PAIRING_CLIENT_NAME_SIZE): the request is anyone's, and
// the name is held for minutes and shown in the app and on the web page. Cut before a UTF-8 character that would not fit
// whole, so a long name doesn't end in a broken character.
inline std::string cap_name(std::string_view name, std::size_t max_bytes) {
    if (name.size() <= max_bytes) return std::string(name);
    std::size_t cut = max_bytes;
    // A UTF-8 character is at most four bytes: step back over at most three continuation bytes to its first byte.
    for (int back = 0; back < 3 && cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80; ++back) --cut;
    if ((static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) cut = max_bytes;  // no valid UTF-8 there: cut by bytes
    return std::string(name.substr(0, cut));
}

// What serverinfo tells. Over plain HTTP anybody may ask; from the internet that is a stranger who port-scans, so the host
// doesn't tell what runs (currentgame, and BUSY in state; currentgame is a hash a list of title IDs reverses) or its LAN
// address (LocalIP). A paired device asks over HTTPS and gets everything; from home nothing changes, the project's own tools
// read state there.
struct ServerInfoView {
    bool running_game = true;  // currentgame and state as they are; else 0 and SUNSHINE_SERVER_FREE
    bool local_address = true;  // LocalIP as it is; else 127.0.0.1, which Moonlight already gets for IPv6 and ignores
};

constexpr ServerInfoView serverinfo_view(bool over_https, bool from_home) {
    const bool all = over_https || from_home;
    return {all, all};
}

// The diagnostics report on its own port: only while the app keeps asking for it, and only to home. It holds the settings
// and the log's lines, scrubbed for the people who post it, not for a stranger.
constexpr bool report_allowed(bool from_home, bool window_open) { return from_home && window_open; }

}  // namespace prospero::exposure
