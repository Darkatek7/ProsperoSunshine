// SPDX-License-Identifier: GPL-3.0-only
#include "net/exposure.hpp"
#include <cassert>
#include <iostream>
using namespace prospero::exposure;

int main() {
    // Pairing: from home always, from the internet only when the owner allows it.
    static_assert(pairing_allowed(true, false) && pairing_allowed(true, true));
    static_assert(!pairing_allowed(false, false));
    static_assert(pairing_allowed(false, true));
    assert(!pairing_refused_message.empty() && pairing_refused_message.find('\n') == std::string_view::npos);

    // The same uniqueid from two addresses gives two keys; the same address and uniqueid the same key.
    assert(pairing_key("203.0.113.7", "0123456789ABCDEF") != pairing_key("198.51.100.4", "0123456789ABCDEF"));
    assert(pairing_key("203.0.113.7", "0123456789ABCDEF") == pairing_key("203.0.113.7", "0123456789ABCDEF"));
    assert(pairing_key("203.0.113.7", "0123456789ABCDEF") == "203.0.113.7 0123456789ABCDEF");
    assert(pairing_key("203.0.113.7", "A") != pairing_key("203.0.113.7", "B"));
    // The key starts with the whole address and a space, which no address holds: a uniqueid can't make another address's key.
    assert(pairing_key("203.0.113.7", "0 X").rfind("203.0.113.7 ", 0) == 0);
    assert(pairing_key("2001:db8::1", "") == "2001:db8::1 ");

    // Two pending pairings per address, the third is refused.
    static_assert(!pairing_address_full(0) && !pairing_address_full(1));
    static_assert(pairing_address_full(2) && pairing_address_full(3));

    // Names: kept up to the limit, cut before a character that doesn't fit whole.
    assert(cap_name("Living room PC", 128) == "Living room PC");
    assert(cap_name(std::string(1000000, 'A'), 128) == std::string(128, 'A'));
    assert(cap_name("", 128).empty());
    assert(cap_name("abcd", 4) == "abcd" && cap_name("abcde", 4) == "abcd");
    // "ab" then U+00E9 (2 bytes) then U+20AC (3 bytes) then U+1F3AE (4 bytes).
    const std::string mixed = "ab\xC3\xA9\xE2\x82\xAC\xF0\x9F\x8E\xAE";
    assert(cap_name(mixed, 3) == "ab");                        // the 2-byte character would be cut
    assert(cap_name(mixed, 4) == "ab\xC3\xA9");
    assert(cap_name(mixed, 5) == "ab\xC3\xA9" && cap_name(mixed, 6) == "ab\xC3\xA9");
    assert(cap_name(mixed, 7) == "ab\xC3\xA9\xE2\x82\xAC");
    assert(cap_name(mixed, 8) == "ab\xC3\xA9\xE2\x82\xAC" && cap_name(mixed, 10) == "ab\xC3\xA9\xE2\x82\xAC");
    assert(cap_name(mixed, 11) == mixed);
    // Not UTF-8 (a run of continuation bytes): cut by bytes.
    assert(cap_name(std::string(10, '\x80'), 4) == std::string(4, '\x80'));
    assert(cap_name("\xF0\x9F\x8E\xAE", 2).empty());

    // serverinfo: everything over HTTPS (paired devices) and from home; the game and LAN address hidden from the internet.
    static_assert(serverinfo_view(true, false).running_game && serverinfo_view(true, false).local_address);
    static_assert(serverinfo_view(false, true).running_game && serverinfo_view(false, true).local_address);
    static_assert(serverinfo_view(true, true).running_game);
    static_assert(!serverinfo_view(false, false).running_game && !serverinfo_view(false, false).local_address);

    // The report port: home only, and only while the app asks.
    static_assert(report_allowed(true, true));
    static_assert(!report_allowed(false, true) && !report_allowed(true, false) && !report_allowed(false, false));

    std::cout << "PASS: exposure (pairing from home, pairing key and cap, name cap, serverinfo, report port)\n";
    return 0;
}
