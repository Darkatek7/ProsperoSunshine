// SPDX-License-Identifier: GPL-3.0-only
#include "web/web_settings.hpp"
#include <cassert>
#include <iostream>
#include <set>
using namespace prospero::web;

int main() {
    // The groups: the app's captions in its reading order, once each, every option's group among them.
    const auto groups = settings_groups();
    assert(groups.size() == 4);
    assert(groups[0].caption == "HOST" && groups[1].caption == "PLAYING" && groups[2].caption == "NETWORK" &&
           groups[3].caption == "EXPERIMENTAL");
    std::set<int> ids;
    for (const auto &g : groups) ids.insert(g.id);
    assert(ids.size() == groups.size());
    for (const auto &spec : psapp::option_specs()) assert(ids.count(static_cast<int>(spec.group)) == 1);

    // fec_percentage is offered, with the app's help text and range.
    const psapp::OptionSpec *fec = psapp::find_option("fec_percentage");
    assert(fec && fec->kind == psapp::OptionKind::number && fec->min == 0 && fec->max == 80 && fec->step == 1);
    assert(std::string(fec->help) == "Rebuilds lost video. More survives more packet loss but costs bitrate.");
    assert(fec->group == psapp::OptionGroup::playing);

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
