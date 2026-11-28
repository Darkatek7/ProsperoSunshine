// SPDX-License-Identifier: GPL-3.0-only
// Giving one title a profile in keyboard_profiles.txt (input/title_profile.hpp), or taking it away, as the web page's Apps
// tab does. Every other byte of the file stays as it was: the player's comments, sections, blank lines and line endings.
// Portable, tested in tests/profile_list_edit_test.cpp.
#pragma once
#include "input/title_profile.hpp"
#include <atomic>
#include <string>
#include <string_view>
#include <vector>

namespace prospero {

// Set when the control socket changed keyboard_profiles.txt, so that the game in front gets its profile anew without
// waiting for the next game (Sunshine patch 0043 takes it).
inline std::atomic<bool> profile_list_edited{false};

namespace detail {

constexpr std::string_view list_trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}

// The title a line gives a profile to, as parse_profile_list reads it (the text before the first # and its =), or empty.
constexpr std::string_view list_line_title(std::string_view line) {
    const auto code = line.substr(0, line.find('#'));
    const auto equals = code.find('=');
    if (equals == std::string_view::npos) return {};
    const auto left = list_trim(code.substr(0, equals));
    return console_title_id(left) ? left : std::string_view{};
}

// A line with its profile, the one after the =, replaced: the spaces around it and a comment after it stay.
inline std::string with_line_profile(std::string_view line, std::string_view profile) {
    const auto hash = line.find('#');
    const auto code = line.substr(0, hash);
    const auto equals = code.find('=');
    const auto right = code.substr(equals + 1);
    const auto first = right.find_first_not_of(" \t");
    std::string out(line.substr(0, equals + 1));
    if (first == std::string_view::npos) {
        out += ' ';
        out += profile;
        if (hash != std::string_view::npos) out += ' ';
    } else {
        out += right.substr(0, first);
        out += profile;
        out += right.substr(right.find_last_not_of(" \t") + 1);
    }
    out += line.substr(code.size());
    return out;
}

}  // namespace detail

// A profile a title's line may name: one of the profiles, or one of the player's own in the file.
inline bool profile_for_title(const ProfileList &list, std::string_view name) {
    return parse_profile(name).has_value() || list.own.contains(std::string(name));
}

// keyboard_profiles.txt with title_id given `profile`, or with `profile` empty its lines commented out, so that the title
// gets what the title map or keyboard_mouse_profile gives it. Setting one changes every line the host reads for the title
// (the last wins there, so all of them, and none disagrees), else takes the # off the title's commented line (the host's
// suggestion, profile_list_additions), else adds "TITLEID = profile  # name" at the end. `name` only goes into such a new
// line. The caller checks the title ID and the profile.
inline std::string with_title_profile(std::string_view text, std::string_view title_id, std::string_view profile,
                                      std::string_view name = {}) {
    struct Line {
        std::string_view body, end;  // the line without its line ending, and that ending ("\n", "\r\n" or none)
    };
    std::vector<Line> lines;
    for (std::string_view rest = text; !rest.empty();) {
        const auto newline = rest.find('\n');
        std::string_view body = rest.substr(0, newline), end;
        if (newline != std::string_view::npos) end = rest.substr(newline, 1);
        if (!body.empty() && body.back() == '\r') {
            body.remove_suffix(1);
            end = rest.substr(body.size(), end.size() + 1);
        }
        lines.push_back({body, end});
        rest = newline == std::string_view::npos ? std::string_view{} : rest.substr(newline + 1);
    }

    std::vector<size_t> active;
    size_t suggestion = lines.size();
    for (size_t i = 0; i < lines.size(); i++) {
        if (detail::list_line_title(lines[i].body) == title_id) {
            active.push_back(i);
            continue;
        }
        const auto start = lines[i].body.find_first_not_of(" \t");
        if (suggestion == lines.size() && start != std::string_view::npos && lines[i].body[start] == '#' &&
            detail::list_line_title(lines[i].body.substr(start + 1)) == title_id)
            suggestion = i;
    }

    std::vector<std::string> bodies(lines.size());
    for (size_t i = 0; i < lines.size(); i++) bodies[i] = lines[i].body;
    std::string added;
    if (profile.empty()) {
        for (const size_t i : active) bodies[i] = "# " + bodies[i];
    } else if (!active.empty()) {
        for (const size_t i : active) bodies[i] = detail::with_line_profile(lines[i].body, profile);
    } else if (suggestion < lines.size()) {
        // "# PPSA01234 = standard   # name" loses its "# ": the # and one space after it.
        std::string_view body = lines[suggestion].body;
        const auto hash = body.find('#');
        std::string uncommented(body.substr(0, hash));
        body = body.substr(hash + 1);
        if (!body.empty() && body.front() == ' ') body.remove_prefix(1);
        uncommented += body;
        bodies[suggestion] = detail::with_line_profile(uncommented, profile);
    } else {
        const std::string_view end = text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
        if (!text.empty() && text.back() != '\n') added += end;
        added += std::string(title_id) + " = " + std::string(profile);
        std::string clean;
        for (const char c : name) clean += c == '\n' || c == '\r' ? ' ' : c;
        if (!detail::list_trim(clean).empty()) added += "  # " + std::string(detail::list_trim(clean));
        added += end;
    }

    std::string out;
    out.reserve(text.size() + added.size() + 2 * active.size());
    for (size_t i = 0; i < lines.size(); i++) {
        out += bodies[i];
        out += lines[i].end;
    }
    return out + added;
}

}  // namespace prospero
