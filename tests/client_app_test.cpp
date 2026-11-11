// SPDX-License-Identifier: GPL-3.0-only
#include "control/client_app.hpp"
#include <iostream>
using namespace prospero;

int main() {
    static_assert(touch_apple_client("Moonlight/1234 CFNetwork/3826.500.131 Darwin/24.5.0"));
    static_assert(touch_apple_client("SomeFork/7 CFNetwork/1498.700.2 Darwin/23.6.0"));
    static_assert(!touch_apple_client("Moonlight%20TV/1234 CFNetwork/3826.500.131 Darwin/24.5.0"));  // Apple TV
    static_assert(!touch_apple_client("Mozilla/5.0"));                                               // Qt's default
    static_assert(!touch_apple_client("okhttp/4.12.0"));
    static_assert(!touch_apple_client(""));
    std::cout << "PASS: client app (Moonlight for iPhone and iPad by its User-Agent)\n";
}
