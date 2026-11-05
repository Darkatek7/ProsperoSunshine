// SPDX-License-Identifier: GPL-3.0-only
// Which keyboard pad profile the game in front gets: the player's own list (keyboard_profiles.txt) first, then, with
// keyboard_mouse_profile_by_game on, the map tools/title_profiles.py makes from Wikidata (by title ID, Store concept,
// then English name), else the profile chosen by hand. Portable so it can be unit-tested on the build host.
#pragma once
#include "input/keyboard_pad.hpp"
#include "input/title_profiles.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace prospero {

// PPSA12345 → 12345, CUSA12345 → 112345; nothing for anything else.
constexpr std::optional<uint32_t> title_key(std::string_view id) {
    if (id.size() != 9 || (id.substr(0, 4) != "PPSA" && id.substr(0, 4) != "CUSA")) return std::nullopt;
    uint32_t number = 0;
    for (const char c : id.substr(4)) {
        if (c < '0' || c > '9') return std::nullopt;
        number = number * 10 + static_cast<uint32_t>(c - '0');
    }
    return (id[0] == 'C' ? 100000 : 0) + number;
}

// A name as the map keeps it: ASCII letters and digits in lower case, every run of anything else one space, so
// "TEKKEN™8" and "Tekken 8" are the same. tools/title_profiles.py does the same.
inline std::string match_name(std::string_view name) {
    std::string out;
    bool gap = false;
    for (const char ch : name) {
        const auto c = static_cast<unsigned char>(ch);
        const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
        if (!keep) {
            gap = true;
            continue;
        }
        if (gap && !out.empty()) out += ' ';
        gap = false;
        out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    return out;
}

constexpr uint32_t name_hash(std::string_view matched) {
    uint32_t hash = 2166136261u;  // FNV-1a
    for (const char c : matched) hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
    return hash;
}

namespace detail {
template <typename T, size_t N>
constexpr std::optional<Profile> find_profile(const T (&table)[N], uint64_t key, int shift) {
    const uint64_t mask = (uint64_t{1} << shift) - 1;
    const auto it = std::lower_bound(std::begin(table), std::end(table), key,
                                     [&](T entry, uint64_t wanted) { return (entry & mask) < wanted; });
    if (it == std::end(table) || (*it & mask) != key) return std::nullopt;
    return static_cast<Profile>(*it >> shift);
}
}

enum class ProfileSource : uint8_t { list, title_id, store_concept, name, chosen };

struct ProfileChoice {
    Profile profile;
    std::shared_ptr<const CustomProfile> own;  // a profile of the player's own on top of profile, its base
    ProfileSource source;
    bool outside = false;  // no game running: the menu keys
};

// Any title ID of the console, four capitals and five digits: games (PPSA, CUSA), homebrew and forwarders alike.
constexpr bool console_title_id(std::string_view id) {
    if (id.size() != 9) return false;
    for (size_t i = 0; i < 9; i++)
        if (i < 4 ? !(id[i] >= 'A' && id[i] <= 'Z') : !(id[i] >= '0' && id[i] <= '9')) return false;
    return true;
}

// The keys of keyboard_profiles.txt by name, as on a US keyboard: A to Z, 0 to 9, F1 to F12, Num0 to Num9, Space,
// Enter, Esc, Tab, Backspace, Shift, Ctrl, Alt (the left one), Up, Down, Left, Right, Insert, Delete, Home, End, PageUp,
// PageDown and the punctuation keys. Not right Alt (standard while held) nor Caps Lock (the vehicle layer).
inline std::vector<uint16_t> key_codes(std::string_view name) {
    std::string lower;
    for (const char c : name) lower += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    if (lower.size() == 1) {
        const char c = lower[0];
        if (c >= 'a' && c <= 'z') return {static_cast<uint16_t>(c - 'a' + 'A')};
        if (c >= '0' && c <= '9') return {static_cast<uint16_t>(c)};
        constexpr std::string_view marks = ";=,-./`[\\]'";
        constexpr uint16_t codes[] = {0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xDB, 0xDC, 0xDD, 0xDE};
        if (const auto at = marks.find(c); at != std::string_view::npos) return {codes[at]};
        return {};
    }
    if (lower.size() >= 2 && lower[0] == 'f') {
        int n = 0;
        const auto [end, ec] = std::from_chars(lower.data() + 1, lower.data() + lower.size(), n);
        if (ec == std::errc{} && end == lower.data() + lower.size() && n >= 1 && n <= 12) return {static_cast<uint16_t>(0x6F + n)};
    }
    if (lower.size() == 4 && lower.starts_with("num") && lower[3] >= '0' && lower[3] <= '9')
        return {static_cast<uint16_t>(0x60 + lower[3] - '0')};
    struct Named { std::string_view name; uint16_t vk; };
    constexpr Named named[] = {
        {"space", vk::space}, {"enter", vk::enter}, {"esc", vk::esc}, {"escape", vk::esc}, {"tab", vk::tab},
        {"backspace", vk::backspace}, {"up", vk::up}, {"down", vk::down}, {"left", vk::left}, {"right", vk::right},
        {"insert", 0x2D}, {"delete", 0x2E}, {"home", 0x24}, {"end", 0x23}, {"pageup", 0x21}, {"pagedown", 0x22},
    };
    for (const auto &n : named)
        if (n.name == lower) return {n.vk};
    if (lower == "shift") return {vk::shift, vk::lshift, vk::rshift};
    if (lower == "ctrl") return {vk::ctrl, vk::lctrl, vk::rctrl};
    if (lower == "alt") return {0x12, 0xA4};  // left Alt, and the side-less code some clients send
    return {};
}

// What a key or mouse button does in keyboard_profiles.txt: cross, circle, square, triangle, l1, r1, l2, r2, l3, r3,
// options, create, ps, touchpad, dpad-up, dpad-down, dpad-left, dpad-right, left-up, left-down, left-left, left-right
// (the left stick), right-up ... (the right stick), half, or none.
inline std::optional<uint64_t> control_by_name(std::string_view name) {
    using namespace moonlight;
    using namespace pad_control;
    struct Named { std::string_view name; uint64_t control; };
    constexpr Named named[] = {
        {"cross", a}, {"circle", b}, {"square", x}, {"triangle", y}, {"l1", left_button}, {"r1", right_button},
        {"l2", l2}, {"r2", r2}, {"l3", left_stick}, {"r3", right_stick}, {"options", start}, {"create", back},
        {"ps", home}, {"touchpad", touchpad}, {"dpad-up", dpad_up}, {"dpad-down", dpad_down}, {"dpad-left", dpad_left},
        {"dpad-right", dpad_right}, {"left-up", left_up}, {"left-down", left_down}, {"left-left", left_left},
        {"left-right", left_right}, {"right-up", right_up}, {"right-down", right_down}, {"right-left", right_left},
        {"right-right", right_right}, {"half", half}, {"none", 0},
    };
    std::string lower;
    for (const char c : name) lower += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    for (const auto &n : named)
        if (n.name == lower) return n.control;
    return std::nullopt;
}

// The player's own list, keyboard_profiles.txt next to sunshine.conf. # starts a comment.
//   PPSA99474 = driving       a title (any title ID: games, homebrew, forwarders) and its profile
//   [kart]                    a profile of the player's own, until the next [name]:
//   base = driving              the profile it starts from (standard unless set)
//   Space = r1                  a key and what it does instead, or none
//   Mouse1 = r2                 a mouse button (1 left, 2 middle, 3 right, 4 back, 5 forward)
//   WheelUp = l1                a wheel step (WheelUp, WheelDown, WheelLeft, WheelRight): a tap
//   PPSA99475 = kart          titles can take it anywhere in the file
// Lines it can't read are skipped and listed in problems, with their line numbers.
struct ProfileList {
    std::unordered_map<std::string, std::string> titles;  // title ID → profile name, as written
    std::unordered_map<std::string, std::shared_ptr<const CustomProfile>> own;
    std::vector<std::string> problems;
};

inline ProfileList parse_profile_list(std::string_view text) {
    ProfileList list;
    const auto trim = [](std::string_view s) {
        const auto first = s.find_first_not_of(" \t\r");
        if (first == std::string_view::npos) return std::string_view{};
        return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
    };
    const auto lowered = [](std::string_view s) {
        std::string out;
        for (const char c : s) out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        return out;
    };
    std::unordered_map<std::string, CustomProfile> own;
    CustomProfile *section = nullptr;
    int number = 0;
    while (!text.empty()) {
        const auto end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        ++number;
        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) continue;
        const auto problem = [&](std::string_view why) { list.problems.push_back("line " + std::to_string(number) + ": " + std::string(why)); };
        if (line.front() == '[' && line.back() == ']') {
            const std::string name = lowered(trim(line.substr(1, line.size() - 2)));
            const bool valid = !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
            });
            section = nullptr;
            if (!valid) problem("a profile name takes letters, digits, - and _");
            else if (parse_profile(name)) problem("[" + name + "] is a profile already");
            else {
                section = &own[name];
                section->name = name;
            }
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) {
            problem("no =");
            continue;
        }
        const auto left = trim(line.substr(0, equals)), right = trim(line.substr(equals + 1));
        if (console_title_id(left)) {
            list.titles[std::string(left)] = lowered(right);
            continue;
        }
        if (!section) {
            problem("neither a title ID nor inside a [profile]");
            continue;
        }
        if (lowered(left) == "base") {
            if (const auto base = parse_profile(lowered(right))) section->base = *base;
            else problem("no profile " + std::string(right));
            continue;
        }
        const auto control = control_by_name(right);
        if (!control) {
            problem("no button " + std::string(right));
            continue;
        }
        const auto lower_left = lowered(left);
        if (lower_left.size() == 6 && lower_left.starts_with("mouse") && lower_left[5] >= '1' && lower_left[5] <= '5') {
            section->mouse[lower_left[5] - '0'] = *control;
            continue;
        }
        constexpr std::string_view wheel_sides[] = {"wheelup", "wheeldown", "wheelleft", "wheelright"};
        if (const auto side = std::find(std::begin(wheel_sides), std::end(wheel_sides), lower_left); side != std::end(wheel_sides)) {
            section->wheel[side - std::begin(wheel_sides)] = *control;
            continue;
        }
        const auto codes = key_codes(left);
        if (codes.empty()) {
            problem("no key " + std::string(left));
            continue;
        }
        for (const uint16_t code : codes) section->keys.push_back({code, *control});
    }
    for (auto &[name, profile] : own) list.own[name] = std::make_shared<const CustomProfile>(std::move(profile));
    return list;
}

// concept_id: the conceptId of a PS5 game's param.json, 0 if none. name: its English name, else its default one.
// use_map: keyboard_mouse_profile_by_game; the player's own list counts either way.
inline ProfileChoice choose_profile(std::string_view title_id, uint32_t concept_id, std::string_view name,
                                    const ProfileList &list, Profile chosen, bool use_map = true) {
    if (const auto it = list.titles.find(std::string(title_id)); it != list.titles.end()) {
        if (const auto profile = parse_profile(it->second)) return {*profile, nullptr, ProfileSource::list};
        if (const auto own = list.own.find(it->second); own != list.own.end())
            return {own->second->base, own->second, ProfileSource::list};
    }
    const auto key = title_key(title_id);
    if (!use_map || !key) return {chosen, nullptr, ProfileSource::chosen};
    if (const auto found = detail::find_profile(title_profiles, *key, 24)) return {*found, nullptr, ProfileSource::title_id};
    if (concept_id)
        if (const auto found = detail::find_profile(concept_profiles, concept_id, 32))
            return {*found, nullptr, ProfileSource::store_concept};
    if (const auto matched = match_name(name); !matched.empty())
        if (const auto found = detail::find_profile(name_profiles, name_hash(matched), 32))
            return {*found, nullptr, ProfileSource::name};
    return {chosen, nullptr, ProfileSource::chosen};
}

constexpr std::string_view source_name(ProfileSource source) {
    switch (source) {
    case ProfileSource::list: return "your list";
    case ProfileSource::title_id: return "the title map";
    case ProfileSource::store_concept: return "the title map, by Store concept";
    case ProfileSource::name: return "the title map, by name";
    case ProfileSource::chosen: return "keyboard_mouse_profile";
    }
    return "";
}

// An installed title for the list: its ID, its name, and the profile the title map gives it (nothing if none).
struct ListedTitle {
    std::string id, name;
    std::optional<Profile> mapped;
};

// What the host adds to keyboard_profiles.txt: the installed titles the file doesn't mention yet, even in a comment, as
// commented lines with the profile they would get, so choosing one is removing a #. The explanation goes on top of a new
// file. Empty if there is nothing to add.
inline std::string profile_list_additions(std::string_view existing, std::span<const ListedTitle> titles) {
    std::string lines;
    for (const auto &title : titles) {
        if (!console_title_id(title.id) || existing.find(title.id) != std::string_view::npos) continue;
        std::string name;
        for (const char c : title.name) name += c == '\n' || c == '\r' ? ' ' : c;
        const std::string profile(title.mapped ? profile_name(*title.mapped) : "standard");
        lines += "# " + title.id + " = " + profile + std::string(profile.size() < 10 ? 10 - profile.size() : 1, ' ') + "# " +
                 name + (title.mapped ? " (from the title map)" : "") + "\n";
    }
    if (lines.empty()) return {};
    if (!existing.empty())
        return std::string(existing.back() == '\n' ? "" : "\n") + "\n# Installed since the last time:\n" + lines;
    return "# Keyboard and mouse profiles for your games (docs/keyboard-profiles.md). Optional: without this file, a\n"
           "# device without a controller plays with keyboard_mouse_profile from sunshine.conf.\n"
           "#\n"
           "# A line \"TITLEID = profile\" picks the profile for that title, also for homebrew and forwarders:\n"
           "# standard, action, shooter, driving, fighting, platformer, sports, or one of your own. Remove the # in\n"
           "# front of a line to use it. The host reads this file when it starts and whenever the game in front\n"
           "# changes, and adds the titles you install later.\n"
           "#\n"
           "# A profile of your own starts from one of those and changes some keys or mouse buttons, e.g.:\n"
           "# [kart]\n"
           "# base = driving\n"
           "# Space = r1\n"
           "# Shift = l1\n"
           "# Mouse1 = r2\n"
           "\n" +
           lines;
}

}  // namespace prospero
