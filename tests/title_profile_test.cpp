// SPDX-License-Identifier: GPL-3.0-only
#include "input/title_profile.hpp"
#include <cassert>
#include <iostream>
using namespace prospero;

int main() {
    static_assert(title_key("PPSA01317") == 1317u && title_key("CUSA13632") == 113632u && title_key("PPSA00000") == 0u);
    static_assert(!title_key("PPSA0131") && !title_key("NPXS40087") && !title_key("PPSA0131x") && !title_key(""));

    assert(match_name("TEKKEN\xe2\x84\xa2" "8") == "tekken 8");  // TEKKEN™8
    assert(match_name("  Need for Speed\xe2\x84\xa2 Unbound ") == "need for speed unbound");
    assert(match_name("Marvel's Spider-Man 2") == "marvel s spider man 2");
    assert(match_name("\xe3\x82\xb2\xe3\x83\xbc\xe3\x83\xa0").empty());  // no ASCII letter or digit
    static_assert(name_hash("") == 2166136261u && name_hash("a") == 0xe40c292cu);  // FNV-1a

    // The generated tables are sorted by key, so the lookups can halve them.
    const auto sorted = [](const auto &table, uint64_t mask) {
        return std::is_sorted(std::begin(table), std::end(table), [&](auto a, auto b) { return (a & mask) < (b & mask); });
    };
    assert(sorted(title_profiles, 0xFFFFFF) && sorted(concept_profiles, 0xFFFFFFFF) && sorted(name_profiles, 0xFFFFFFFF));
    assert(std::size(title_profiles) > 1000 && std::size(concept_profiles) > 1000 && std::size(name_profiles) > 1000);

    // Games from the map, as they are on a console: the title ID first, then the Store concept, then the English name.
    const ProfileList none;
    const auto choose = [&](const char *id, uint32_t concept_id, const char *name) {
        return choose_profile(id, concept_id, name, none, Profile::standard);
    };
    assert(choose("PPSA01317", 0, "").profile == Profile::driving);                // Gran Turismo 7
    assert(choose("PPSA10595", 0, "").profile == Profile::fighting);               // Tekken 8
    assert(choose("PPSA08329", 0, "").profile == Profile::action);                 // God of War Ragnarök: hack and slash, not fighting
    assert(choose("PPSA04610", 0, "").profile == Profile::action);                 // Elden Ring
    assert(choose("CUSA13632", 0, "").profile == Profile::platformer);             // Hollow Knight
    assert(choose("PPSA01317", 0, "").source == ProfileSource::title_id);
    assert(choose("PPSA03355", 10002648, "Hades").profile == Profile::action);    // by Store concept
    assert(choose("PPSA03355", 10002648, "Hades").source == ProfileSource::store_concept);
    assert(choose("PPSA22327", 10010647, "Forza Horizon 5").profile == Profile::driving);  // by name
    assert(choose("PPSA22327", 10010647, "Forza Horizon 5").source == ProfileSource::name);
    assert(choose("PPSA99999", 0, "No such game anywhere").source == ProfileSource::chosen);
    assert(choose_profile("PPSA99999", 0, "", none, Profile::sports).profile == Profile::sports);
    assert(choose_profile("NPXS40087", 0, "", none, Profile::shooter).profile == Profile::shooter);  // not a game

    // The player's own list wins over the map, takes any title ID (homebrew, forwarders) and counts with the map off.
    const auto list = parse_profile_list("# my games\n"
                                         "PPSA01317 = fighting\r\n"
                                         "  CUSA13632=Sports   # comment\n"
                                         "PPSA24662 = driving\n"
                                         "PPSA99474 = kart          # my Mario Kart 8 Deluxe forwarder\n"
                                         "PPSA00001 = flying\n"
                                         "nonsense\n"
                                         "\n"
                                         "[kart]\n"
                                         "base = driving\n"
                                         "Space = r1\n"
                                         "Shift = L1\n"
                                         "Mouse1 = r2\n"
                                         "WheelUp = l1\n"
                                         "wheeldown = L1\n"
                                         "Q = none\n"
                                         "F13 = cross\n"
                                         "Alt = cross\n"
                                         "W = jump\n"
                                         "[standard]\n"
                                         "[bad name!]\n"
                                         "B = cross\n");
    assert(list.titles.size() == 5 && list.own.size() == 1);
    assert(list.problems.size() == 6);  // nonsense, F13, jump, [standard], [bad name!], and B outside a profile
    assert(list.problems[0] == "line 7: no =");
    assert(choose_profile("PPSA01317", 0, "", list, Profile::standard).profile == Profile::fighting);
    assert(choose_profile("PPSA01317", 0, "", list, Profile::standard).source == ProfileSource::list);
    assert(choose_profile("CUSA13632", 0, "", list, Profile::standard).profile == Profile::sports);
    assert(choose_profile("PPSA24662", 0, "F1 25", list, Profile::standard, false).profile == Profile::driving);
    const auto kart = choose_profile("PPSA99474", 0, "Mario Kart 8 Deluxe", list, Profile::standard, false);
    assert(kart.profile == Profile::driving && kart.own && kart.own->name == "kart" && kart.source == ProfileSource::list);
    assert(kart.own->keys.size() == 1 + 3 + 1 + 2);  // Space, Shift (three codes), Q, Alt (two)
    assert(kart.own->mouse[1] == pad_control::r2);
    assert(kart.own->wheel[0] == moonlight::left_button && kart.own->wheel[1] == moonlight::left_button && !kart.own->wheel[2]);
    // A name nobody knows falls through to the map, or to the profile chosen by hand.
    assert(choose_profile("PPSA00001", 0, "", list, Profile::shooter).source == ProfileSource::chosen);
    // With the map off only the list counts.
    assert(choose_profile("PPSA10595", 0, "", list, Profile::shooter, false).profile == Profile::shooter);
    assert(choose_profile("PPSA10595", 0, "", list, Profile::shooter, true).profile == Profile::fighting);
    assert(parse_profile_list("").titles.empty());
    assert(source_name(ProfileSource::list) == "your list");
    static_assert(console_title_id("PPSA99474") && console_title_id("LAPY10010") && !console_title_id("ppsa99474"));
    assert(key_codes("space") == std::vector<uint16_t>{0x20} && key_codes("F12") == std::vector<uint16_t>{0x7B});
    assert(key_codes("num5") == std::vector<uint16_t>{0x65} && key_codes(";") == std::vector<uint16_t>{0xBA});
    assert(key_codes("alt") == (std::vector<uint16_t>{0x12, 0xA4}));  // the left one: right Alt is standard while held
    assert(key_codes("CapsLock").empty() && key_codes("AltGr").empty() && key_codes("RightAlt").empty() && key_codes("F0").empty());
    assert(control_by_name("TRIANGLE") == moonlight::y && control_by_name("none") == 0u && !control_by_name("jump"));

    // The installed titles the file doesn't mention yet, as comments with the profile they would get.
    const ListedTitle installed[] = {{"PPSA01716", "Need for Speed Unbound", Profile::driving},
                                     {"PPSA99474", "Mario Kart 8 Deluxe", std::nullopt},
                                     {"PPSA24662", "F1\xc2\xae 25\n", std::nullopt}};
    const auto fresh = profile_list_additions("", installed);
    assert(fresh.starts_with("# Keyboard and mouse profiles"));
    assert(fresh.find("# PPSA01716 = driving   # Need for Speed Unbound (from the title map)\n") != std::string::npos);
    assert(fresh.find("# PPSA99474 = standard  # Mario Kart 8 Deluxe\n") != std::string::npos);
    assert(fresh.find("F1\xc2\xae 25 \n") != std::string::npos);  // no line break inside a name
    const auto later = profile_list_additions("PPSA99474 = kart\n# PPSA01716 = driving", installed);
    assert(later == "\n\n# Installed since the last time:\n# PPSA24662 = standard  # F1\xc2\xae 25 \n");
    assert(profile_list_additions(fresh, installed).empty());

    std::cout << "PASS: title profile (title keys, names, sorted tables, title ID, Store concept, name, own list, own profiles, "
                 "installed titles)\n";
}
