// SPDX-License-Identifier: GPL-3.0-only
// sunshine.conf as the app edits it. Sunshine reads "key = value", one option a line, '#' starts a
// comment, and a key that is missing keeps its default (config.cpp in Sunshine a679b059). The app
// knows a handful of keys (kOptions); it changes only their lines and keeps every other line,
// comments included. Pure code, tested on the PC (app/tests).
#pragma once

#include "app/psapp/icon_id.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace psapp
{

enum class OptionKind
{
    text,   // free text (sunshine_name)
    choice, // one of `choices` (the value written is choices[i].value)
    number, // an integer in [min, max]
    toggle, // on ("1") or off: Cross flips it on the tile, and off removes the key
};

// The settings page's sections (settings_layout.hpp places them). Every option names its own, and its icon, as the first two
// fields of its OptionSpec, so an option added without them does not compile.
enum class OptionGroup
{
    host,         // the host's name and log
    network,      // how clients reach it: ports, pairing, encryption, the ping timeout
    playing,      // what a stream does on the console
    experimental, // the firmware switches (experimental_list.h) and surround sound
};

struct Choice
{
    const char *value; // as written to the file
    const char *label; // as shown
};

struct OptionSpec
{
    OptionGroup group;
    icons::Icon icon;
    const char *key;
    const char *label;
    const char *help;
    OptionKind kind;
    const char *default_label; // what the host does without the key
    std::vector<Choice> choices;
    long min = 0;
    long max = 0;
    long step = 1;
    long fallback = 0; // the first number offered when the key is not set
    const char *unit = ""; // after a number's value on its tile
};

// The options the app offers, group by group in the settings page's order.
const std::vector<OptionSpec> &option_specs();
const OptionSpec *find_option(std::string_view key);
// The option's place in experimental_list.h (ps_exp_switch_at), or -1 for an option that is no such switch.
int experimental_switch(const OptionSpec &spec);

// Checks a value for an option; an empty optional means "fine", else the reason.
std::optional<std::string> validate(const OptionSpec &spec, std::string_view value);

class Config;

// The option's value in the file, as the app shows and saves it. close_game_on_quit, upnp and pair_over_internet go through
// Sunshine's own reading of a boolean, so a hand-written `enabled` (Sunshine's own word for upnp) counts as on; the app writes 1.
std::optional<std::string> option_value(const Config &config, const OptionSpec &spec);

class Config
{
  public:
    // Parses the file's text. Never fails: lines it does not understand are kept as they are.
    static Config parse(std::string_view text);

    // The value of a key, or nothing when the key is not set (the host's default applies).
    std::optional<std::string> get(std::string_view key) const;
    // Sets a key: replaces its line (the last one, which is what Sunshine uses, and drops earlier
    // duplicates) or appends one. An empty optional removes the key's lines.
    void set(std::string_view key, std::optional<std::string> value);

    std::string text() const;

  private:
    struct Line
    {
        std::string raw;
        std::string key; // empty for comments, blank and malformed lines
        std::string value;
    };
    std::vector<Line> lines_;
};

} // namespace psapp
