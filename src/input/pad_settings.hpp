// SPDX-License-Identifier: GPL-3.0-only
// The pads' settings in sunshine.conf that Sunshine doesn't know: trigger_knock_strength, haptic_mode and haptic_strength.
// Portable, so that the parsing is tested on the build host (tests/pad_settings_test.cpp).
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace prospero {

inline constexpr int knock_strength_max = 100;    // trigger_knock_strength: 100 leaves the knock as the game sent it
inline constexpr int haptic_strength_max = 200;   // haptic_strength: up to twice the default rumble

// A percentage from sunshine.conf: digits only, spaces around them aside, from 0 to max. Anything else, a typo such as "off"
// or "70%", a sign, a fraction or a number past max, gives 100, the value that changes nothing, and says so: read as a number
// it would often have been 0, which turns the effect off.
struct Percent {
    int value = 100;
    bool fell_back = false;
};

constexpr Percent parse_percent(std::string_view text, int max) {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (!text.empty() && space(text.front())) text.remove_prefix(1);
    while (!text.empty() && space(text.back())) text.remove_suffix(1);
    if (text.empty() || text.size() > 9) return {100, true};  // nine digits fit an int
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return {100, true};
        value = value * 10 + (c - '0');
    }
    if (value > max) return {100, true};
    return {value, false};
}

struct PadSettings {
    int knock = 100;            // trigger_knock_strength
    bool rumble = false;        // haptic_mode = rumble
    int haptic_strength = 100;  // haptic_strength
    std::vector<std::string> notes;  // what was set but couldn't be used, for the log

    bool operator==(const PadSettings &other) const {
        return knock == other.knock && rumble == other.rumble && haptic_strength == other.haptic_strength;
    }
};

// settings is the map config::parse_config returns (key to value), or any map with find(std::string).
template <class Map> PadSettings read_pad_settings(const Map &settings) {
    PadSettings read;
    const auto percent = [&](const std::string &key, int max, int &out) {
        const auto found = settings.find(key);
        if (found == settings.end()) return;
        const Percent parsed = parse_percent(found->second, max);
        out = parsed.value;
        if (parsed.fell_back)
            read.notes.push_back(key + " isn't a whole number from 0 to " + std::to_string(max) + ", using 100");
    };
    percent("trigger_knock_strength", knock_strength_max, read.knock);
    percent("haptic_strength", haptic_strength_max, read.haptic_strength);
    if (const auto mode = settings.find(std::string("haptic_mode")); mode != settings.end()) {
        read.rumble = mode->second == "rumble";
        if (!read.rumble && mode->second != "off") read.notes.push_back("haptic_mode is neither rumble nor off, using off");
    }
    return read;
}

}  // namespace prospero
