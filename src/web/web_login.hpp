// SPDX-License-Identifier: GPL-3.0-only
// The web page's login as the host keeps it: <appdata>/web_login, written by the app through the control socket
// (WEBLOGIN). Without the file, or with the login turned off (enabled=0 in it), the web page stays off. Browsers that
// asked to be remembered are in <appdata>/web_remembered, as hashes only; setting, turning off or forgetting the login
// removes that file.
#pragma once
#include "web/web_auth.hpp"
#include <string>
#include <string_view>

namespace prospero::web_login {

// Sets user and password and turns the page on; the empty string on success, else why not.
std::string set(std::string_view user, std::string_view password);
// Turns the page on or off and keeps the login; the empty string on success, else why not ("no login" without one).
std::string set_enabled(bool on);
// Removes the login, which turns the web page off.
void forget();
// The user name while the page is on, else empty: the web page serves only while this has a name.
std::string user();
// The stored user name, on or off; empty without a login.
std::string stored_user();
// Whether a login is stored and on.
bool enabled();
// Whether user and password match a login that is on.
bool verify(std::string_view user, std::string_view password);
// Rises with every change, so the web page notices one and drops its sessions.
unsigned generation();
// A random token for a session, hex.
std::string random_token();
// SHA-256 of a token, hex: what the remembered file keeps instead of the token.
std::string token_hash(std::string_view token);
// Seconds of the wall clock, which the remembered tokens expire by.
long long wall_now();
// The remembered tokens of the current login, without the expired ones; none while the page is off.
web::Remembered load_remembered();
// Writes them (none removes the file), unless the login changed since `generation`; the empty string on success.
std::string save_remembered(web::Remembered remembered, unsigned generation);

}  // namespace prospero::web_login
