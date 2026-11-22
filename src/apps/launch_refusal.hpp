// SPDX-License-Identifier: GPL-3.0-only
// What Moonlight shows when the host doesn't start the game a client picked because another title is open on the console
// (process_ps5.cpp, start_title): starting one would close the other and could lose its progress. Moonlight only said
// "Failed to start the specified application" before. Portable, tested in tests/launch_refusal_test.cpp.
#pragma once
#include <string>
#include <string_view>

namespace prospero {

enum class OpenTitle {
    in_front,  // the title in front of the console
    behind,    // still open behind another one, as a game behind the settings app
};

// open: the name of the title that is open, empty when the client may not see it (hidden from the device) or the host has
// none for it (a system app). wanted: the name of the one the client picked.
inline std::string launch_refusal(std::string_view open, std::string_view wanted, OpenTitle where) {
    std::string text = open.empty() ? std::string("Another title") : std::string(open);
    text += where == OpenTitle::in_front ? " is open on the console" : " is still open on the console";
    text += ", and starting ";
    text += wanted.empty() ? std::string_view("this one") : wanted;
    text += " would close it. Close it first, or pick PS5 Screen to go on with it.";
    return text;
}

}  // namespace prospero
