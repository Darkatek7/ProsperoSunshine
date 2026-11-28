// SPDX-License-Identifier: GPL-3.0-only
#include "input/profile_list_edit.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
using namespace prospero;

namespace {

std::vector<std::string> lines_of(const std::string &text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        const auto end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start + 1));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return lines;
}

// Every line but those listed is the same, byte for byte, and the file has as many lines (plus `added` at the end).
void only_changed(const std::string &before, const std::string &after, std::vector<size_t> changed, size_t added = 0) {
    const auto a = lines_of(before), b = lines_of(after);
    assert(b.size() == a.size() + added);
    for (size_t i = 0; i < a.size(); i++) {
        const bool listed = std::find(changed.begin(), changed.end(), i) != changed.end();
        // The last line may gain a line ending when a line is added after it.
        if (!listed && !(added && i + 1 == a.size())) assert(a[i] == b[i]);
        if (listed) assert(a[i] != b[i]);
    }
}

std::string profile_of(const std::string &text, const std::string &id) {
    const auto list = parse_profile_list(text);
    assert(list.problems.empty());
    const auto it = list.titles.find(id);
    return it == list.titles.end() ? "" : it->second;
}

}  // namespace

int main() {
    // docs/keyboard-profiles.md's example, with a suggestion the host added below it.
    const std::string example =
        "# A line \"TITLEID = profile\" picks the profile for that title: any title ID of the console, also homebrew and "
        "forwarders.\n"
        "PPSA24662 = driving        # F1 25, not in the map yet\n"
        "PPSA99474 = kart           # my own Mario Kart 8 Deluxe forwarder, with the profile below\n"
        "CUSA13632 = platformer     # Hollow Knight\n"
        "PPSA03527 = gta            # GTA III Definitive Edition, with the profile below; Caps Lock in a car\n"
        "\n"
        "# A profile of your own starts from a profile and changes some keys or mouse buttons.\n"
        "[kart]\n"
        "base = driving\n"
        "Space = r1                 # drift\n"
        "Shift = l1                 # item\n"
        "Mouse1 = r2\n"
        "Q = none                   # nothing\n"
        "\n"
        "[gta]                      # Rockstar's layout: Space jumps, Shift runs, E or F gets in, R reloads\n"
        "base = shooter\n"
        "Space = square\n"
        "\n"
        "# Installed since the last time:\n"
        "# PPSA01317 = driving    # Gran Turismo 7 (from the title map)\n"
        "# PPSA10595 = standard   # Some homebrew\n";

    // A title the file names: its line gets the new profile, its comment and spacing stay.
    {
        const auto after = with_title_profile(example, "PPSA24662", "action");
        only_changed(example, after, {1});
        assert(lines_of(after)[1] == "PPSA24662 = action        # F1 25, not in the map yet\n");
        assert(profile_of(after, "PPSA24662") == "action");
        // Back again gives the file as it was.
        assert(with_title_profile(after, "PPSA24662", "driving") == example);
    }
    // One of the player's own profiles.
    {
        const auto after = with_title_profile(example, "CUSA13632", "kart");
        only_changed(example, after, {3});
        assert(profile_of(after, "CUSA13632") == "kart");
    }
    // A suggestion of the host's loses its "# " and takes the profile; the name stays as its comment.
    {
        const auto after = with_title_profile(example, "PPSA01317", "shooter");
        only_changed(example, after, {19});
        assert(lines_of(after)[19] == "PPSA01317 = shooter    # Gran Turismo 7 (from the title map)\n");
        assert(profile_of(after, "PPSA01317") == "shooter");
        // Taking it away comments it out again; the host's next suggestion pass sees the title as mentioned.
        const auto back = with_title_profile(after, "PPSA01317", "");
        only_changed(example, back, {19});
        assert(lines_of(back)[19] == "# PPSA01317 = shooter    # Gran Turismo 7 (from the title map)\n");
        assert(profile_of(back, "PPSA01317").empty());
        // And choosing it again takes that line.
        assert(with_title_profile(back, "PPSA01317", "shooter") == after);
    }
    // Taking away a title the file names: commented out, the rest as it was.
    {
        const auto after = with_title_profile(example, "PPSA03527", "");
        only_changed(example, after, {4});
        assert(lines_of(after)[4] ==
               "# PPSA03527 = gta            # GTA III Definitive Edition, with the profile below; Caps Lock in a car\n");
        assert(profile_of(after, "PPSA03527").empty());
        assert(parse_profile_list(after).own.contains("gta"));  // its profile stays
    }
    // Taking away a title the file doesn't name changes nothing.
    assert(with_title_profile(example, "PPSA77777", "") == example);
    // A title the file doesn't name yet: a new line at the end, with its name as the comment.
    {
        const auto after = with_title_profile(example, "PPSA77777", "fighting", "Street\nFighter 6 ");
        only_changed(example, after, {}, 1);
        assert(lines_of(after).back() == "PPSA77777 = fighting  # Street Fighter 6\n");
        assert(profile_of(after, "PPSA77777") == "fighting");
        assert(profile_of(after, "PPSA24662") == "driving");
    }
    // Into an empty file, and onto a last line without a line ending.
    assert(with_title_profile("", "PPSA77777", "sports") == "PPSA77777 = sports\n");
    assert(with_title_profile("[mine]\nbase = action", "PPSA77777", "mine") == "[mine]\nbase = action\nPPSA77777 = mine\n");
    // Lines the host reads twice for a title (the last wins) all get the new profile.
    {
        const std::string twice = "PPSA00001 = action\nPPSA00002 = sports\n  PPSA00001=shooter#old\n";
        const auto after = with_title_profile(twice, "PPSA00001", "driving");
        assert(after == "PPSA00001 = driving\nPPSA00002 = sports\n  PPSA00001=driving#old\n");
        assert(with_title_profile(twice, "PPSA00001", "") ==
               "# PPSA00001 = action\nPPSA00002 = sports\n#   PPSA00001=shooter#old\n");
    }
    // A line without a profile after its = gets one.
    assert(with_title_profile("PPSA00001 =\n", "PPSA00001", "action") == "PPSA00001 = action\n");
    assert(with_title_profile("PPSA00001 =# later\n", "PPSA00001", "action") == "PPSA00001 = action # later\n");
    // Windows line endings stay, also on a new line.
    {
        const std::string crlf = "PPSA00001 = action\r\n# PPSA00002 = standard  # A game\r\n";
        assert(with_title_profile(crlf, "PPSA00001", "sports") == "PPSA00001 = sports\r\n# PPSA00002 = standard  # A game\r\n");
        assert(with_title_profile(crlf, "PPSA00002", "action") == "PPSA00001 = action\r\nPPSA00002 = action  # A game\r\n");
        assert(with_title_profile(crlf, "PPSA00003", "action") == crlf + "PPSA00003 = action\r\n");
        assert(with_title_profile("PPSA00001 = action\r", "PPSA00001", "sports") == "PPSA00001 = sports\r");
    }
    // Text that only looks like a title stays: an explanation, another ID that starts the same, a key line in a profile.
    {
        const std::string text = "# TITLEID = profile\nPPSA000011 = action\n[x]\nbase = action\n# see PPSA00001 = later\n";
        assert(with_title_profile(text, "PPSA00001", "sports") == text + "PPSA00001 = sports\n");
    }
    // Which names a title's line may take.
    {
        const auto list = parse_profile_list(example);
        assert(profile_for_title(list, "standard") && profile_for_title(list, "kart") && profile_for_title(list, "gta"));
        assert(!profile_for_title(list, "souls") && !profile_for_title(list, "") && !profile_for_title(list, "Kart"));
    }

    std::cout << "profile_list_edit_test: ok\n";
    return 0;
}
