// SPDX-License-Identifier: GPL-3.0-only
#include "app/psapp/autoload.hpp"

#include <string>
#include <vector>

namespace psapp::autoload
{

namespace
{

std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

std::vector<std::string_view> lines(std::string_view text)
{
    std::vector<std::string_view> out;
    while (!text.empty())
    {
        const std::size_t end = text.find('\n');
        out.push_back(text.substr(0, end));
        if (end == std::string_view::npos)
            break;
        text.remove_prefix(end + 1);
    }
    return out;
}

} // namespace

bool contains(std::string_view list, std::string_view key)
{
    for (std::string_view line : lines(list))
    {
        if (trim(line) == key)
            return true;
    }
    return false;
}

std::string with_entry(std::string_view list, std::string_view key)
{
    std::string out(list);
    if (contains(list, key))
        return out;
    if (!out.empty() && out.back() != '\n')
        out += '\n';
    out += key;
    out += '\n';
    return out;
}

std::string without_entry(std::string_view list, std::string_view key)
{
    std::string out;
    for (std::string_view line : lines(list))
    {
        if (trim(line) == key)
            continue;
        out += line;
        out += '\n';
    }
    return out;
}

bool manager_autoload_enabled(std::string_view config)
{
    bool enabled = false;
    for (std::string_view line : lines(config))
    {
        line = trim(line);
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos || trim(line.substr(0, eq)) != "AUTOLOAD_ENABLED")
            continue;
        enabled = trim(line.substr(eq + 1)) == "1";
    }
    return enabled;
}

std::string slot_meta(std::string_view filename, std::string_view installed_at)
{
    std::string out = "{\n  \"schema\": 2,\n  \"kind\": \"local\",\n  \"filename\": \"";
    out += filename;
    out += "\",\n  \"installed_at\": \"";
    out += installed_at;
    out += "\",\n  \"origin\": \"web_upload\",\n  \"origin_detail\": \"ProsperoSunshine app\"\n}\n";
    return out;
}

BootStart boot_start(const std::string *list, bool missing, std::string_view host_answer)
{
    if (list != nullptr)
        return contains(*list, kSlotKey) ? BootStart::on : BootStart::off;
    // No list: nothing starts at boot, and turning it on makes one.
    if (missing)
        return BootStart::off;
    if (host_answer.substr(0, 3) != "OK ")
        return BootStart::unknown;
    // " list=" with its value up to the next space.
    const std::size_t at = host_answer.find(" list=");
    if (at == std::string_view::npos)
        return BootStart::unknown;
    const std::string_view value = host_answer.substr(at + 6, host_answer.find(' ', at + 6) - (at + 6));
    return value == "1" ? BootStart::on : value == "0" ? BootStart::off : BootStart::unknown;
}

// The chip sits in a row with the port, LAN encryption and the paired devices, left of the sun: app/tests/psapp_test.cpp checks
// that the longest row still ends before the sun's rays.
const char *boot_chip(BootStart state)
{
    switch (state)
    {
    case BootStart::on:
        return "Starts at boot";
    case BootStart::off:
        return "Manual start";
    case BootStart::unknown:
        break;
    }
    return "Boot start unknown";
}

const char *boot_unknown_hint(bool host_running)
{
    return host_running ? "The running host did not say whether it starts at boot. Reload (Triangle) asks again."
                        : "Start the host to see whether it starts at boot: only the host may read the Payload Manager's list.";
}

std::string slot_copy_note(BootStart state, long long host_size, long long slot_size, std::optional<bool> same_bytes)
{
    if (state == BootStart::off || host_size < 0 || slot_size < 0)
        return {};
    std::string out = state == BootStart::on ? "Start at boot runs another host build: " : "If Start at boot is on, it runs another host build: ";
    if (slot_size != host_size)
        out += "the slot's copy has " + std::to_string(slot_size) + " bytes, the data folder's " + std::to_string(host_size) + ".";
    else if (same_bytes == false)
        out += "the slot's copy has other bytes than the data folder's (both " + std::to_string(host_size) + ").";
    else
        return {};
    // prepare_files and read_files in host.cpp.
    return out + " The app updates the slot when it starts, or through the running host where it may not read autoload.txt.";
}

} // namespace psapp::autoload
