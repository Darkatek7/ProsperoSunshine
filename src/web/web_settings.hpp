// SPDX-License-Identifier: GPL-3.0-only
// What the web page's Settings tab shows besides the options themselves: the groups, in the order and with the captions the
// app's Settings page has, and the Experimental firmware switches the way the app words them. Portable (tests/web_settings_test.cpp).
//
// The groups come from the app's layout (app/src/psapp/settings_layout.cpp: the captions of the page in reading order), so a
// new group there shows on the web page without a line here. The words for a firmware switch that can't change anything on this
// firmware are the app's (screen.cpp, value_text and the help bar): "Verified here" / the list's own "No effect here" and the
// help sentences. They are written out here once for the page; the app has its own copy of the two sentences in screen.cpp.
#pragma once
#include "app/psapp/conf.hpp"
#include "app/psapp/experimental_list.h"
#include "app/psapp/settings_layout.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace prospero::web {

struct SettingsGroup {
    int id;               // the OptionGroup's number; each option of the page names its group by it
    std::string caption;  // as the app shows it ("HOST"); the page sets it in spaced capitals too
};

// The groups that have options, in the app's reading order (Host, Playing, Network, Experimental).
inline std::vector<SettingsGroup> settings_groups() {
    std::vector<SettingsGroup> groups;
    for (const auto &caption : psapp::settings_layout::page().captions)
        groups.push_back({static_cast<int>(caption.group), caption.text});
    return groups;
}

// "Firmware 13.00", or "this firmware" while the host doesn't know it. `text` is the firmware as "13.00".
inline std::string firmware_place(std::uint32_t firmware, std::string_view text) {
    return firmware != 0 ? "Firmware " + std::string(text) : std::string("this firmware");
}

// An Experimental firmware switch (experimental_list.h) on the page. The page shows it and doesn't change it: the app checks
// the firmware and tells what a failed trial turned off. `locked`: on this firmware it can't change anything, so the app
// shows its state in words instead of a switch.
struct SwitchState {
    bool firmware_switch = false;  // false: an ordinary option (Surround is in the Experimental group but no firmware decides it)
    bool locked = false;
    std::string state;  // locked: the app's word for it; else "On" or "Off"
    std::string help;   // locked: the app's sentence; else the option's own
};

// `value` is the option's value in sunshine.conf ("1" is on).
inline SwitchState switch_state(const psapp::OptionSpec &spec, std::uint32_t firmware, std::string_view firmware_name,
                                const std::string &value) {
    SwitchState out;
    const int index = psapp::experimental_switch(spec);
    out.help = spec.help;
    if (index < 0) return out;
    out.firmware_switch = true;
    const ps_exp_switch_info *info = ps_exp_switch_at(index);
    // An unknown firmware (0) keeps the switch: the host still decides, as in the app (diag.cpp, experimental_needed).
    out.locked = firmware != 0 && !ps_exp_counts(index, firmware);
    if (!out.locked) {
        out.state = value == "1" ? "On" : "Off";
        return out;
    }
    const bool no_effect = info->no_effect(firmware) != 0;
    out.state = no_effect ? info->no_effect_word : "Verified here";
    out.help = no_effect ? std::string(info->no_effect_help)
                         : "Verified on " + firmware_place(firmware, firmware_name) + ": it is there without a switch, and this switch changes nothing.";
    return out;
}

// The line under the Experimental caption in the app: whether anything there needs a switch on this firmware.
inline std::string experimental_note(bool any_needed, std::string_view where) {
    return any_needed ? "For what is untested on " + std::string(where) + ". Off by default; what fails turns itself off."
                      : "Nothing to switch on " + std::string(where) + ": all of this is verified there.";
}

// Whether any firmware switch counts on this firmware (the caption's note depends on it).
inline bool any_switch_needed(std::uint32_t firmware) {
    for (int i = 0; i < PS_EXP_SWITCH_COUNT; ++i)
        if (firmware == 0 || ps_exp_counts(i, firmware)) return true;
    return false;
}

}  // namespace prospero::web
