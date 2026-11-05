// SPDX-License-Identifier: GPL-3.0-only
// The web page's login as the host keeps it: <appdata>/web_login, written by the app through the control socket
// (WEBLOGIN). Without the file the web page stays off.
#pragma once
#include <string>
#include <string_view>

namespace prospero::web_login {

// Sets user and password; the empty string on success, else why not.
std::string set(std::string_view user, std::string_view password);
// Removes the login, which turns the web page off.
void clear();
// The user name, empty without a login.
std::string user();
bool verify(std::string_view user, std::string_view password);
// Rises with every change, so the web page notices one and drops its sessions.
unsigned generation();
// A random token for a session, hex.
std::string random_token();

}  // namespace prospero::web_login
