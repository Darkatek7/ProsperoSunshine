// SPDX-License-Identifier: GPL-3.0-only
// trigger_knock_strength, haptic_mode and haptic_strength from sunshine.conf: a clean number in range is taken, anything else
// gives 100 and a note for the log.
#include "input/pad_settings.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>

using namespace prospero;

static_assert(parse_percent("70", 100).value == 70 && !parse_percent("70", 100).fell_back);
static_assert(parse_percent("0", 100).value == 0 && !parse_percent("0", 100).fell_back);
static_assert(parse_percent("100", 100).value == 100 && !parse_percent("100", 100).fell_back);
static_assert(parse_percent("200", 200).value == 200 && !parse_percent("200", 200).fell_back);
static_assert(parse_percent(" 070\t", 100).value == 70 && !parse_percent(" 070\t", 100).fell_back);
static_assert(parse_percent("70\r", 100).value == 70);

static constexpr bool falls_back(std::string_view text, int max) {
    const Percent parsed = parse_percent(text, max);
    return parsed.fell_back && parsed.value == 100;
}
// What atoi would have read as 0, or as a number: each is the default now.
static_assert(falls_back("off", 100) && falls_back("abc", 200) && falls_back("", 100) && falls_back("   ", 100));
static_assert(falls_back("70%", 100) && falls_back("7O", 100) && falls_back("70 80", 100) && falls_back("0x10", 100));
static_assert(falls_back("-5", 100) && falls_back("+5", 100) && falls_back("12.5", 100));
static_assert(falls_back("101", 100) && falls_back("201", 200) && falls_back("4294967366", 200) && falls_back("9999999999", 100));
static_assert(!falls_back("150", 200) && falls_back("150", 100));

int main() {
    // Nothing set: no knock change, no rumble, the default strength, nothing to say.
    const std::unordered_map<std::string, std::string> none;
    const auto empty = read_pad_settings(none);
    assert(empty.knock == 100 && !empty.rumble && empty.haptic_strength == 100 && empty.notes.empty());

    // All three set as intended.
    const std::map<std::string, std::string> set{{"trigger_knock_strength", "70"}, {"haptic_mode", "rumble"}, {"haptic_strength", "150"}};
    const auto good = read_pad_settings(set);
    assert(good.knock == 70 && good.rumble && good.haptic_strength == 150 && good.notes.empty());
    assert(!(good == empty));
    PadSettings again = good;
    again.notes.push_back("ignored");
    assert(again == good);  // notes don't count as a change

    // trigger_knock_strength = 0 is no knock, not a typo.
    const std::map<std::string, std::string> zero{{"trigger_knock_strength", "0"}, {"haptic_mode", "off"}, {"haptic_strength", "0"}};
    const auto off = read_pad_settings(zero);
    assert(off.knock == 0 && !off.rumble && off.haptic_strength == 0 && off.notes.empty());

    // Typos and values out of range: 100, rumble off, and a note for each.
    const std::map<std::string, std::string> typos{{"trigger_knock_strength", "off"}, {"haptic_mode", "on"}, {"haptic_strength", "250"}};
    const auto bad = read_pad_settings(typos);
    assert(bad.knock == 100 && !bad.rumble && bad.haptic_strength == 100);
    assert(bad.notes.size() == 3);
    assert(bad.notes[0] == "trigger_knock_strength isn't a whole number from 0 to 100, using 100");
    assert(bad.notes[1] == "haptic_strength isn't a whole number from 0 to 200, using 100");
    assert(bad.notes[2] == "haptic_mode is neither rumble nor off, using off");

    puts("PASS: pad settings (percentages: clean numbers in range, 100 and a note for anything else; haptic_mode)");
    return 0;
}
