// SPDX-License-Identifier: GPL-3.0-only
// Which apps a paired device sees and may start (issue 36). Some apps can change the console: a store installs, updates or
// uninstalls titles. The operator can hide such apps from devices they don't trust: hidden_apps.txt in appdata, one title
// ID a line (the file goes through uuid_lines.hpp, as web_trusted.txt does). The host leaves a hidden app out of the list
// it gives such a device and refuses to start or resume it (patch 0023). Portable, tested on the PC (tests/app_access_test.cpp).
#pragma once
#include <string_view>

namespace prospero {

// A title ID as the console names a title's folder: PPSA01234, CUSA01234, or a homebrew one such as FAKE77031. Letters and
// digits only, so that it is one word on the control socket and one line in the file.
constexpr bool title_id_valid(std::string_view id) {
    if (id.size() < 4 || id.size() > 16) return false;
    for (const char c : id)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

// A device the operator trusts with every app: one they marked "I trust this device" that still plays as any profile it
// likes (PR 33). Its profiles are free, or not set yet, which plays the same. Trust only goes with a free choice of
// profile, so a device later limited to some profiles, or set to watch only, counts as not trusted, even while its trust
// mark is still in the file.
constexpr bool trusted_with_apps(bool marked_trusted, bool profiles_set, bool profiles_free) {
    return marked_trusted && (!profiles_set || profiles_free);
}

// Whether a device sees an app and may start or resume it. "PS5 Screen" is never hidden: it is how every device streams.
constexpr bool app_allowed(bool is_screen, bool hidden, bool trusted) { return is_screen || !hidden || trusted; }

}  // namespace prospero
