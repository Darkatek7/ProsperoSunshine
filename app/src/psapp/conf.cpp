// SPDX-License-Identifier: GPL-3.0-only
#include "app/psapp/conf.hpp"

#include "app/psapp/experimental_list.h"
#include "app/psapp/flavor.hpp"

#include <cstdlib>

namespace psapp
{

namespace
{

bool space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && space(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && space(s.back()))
        s.remove_suffix(1);
    return s;
}

const std::vector<Choice> kEncryption = {
    {"0", "Off"},
    {"1", "Optional"},  // encrypted when the client supports it; "When supported" was cut off on the tile
    {"2", "Required"},
};

} // namespace

// Names, ranges and defaults from Sunshine a679b059 src/config.cpp; ping_timeout's default is the
// host's own (src/sunshine/main_ps5.cpp sets 2 s before the file is read). The order is the settings page's: Host,
// Network, Playing, Experimental (settings_layout.cpp lays the groups out; the app's tests check every option has a tile).
const std::vector<OptionSpec> &option_specs()
{
    using G = OptionGroup;
    using I = icons::Icon;
    static const std::vector<OptionSpec> specs = [] {
        std::vector<OptionSpec> list = {
        {G::host, I::tag, "sunshine_name", "Host name", "The name Moonlight shows.", OptionKind::text,
         "network name", {}, 0, 0, 1, 0},
        {G::host, I::log, "min_log_level", "Log level", "Detail in sunshine.log and the kernel log.", OptionKind::choice,
         kDebugBuild ? "Debug" : "Info",
         {{"verbose", "Verbose"},
          {"debug", "Debug"},
          {"info", "Info"},
          {"warning", "Warning"},
          {"error", "Error"},
          {"fatal", "Fatal"},
          {"none", "None"}},
         0, 0, 1, 0},
        {G::network, I::plug, "port", "Base port",
         "Moonlight's ports count from here.",
         OptionKind::number, "47989", {}, 1025, 65514, 1, 47989},
        // Sunshine's own key, read by the host (src/net/upnp_client.cpp). Off: the ports are forwarded by hand, if at all.
        {G::network, I::wifi, "upnp", "UPnP port forwarding",
         "On: the host asks the router to forward Moonlight's ports, so devices outside your network can connect. The web page shows the result.",
         OptionKind::toggle, "Off", {}, 0, 0, 1, 0},
        // Sunshine reads this one (patch 0030), like close_game_on_quit. Off, pairing works from the LAN and Tailscale only.
        {G::network, I::link, "pair_over_internet", "Pair over the internet",
         "Off: devices pair at home, on the same network or over Tailscale. On: also from the internet, where only the four-digit PIN guards it.",
         OptionKind::toggle, "Off", {}, 0, 0, 1, 0},
        {G::network, I::lock, "lan_encryption_mode", "LAN encryption",
         "Encrypts the stream in the local network.", OptionKind::choice, "Off",
         kEncryption, 0, 0, 1, 0},
        {G::network, I::globe, "wan_encryption_mode", "Internet encryption",
         "The same for clients outside it.", OptionKind::choice,
         "Optional", kEncryption, 0, 0, 1, 0},
        {G::network, I::timer, "ping_timeout", "Ping timeout (ms)",
         "Ends a stream whose client went silent.",
         OptionKind::number, "2000 ms", {}, 500, 60000, 500, 2000, "ms"},
        // Sunshine reads this one (patch 0010), as it does the options above.
        {G::playing, I::home, "close_game_on_quit", "Quit closes the game",
         "On: quitting in Moonlight also closes the game the stream started or found in front, and unsaved progress is lost.",
         OptionKind::toggle, "Off", {}, 0, 0, 1, 0},
        // The host reads this one (src/sunshine/platform_ps5.cpp). Off, only the host's controller has PS and Create.
        {G::playing, I::monitor, "ps_button_all_players", "PS for all players",
         "Off: only the host, who plays as the console's user, has PS and Create. On: every player has them, friends online too.",
         OptionKind::toggle, "Off", {}, 0, 0, 1, 0},
        // Sunshine's own key (src/config.cpp, 0 to 100, default 20), which the host leaves alone: stream.cpp adds that many
        // Reed-Solomon packets per 100 video packets, and rtsp.cpp lowers the encoder's bitrate to make room for them, up to
        // 80. Above 80 it stops lowering it and the stream outgrows the bitrate the client asked for, so the app stops there.
        {G::playing, I::check, "fec_percentage", "Error correction (%)",
         "Rebuilds lost video. More survives more packet loss but costs bitrate.",
         OptionKind::number, "20 %", {}, 0, 80, 1, 20, "%"},
        };
        // The Experimental switches, which the host reads itself (src/sunshine/main_ps5.cpp), not Sunshine.
        for (int i = 0; i < PS_EXP_SWITCH_COUNT; ++i)
        {
            const ps_exp_switch_info *info = ps_exp_switch_at(i);
            list.push_back({G::experimental, I::sliders, info->key, info->label, info->help, OptionKind::toggle, "Off", {}, 0, 0, 1, 0});
        }
        // The host reads this one too (src/sunshine/platform_ps5.cpp). Among the Experimental switches, but no firmware check
        // locks it.
        list.push_back({G::experimental, I::sliders, "surround_sound", "Surround",
                        "5.1 or 7.1 sound for devices that ask for it. The speaker order is not verified yet.",
                        OptionKind::toggle, "Off", {}, 0, 0, 1, 0});
        return list;
    }();
    return specs;
}

const OptionSpec *find_option(std::string_view key)
{
    for (const OptionSpec &spec : option_specs())
    {
        if (key == spec.key)
            return &spec;
    }
    return nullptr;
}

int experimental_switch(const OptionSpec &spec)
{
    for (int i = 0; i < PS_EXP_SWITCH_COUNT; ++i)
    {
        if (std::string_view(spec.key) == ps_exp_switch_at(i)->key)
            return i;
    }
    return -1;
}

std::optional<std::string> validate(const OptionSpec &spec, std::string_view value)
{
    switch (spec.kind)
    {
    case OptionKind::text:
        if (value.empty() || trim(value).size() != value.size())
            return std::string("must not be empty or start or end with a space");
        if (value.size() > 63)
            return std::string("must be at most 63 characters");
        for (char c : value)
        {
            // '#' starts a comment in sunshine.conf and '[' a list.
            if (c == '#' || c == '[' || c == '\n' || static_cast<unsigned char>(c) < 0x20)
                return std::string("must not contain # or [");
        }
        return std::nullopt;
    case OptionKind::choice:
        for (const Choice &choice : spec.choices)
        {
            if (value == choice.value)
                return std::nullopt;
        }
        return std::string("is not one of the offered values");
    case OptionKind::number: {
        if (value.empty() || value.size() > 9)
            return std::string("is not a number");
        long number = 0;
        for (char c : value)
        {
            if (c < '0' || c > '9')
                return std::string("is not a number");
            number = number * 10 + (c - '0');
        }
        if (number < spec.min || number > spec.max)
            return "must be between " + std::to_string(spec.min) + " and " + std::to_string(spec.max);
        return std::nullopt;
    }
    case OptionKind::toggle:
        if (value == "0" || value == "1")
            return std::nullopt;
        return std::string("must be 0 or 1");
    }
    return std::string("unknown option");
}

std::optional<std::string> option_value(const Config &config, const OptionSpec &spec)
{
    std::optional<std::string> value = config.get(spec.key);
    const std::string_view key = spec.key;
    if (value && (key == "close_game_on_quit" || key == "upnp" || key == "pair_over_internet"))
    {
        // to_bool in Sunshine's src/config.cpp, which is case-sensitive.
        const std::string &v = *value;
        const bool on = v == "true" || v == "yes" || v == "enable" || v == "enabled" || v == "on" || v.find('1') != std::string::npos;
        value = on ? "1" : "0";
    }
    return value;
}

Config Config::parse(std::string_view text)
{
    Config config;
    while (!text.empty())
    {
        const std::size_t end = text.find('\n');
        std::string_view raw = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (!raw.empty() && raw.back() == '\r')
            raw.remove_suffix(1);
        Line line{std::string(raw), {}, {}};
        std::string_view body = raw.substr(0, raw.find('#'));
        const std::size_t eq = body.find('=');
        if (eq != std::string_view::npos)
        {
            const std::string_view key = trim(body.substr(0, eq));
            const std::string_view value = trim(body.substr(eq + 1));
            if (!key.empty() && !value.empty())
            {
                line.key = key;
                line.value = value;
            }
        }
        config.lines_.push_back(std::move(line));
    }
    return config;
}

std::optional<std::string> Config::get(std::string_view key) const
{
    // Sunshine keeps the first occurrence of a key (try_emplace).
    for (const Line &line : lines_)
    {
        if (line.key == key)
            return line.value;
    }
    return std::nullopt;
}

void Config::set(std::string_view key, std::optional<std::string> value)
{
    bool placed = false;
    std::vector<Line> kept;
    kept.reserve(lines_.size() + 1);
    for (Line &line : lines_)
    {
        if (line.key != key)
        {
            kept.push_back(std::move(line));
            continue;
        }
        if (value && !placed)
        {
            kept.push_back({std::string(key) + " = " + *value, std::string(key), *value});
            placed = true;
        }
    }
    if (value && !placed)
        kept.push_back({std::string(key) + " = " + *value, std::string(key), *value});
    lines_ = std::move(kept);
}

std::string Config::text() const
{
    std::string out;
    for (const Line &line : lines_)
        out += line.raw + '\n';
    return out;
}

} // namespace psapp
