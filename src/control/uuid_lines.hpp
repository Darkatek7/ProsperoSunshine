// SPDX-License-Identifier: GPL-3.0-only
// Files of one device uuid a line in appdata: web_trusted.txt (devices the operator trusts to play as any profile) and
// web_risk_ack.txt (risky devices the web page's notice no longer names). The web page changes them and UNPAIR takes a
// device out of them, from different threads: every change reads, changes and writes the file under one lock of the
// process, and the file is replaced whole, so that no change undoes another.
#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace prospero {

inline std::mutex &uuid_lines_mutex() {
    static std::mutex mutex;
    return mutex;
}

// The uuids in the file, in order; none if it isn't there. Caller holds uuid_lines_mutex().
inline std::vector<std::string> read_uuid_lines(const std::filesystem::path &file) {
    std::vector<std::string> uuids;
    std::ifstream in(file);
    for (std::string line; std::getline(in, line);)
        if (!line.empty() && line.size() <= 64 && line.find_first_of(" \t\r") == std::string::npos) uuids.push_back(line);
    return uuids;
}

// Replaces the file with these uuids. Caller holds uuid_lines_mutex().
inline bool write_uuid_lines(const std::filesystem::path &file, const std::vector<std::string> &uuids) {
    auto temporary = file;
    temporary += ".new";
    {
        std::ofstream out(temporary, std::ios::trunc);
        for (const auto &uuid : uuids) out << uuid << '\n';
        if (!out.flush()) return false;
    }
    std::error_code ec;
    std::filesystem::rename(temporary, file, ec);
    return !ec;
}

// Adds the uuids to the file, or takes them out. False when it couldn't be written.
inline bool change_uuid_lines(const std::filesystem::path &file, const std::vector<std::string> &uuids, bool add) {
    std::scoped_lock lock(uuid_lines_mutex());
    auto lines = read_uuid_lines(file);
    const auto before = lines;
    for (const auto &uuid : uuids) {
        const auto it = std::find(lines.begin(), lines.end(), uuid);
        if (add && it == lines.end()) lines.push_back(uuid);
        if (!add && it != lines.end()) lines.erase(it);
    }
    return lines == before || write_uuid_lines(file, lines);
}

}  // namespace prospero
