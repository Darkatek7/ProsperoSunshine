// SPDX-License-Identifier: GPL-3.0-only
#include "app/psapp/web.hpp"

#include <cstdlib>

namespace psapp::web
{

namespace
{

bool user_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
           c == '-';
}

bool printable(char c)
{
    return c >= 0x20 && c < 0x7f;
}

} // namespace

bool valid_user(std::string_view user)
{
    if (user.empty() || user.size() > kUserMax)
        return false;
    for (char c : user)
    {
        if (!user_char(c))
            return false;
    }
    return true;
}

bool valid_password(std::string_view password)
{
    if (password.size() < kPasswordMin || password.size() > kPasswordMax)
        return false;
    for (char c : password)
    {
        if (!printable(c))
            return false;
    }
    return true;
}

std::string user_problem(std::string_view user)
{
    if (user.empty())
        return "Type a user name";
    if (user.size() > kUserMax)
        return "The user name takes at most 32 characters";
    if (!valid_user(user))
        return "Only letters, digits, '.', '_' and '-' in the user name";
    return {};
}

std::string password_problem(std::string_view password)
{
    if (password.size() < kPasswordMin)
        return "The password needs at least 8 characters";
    if (password.size() > kPasswordMax)
        return "The password takes at most 128 characters";
    if (!valid_password(password))
        return "Only the keyboard's letters, digits, symbols and spaces in the password";
    return {};
}

std::uint16_t port(const std::optional<std::string> &base_port)
{
    long base = kDefaultBasePort;
    if (base_port && !base_port->empty())
    {
        char *end = nullptr;
        const long value = std::strtol(base_port->c_str(), &end, 10);
        if (end != nullptr && *end == '\0' && value > 0 && value < 65535)
            base = value;
    }
    return static_cast<std::uint16_t>(base + 1);
}

std::string address(const std::string &ip, std::uint16_t port)
{
    if (ip.empty())
        return {};
    return "https://" + ip + ":" + std::to_string(port);
}

std::string set_request(std::string_view user, std::string_view password)
{
    std::string line = "WEBLOGIN SET ";
    line.append(user);
    line += ' ';
    line.append(password);
    line += '\n';
    return line;
}

std::optional<std::string> parse_user(const std::string &line)
{
    static const std::string prefix = "OK user=";
    if (line.rfind(prefix, 0) != 0)
        return std::nullopt;
    return line.substr(prefix.size());
}

Outcome parse_outcome(const std::string &line)
{
    Outcome outcome;
    if (line == "OK" || line.rfind("OK ", 0) == 0)
    {
        outcome.ok = true;
        return outcome;
    }
    if (line == "ERR unknown command")
    {
        outcome.unknown = true;
        outcome.reason = "the running host has no web page";
        return outcome;
    }
    if (line.rfind("ERR ", 0) == 0)
        outcome.reason = line.substr(4);
    else
        outcome.reason = line.empty() ? std::string("no answer") : line;
    return outcome;
}

} // namespace psapp::web
