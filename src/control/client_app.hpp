// SPDX-License-Identifier: GPL-3.0-only
// Which Moonlight a client is, from the User-Agent of its last launch or resume (patch 0026). Moonlight for iPhone and
// iPad sets none of its own, so Apple's networking sends its default, "<app>/<build> CFNetwork/<version> Darwin/<version>"
// ("Moonlight/..."); its Apple TV build is "Moonlight%20TV/..." and has no on-screen keyboard. Moonlight for PC (Qt) and
// for Android (OkHttp) send their own. Portable, tested in tests/client_app_test.cpp.
#pragma once
#include <string_view>

namespace prospero {

// Moonlight for iPhone or iPad, or a fork of it: no PS button, but Windows and Esc above its on-screen keyboard
// (keyboard_map.hpp, ps_chord).
constexpr bool touch_apple_client(std::string_view agent) {
    return agent.find("CFNetwork/") != std::string_view::npos && !agent.starts_with("Moonlight%20TV/");
}

}  // namespace prospero
