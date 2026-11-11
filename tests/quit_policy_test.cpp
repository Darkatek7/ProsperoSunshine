// SPDX-License-Identifier: GPL-3.0-only
#include "apps/quit_policy.hpp"
#include <cstdio>

using namespace std::chrono_literals;
using prospero::quit_decision;
using prospero::QuitDecision;

static_assert(quit_decision(false, "PPSA03527", "PPSA03527", true) == QuitDecision::off);
static_assert(quit_decision(true, "", "PPSA03527", true) == QuitDecision::no_game);  // "PS5 Screen", 409 or failed start
static_assert(quit_decision(true, "PPSA03527", "PPSA03671", true) == QuitDecision::not_in_front);
static_assert(quit_decision(true, "PPSA03527", "PPSA47989", true) == QuitDecision::not_in_front);  // behind the settings app
static_assert(quit_decision(true, "PPSA03527", "", true) == QuitDecision::not_in_front);  // home screen
static_assert(quit_decision(true, "PPSA03527", "PPSA03527", true) == QuitDecision::close);

// Another device's quit leaves the game, also while it is in front (the owner may play on).
static_assert(quit_decision(true, "PPSA03527", "PPSA03527", false) == QuitDecision::not_theirs);
static_assert(quit_decision(false, "PPSA03527", "PPSA03527", false) == QuitDecision::off);
static_assert(quit_decision(true, "", "PPSA03527", false) == QuitDecision::no_game);
static_assert(prospero::may_close_game(true, false));   // the device that started it
static_assert(prospero::may_close_game(false, true));   // a device trusted with every app
static_assert(!prospero::may_close_game(false, false));  // any other paired device

static_assert(!prospero::released(1s, true, 0ms));        // the title's folder is still there
static_assert(!prospero::released(2s, false, 1s));        // gone, but within the grace
static_assert(prospero::released(3s, false, 1500ms));
static_assert(prospero::released(11s, true, 0ms));        // ShadowMountPlus gives up waiting after 10 s
static_assert(!prospero::released(10900ms, true, 0ms));

int main() {
    std::puts("PASS: quit policy (setting off, no game, another device's game, title not in front, release before a restart)");
    return 0;
}
