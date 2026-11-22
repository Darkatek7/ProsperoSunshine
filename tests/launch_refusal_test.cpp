// SPDX-License-Identifier: GPL-3.0-only
// The message Moonlight shows when another title is open on the console (src/apps/launch_refusal.hpp).
#include "apps/launch_refusal.hpp"
#include <cassert>
#include <cstdio>

int main() {
    using prospero::OpenTitle;
    using prospero::launch_refusal;
    assert(launch_refusal("RetroArch", "ProsperoStore", OpenTitle::in_front) ==
           "RetroArch is open on the console, and starting ProsperoStore would close it. Close it first, or pick PS5 Screen to "
           "go on with it.");
    assert(launch_refusal("Astro Bot", "Playden", OpenTitle::behind) ==
           "Astro Bot is still open on the console, and starting Playden would close it. Close it first, or pick PS5 Screen to "
           "go on with it.");
    // A title hidden from the device, or one without a name in the list, isn't named.
    assert(launch_refusal("", "Playden", OpenTitle::in_front).starts_with("Another title is open on the console, and starting Playden"));
    assert(launch_refusal("RetroArch", "", OpenTitle::in_front).find("starting this one would close it") != std::string::npos);
    std::puts("PASS: launch refusal message");
    return 0;
}
