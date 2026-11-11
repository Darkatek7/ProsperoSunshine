// SPDX-License-Identifier: GPL-3.0-only
// What the PS5 platform (platform_ps5.cpp) offers the host's own code, beside Sunshine's platf interface.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platf {

// A paired device's profiles changed, by its certificate: each of its controllers sitting in a profile the device no
// longer has leaves it and only watches until a press joins it as one it still has. The stream goes on. `added` are the
// profiles it got beside those it had (none when it could choose freely before), passed only while the device streams:
// it is told at once.
void profiles_changed(const std::string &cert, const std::vector<uint32_t> &added);

// Whether the paired device with this certificate gets "PS5 Screen" (control/menu_access.hpp): yes while the console's
// menus aren't chosen, for a device that can choose freely or isn't set, and for one with a profile that reaches the menus.
bool screen_offered_to(const std::string &cert);

// Before a game starts for the paired device with this certificate: while nobody is in front on the console, one of the
// device's own profiles is signed in if it isn't. The profile the game should start for, else 0.
// title: the title about to start, so the profile isn't signed out while it is open.
uint32_t sign_in_for_launch(const std::string &cert, const std::string &title);

}  // namespace platf
