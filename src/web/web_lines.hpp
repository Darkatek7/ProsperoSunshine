// SPDX-License-Identifier: GPL-3.0-only
// The web page asks the host through the same commands as the app (control_server.hpp) and turns their answer lines into
// records here. Portable, tested on the PC (tests/web_lines_test.cpp).
#pragma once
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prospero::web {

using Record = std::map<std::string, std::string>;

// The answer's "OK ..." lines without the "OK ", up to END. An "ERR ..." answer gives no lines.
inline std::vector<std::string> ok_lines(std::string_view answer) {
    std::vector<std::string> lines;
    while (!answer.empty()) {
        const size_t end = answer.find('\n');
        const std::string_view line = answer.substr(0, end);
        answer = end == std::string_view::npos ? std::string_view() : answer.substr(end + 1);
        if (line.size() >= 3 && line.substr(0, 3) == "OK ") lines.emplace_back(line.substr(3));
        else if (line == "OK") lines.emplace_back();
    }
    return lines;
}

// "key=value key=value last=rest of the line": the value of `last` is everything after it, as it may hold spaces.
inline Record fields(std::string_view line, std::string_view last) {
    Record record;
    while (!line.empty()) {
        while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) break;
        const std::string key(line.substr(0, eq));
        line.remove_prefix(eq + 1);
        if (key == last) {
            record[key] = std::string(line);
            break;
        }
        const size_t space = line.find(' ');
        record[key] = std::string(line.substr(0, space));
        line = space == std::string_view::npos ? std::string_view() : line.substr(space + 1);
    }
    return record;
}

// STATUS: "running=<0|1> app=<name> pending=<n> lan=<0|1> address=<ip> client=<name>"; the app's name may hold spaces too, so
// the keys are found by name and in this order, each value running to the next key found. A key an older host doesn't send
// (lan) is left out of the record.
inline Record status(std::string_view line) {
    Record record;
    const std::string padded = " " + std::string(line);
    const std::string_view keys[] = {"running", "app", "pending", "lan", "address", "client"};
    std::vector<std::pair<std::string_view, size_t>> found;  // a key, and where its " key=" starts
    size_t from = 0;
    for (const auto key : keys) {
        const size_t at = padded.find(" " + std::string(key) + "=", from);
        if (at == std::string::npos) continue;
        found.emplace_back(key, at);
        from = at + key.size() + 2;
    }
    for (size_t i = 0; i < found.size(); ++i) {
        const size_t start = found[i].second + found[i].first.size() + 2;
        const size_t end = i + 1 < found.size() ? found[i + 1].second : padded.size();
        record[std::string(found[i].first)] = padded.substr(start, end - start);
    }
    return record;
}

// DEVICES: "<uuid> <streaming 0|1> <mode or -> <name>".
inline Record device(std::string_view line) {
    Record record;
    const char *keys[] = {"uuid", "streaming", "mode"};
    for (const char *key : keys) {
        const size_t space = line.find(' ');
        record[key] = std::string(line.substr(0, space));
        line = space == std::string_view::npos ? std::string_view() : line.substr(space + 1);
    }
    if (record["mode"] == "-") record["mode"].clear();
    record["name"] = std::string(line);
    return record;
}

// PAIRINGS: "<id> <name> <address>": the name may hold spaces, the address is the last word.
inline Record pairing(std::string_view line) {
    Record record;
    const size_t first = line.find(' ');
    record["id"] = std::string(line.substr(0, first));
    std::string_view rest = first == std::string_view::npos ? std::string_view() : line.substr(first + 1);
    const size_t last = rest.rfind(' ');
    if (last == std::string_view::npos) {
        record["name"] = std::string(rest);
        record["address"] = "";
    } else {
        record["name"] = std::string(rest.substr(0, last));
        record["address"] = std::string(rest.substr(last + 1));
    }
    return record;
}

// USERS: "<id> <name>", the name the rest of the line (it may hold spaces, or be empty).
inline Record console_user(std::string_view line) {
    const size_t space = line.find(' ');
    return {{"id", std::string(line.substr(0, space))},
            {"name", space == std::string_view::npos ? std::string() : std::string(line.substr(space + 1))}};
}

// PROFILES: "<uuid> <profiles>".
inline Record device_profiles(std::string_view line) {
    const size_t space = line.find(' ');
    return {{"uuid", std::string(line.substr(0, space))},
            {"profiles", space == std::string_view::npos ? std::string() : std::string(line.substr(space + 1))}};
}

// APPS: "<title id> <hidden|shown> <name>", the name the rest of the line (empty for a hidden title no longer installed).
inline Record app_entry(std::string_view line) {
    const size_t first = line.find(' ');
    const std::string_view rest = first == std::string_view::npos ? std::string_view() : line.substr(first + 1);
    const size_t second = rest.find(' ');
    return {{"id", std::string(line.substr(0, first))},
            {"state", std::string(rest.substr(0, second))},
            {"name", second == std::string_view::npos ? std::string() : std::string(rest.substr(second + 1))}};
}

// The last `limit` bytes of a log, starting at a line.
inline std::string tail(std::string_view text, size_t limit) {
    if (text.size() <= limit) return std::string(text);
    std::string_view cut = text.substr(text.size() - limit);
    const size_t nl = cut.find('\n');
    if (nl != std::string_view::npos) cut.remove_prefix(nl + 1);
    return std::string(cut);
}

}  // namespace prospero::web
