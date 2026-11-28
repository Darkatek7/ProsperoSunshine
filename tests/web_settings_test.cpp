// SPDX-License-Identifier: GPL-3.0-only
#include "web/web_settings.hpp"
#include "input/keyboard_pad.hpp"
#include <cassert>
#include <iostream>
#include <set>
using namespace prospero::web;

int main() {
    // The groups: the app's captions in its reading order, once each, every option's group among them.
    const auto groups = settings_groups();
    assert(groups.size() == 6);
    assert(groups[0].caption == "HOST" && groups[1].caption == "PLAYING" && groups[2].caption == "ERROR CORRECTION" &&
           groups[3].caption == "NETWORK" && groups[4].caption == "CONTROLLER" && groups[5].caption == "EXPERIMENTAL");
    std::set<int> ids;
    for (const auto &g : groups) ids.insert(g.id);
    assert(ids.size() == groups.size());
    for (const auto &spec : psapp::option_specs()) assert(ids.count(static_cast<int>(spec.group)) == 1);
    for (const auto &spec : psapp::web_option_specs()) assert(ids.count(static_cast<int>(spec.group)) == 1);

    // The keyboard's settings (docs/keyboard-profiles.md): by game in the app's Controller group, the other two on the web page
    // only, found by find_option all the same, with the host's profiles and the host's range for the mouse.
    const psapp::OptionSpec *by_game = psapp::find_option("keyboard_mouse_profile_by_game");
    assert(by_game && by_game->group == psapp::OptionGroup::controller && by_game->kind == psapp::OptionKind::toggle);
    assert(std::any_of(psapp::option_specs().begin(), psapp::option_specs().end(),
                       [](const psapp::OptionSpec &spec) { return std::string(spec.key) == "keyboard_mouse_profile_by_game"; }));
    const psapp::OptionSpec *profile = psapp::find_option("keyboard_mouse_profile");
    assert(profile && profile->kind == psapp::OptionKind::choice && profile->choices.size() == prospero::profile_names.size());
    for (size_t i = 0; i < prospero::profile_names.size(); i++) assert(profile->choices[i].value == prospero::profile_names[i]);
    assert(std::string(profile->default_label) == "Standard");
    const psapp::OptionSpec *speed = psapp::find_option("keyboard_mouse_sensitivity");
    assert(speed && speed->kind == psapp::OptionKind::number);
    assert(speed->min == prospero::min_sensitivity && speed->max == prospero::max_sensitivity &&
           speed->fallback == prospero::default_sensitivity);
    assert(psapp::validate(*speed, "100") == std::nullopt && psapp::validate(*speed, "5") && psapp::validate(*speed, "1001"));
    for (const auto &spec : psapp::web_option_specs())
        assert(std::none_of(psapp::option_specs().begin(), psapp::option_specs().end(),
                            [&spec](const psapp::OptionSpec &app) { return std::string(app.key) == spec.key; }));

    // fec_percentage is offered, with the app's help text and range.
    const psapp::OptionSpec *fec = psapp::find_option("fec_percentage");
    assert(fec && fec->kind == psapp::OptionKind::number && fec->min == 0 && fec->max == 80 && fec->step == 1);
    assert(std::string(fec->help) == "Rebuilds lost video. More survives more packet loss but costs bitrate.");
    assert(fec->group == psapp::OptionGroup::correction && std::string(fec->label) == "At home");
    // The share for devices over the internet (Sunshine patch 0039), unset by default: then they get fec_percentage.
    const psapp::OptionSpec *internet = psapp::find_option("internet_fec_percentage");
    assert(internet && internet->group == psapp::OptionGroup::correction && internet->kind == psapp::OptionKind::number);
    assert(internet->min == 0 && internet->max == 80 && std::string(internet->default_label) == "As at home");
    // The Controller group: the three the host reads itself (src/input/pad_settings.hpp), with its values and ranges.
    const psapp::OptionSpec *mode = psapp::find_option("haptic_mode");
    assert(mode && mode->group == psapp::OptionGroup::controller && mode->kind == psapp::OptionKind::choice);
    assert(!psapp::validate(*mode, "rumble") && !psapp::validate(*mode, "off") && psapp::validate(*mode, "1"));
    const psapp::OptionSpec *level = psapp::find_option("haptic_strength");
    assert(level && level->group == psapp::OptionGroup::controller && level->min == 0 && level->max == 200);
    assert(!psapp::validate(*level, "200") && psapp::validate(*level, "201"));
    const psapp::OptionSpec *knock = psapp::find_option("trigger_knock_strength");
    assert(knock && knock->group == psapp::OptionGroup::controller && knock->min == 0 && knock->max == 100);
    assert(!psapp::validate(*knock, "0") && psapp::validate(*knock, "101") && psapp::validate(*knock, "70%"));

    const auto state = [](const char *key, std::uint32_t firmware, const std::string &value = "") {
        return switch_state(*psapp::find_option(key), firmware, firmware == 0x13000040u ? "13.00" : "10.20", value);
    };
    // 13.00: HEVC, HDR, 1440p and 4K, 150 Mbit/s and Below 1080p are verified there without a switch.
    for (const char *key : {"experimental_hevc", "experimental_hdr", "experimental_large_modes", "experimental_high_bitrate",
                            "experimental_small_modes"}) {
        const auto s = state(key, 0x13000040u, "1");
        assert(s.firmware_switch && s.locked && s.state == "Verified here");
        assert(s.help == "Verified on Firmware 13.00: it is there without a switch, and this switch changes nothing.");
    }
    // 10.20: 150 Mbit/s can change nothing (the record has no rate fields); 1440p and 4K still needs its switch.
    auto s = state("experimental_high_bitrate", 0x10200040u);
    assert(s.locked && s.state == "No effect here" && s.help.find("keeps its own rate") != std::string::npos);
    s = state("experimental_hevc", 0x10200040u);
    assert(s.locked && s.state == "Verified here" && s.help.find("Firmware 10.20") != std::string::npos);
    s = state("experimental_large_modes", 0x10200040u, "1");
    assert(s.firmware_switch && !s.locked && s.state == "On" && s.help == psapp::find_option("experimental_large_modes")->help);
    assert(state("experimental_large_modes", 0x10200040u).state == "Off");
    // An unknown firmware keeps every switch: the host decides.
    s = state("experimental_hevc", 0);
    assert(s.firmware_switch && !s.locked);
    // Surround sits in the Experimental group but no firmware decides it; an ordinary option is none of this.
    s = state("surround_sound", 0x13000040u);
    assert(!s.firmware_switch && !s.locked && s.state.empty());
    assert(psapp::find_option("surround_sound")->group == psapp::OptionGroup::experimental);
    assert(!state("fec_percentage", 0x13000040u).firmware_switch);

    assert(firmware_place(0x13000040u, "13.00") == "Firmware 13.00" && firmware_place(0, "") == "this firmware");
    assert(!any_switch_needed(0x13000040u) && any_switch_needed(0x10200040u) && any_switch_needed(0));
    assert(experimental_note(false, "Firmware 13.00") == "Nothing to switch on Firmware 13.00: all of this is verified there.");
    assert(experimental_note(true, "Firmware 10.20").find("untested on Firmware 10.20") != std::string::npos);
    std::cout << "PASS web_settings_test\n";
}
