// SPDX-License-Identifier: GPL-3.0-only
// The host's web page as the app sets it up: its address, and the login the host keeps (WEBLOGIN on the
// control socket, src/control/control_server.hpp). The page is off until a login is set. The checks of
// user and password are the host's own (src/web/web_auth.hpp), so the app can say what is wrong before
// it sends anything. Pure code, tested on the PC (app/tests).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace psapp::web
{

constexpr std::uint16_t kDefaultBasePort = 47989; // sunshine.conf's port without the key
constexpr std::size_t kUserMax = 32;
constexpr std::size_t kPasswordMin = 8;
constexpr std::size_t kPasswordMax = 128;

// 1 to 32 letters, digits, '.', '_' or '-': one word, as it travels in the control line.
bool valid_user(std::string_view user);
// 8 to 128 printable ASCII characters; spaces count.
bool valid_password(std::string_view password);
// Why not, to show under the field; empty when it is fine. Never holds the password itself.
std::string user_problem(std::string_view user);
std::string password_problem(std::string_view password);

// The page's port: sunshine.conf's base port (the `port` key, nullopt: not set) plus one.
std::uint16_t port(const std::optional<std::string> &base_port);
// "https://<ip>:<port>"; empty without an address.
std::string address(const std::string &ip, std::uint16_t port);

// The control lines. The request with the password must never be logged.
inline constexpr const char *kQuery = "WEBLOGIN\n";
inline constexpr const char *kOn = "WEBLOGIN ON\n";         // with the stored login
inline constexpr const char *kOff = "WEBLOGIN OFF\n";       // keeps the login (an older host removes it)
inline constexpr const char *kForget = "WEBLOGIN FORGET\n"; // removes the login
std::string set_request(std::string_view user, std::string_view password);

// The answer to kQuery.
struct LoginState
{
    std::string user;        // the stored login's user; empty: none
    bool enabled = false;    // the page is on
    bool switchable = false; // the host keeps a login while the page is off (ON, OFF and FORGET)
};
// "OK user=<name> enabled=<0|1>"; a host from before the switch answers "OK user=<name>", where a name
// means on and OFF removes the login. Nothing for any other line, which is what a host without the web
// page answers ("ERR unknown command").
std::optional<LoginState> parse_state(const std::string &line);

// The answer to a SET or OFF: ok, or the host's reason ("ERR <reason>" without the "ERR ").
struct Outcome
{
    bool ok = false;
    bool unknown = false; // the host does not know WEBLOGIN: it is older than the web page
    std::string reason;
};
Outcome parse_outcome(const std::string &line);

} // namespace psapp::web
