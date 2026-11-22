// SPDX-License-Identifier: GPL-3.0-only
// Whether a paired device follows the console's output size, set on the web page in Devices. When a game switches the output
// so that it no longer takes a running 1440p or 4K stream's size (a PS5 takes neither at 120 Hz), the stream of a device that
// follows it goes on at 1920x1080 instead of ending, and goes back to its size at a later reopen whose output takes it again
// (video_ps5.cpp). Each device for itself: a device that joined another's stream and doesn't follow ends when the picture
// gets smaller. Off by default: Moonlight for Windows with D3D11 broke on such a change (issue 39).
// A file of its own like device_profiles.txt, <appdata>/device_follow_size.txt, one "<uuid> on|off" a line. A device without
// a line, or with one that doesn't read, gets follow_output_size from sunshine.conf (off by default).
// Portable, tested in tests/device_follow_size_test.cpp.
#pragma once
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace prospero {

inline std::optional<bool> parse_follow_size(std::string_view text) {
    if (text == "on") return true;
    if (text == "off") return false;
    return std::nullopt;
}

inline const char *format_follow_size(bool on) { return on ? "on" : "off"; }

class DeviceFollowSize {
public:
    explicit DeviceFollowSize(std::filesystem::path file) : file_(std::move(file)) {
        std::ifstream in(file_);
        for (std::string line; std::getline(in, line);) {
            std::istringstream fields(line);
            std::string uuid, text, extra;
            if (!(fields >> uuid >> text) || (fields >> extra)) continue;
            if (const auto on = parse_follow_size(text)) set_[uuid] = *on;
        }
    }

    // nullopt: the device uses the default (its line goes). False when the file couldn't be written: then nothing changed.
    bool set(const std::string &uuid, std::optional<bool> on) {
        if (uuid.empty()) return false;
        std::scoped_lock lock(mutex_);
        const auto before = set_;
        const auto it = set_.find(uuid);
        if (!on) {
            if (it == set_.end()) return true;
            set_.erase(it);
        } else {
            if (it != set_.end() && it->second == *on) return true;
            set_[uuid] = *on;
        }
        if (save()) return true;
        set_ = before;
        return false;
    }

    bool forget(const std::string &uuid) { return set(uuid, std::nullopt); }

    // The device's own setting; nullopt without one.
    std::optional<bool> get(const std::string &uuid) const {
        std::scoped_lock lock(mutex_);
        const auto it = set_.find(uuid);
        if (it == set_.end()) return std::nullopt;
        return it->second;
    }

    bool follows(const std::string &uuid, bool by_default) const { return get(uuid).value_or(by_default); }

    std::map<std::string, bool> all() const {
        std::scoped_lock lock(mutex_);
        return set_;
    }

private:
    bool save() const {
        auto temporary = file_;
        temporary += ".new";
        {
            std::ofstream out(temporary, std::ios::trunc);
            for (const auto &[uuid, on] : set_) out << uuid << ' ' << format_follow_size(on) << '\n';
            if (!out.flush()) return false;
        }
        std::error_code ec;
        std::filesystem::rename(temporary, file_, ec);
        return !ec;
    }

    std::filesystem::path file_;
    mutable std::mutex mutex_;
    std::map<std::string, bool> set_;
};

// A stream on a capture channel, as a reopen sees it: whether its device follows the output's size, and the height it asked
// for, which its client set its decoder up for.
struct SizeFollower {
    bool follows = false;
    int asked_height = 0;
};

// Whether every stream on a channel follows the output's size, as a channel needs to go back up (channel_may_return_to).
// Going down needs less: a reopen whose 1440p or 4K the output refuses goes on at 1080p when the device of the stream that
// holds the channel follows the size, and the joined streams whose devices don't then end (streams_ended_by_resize).
inline bool channel_follows_size(const std::vector<SizeFollower> &streams) {
    return !streams.empty() && std::all_of(streams.begin(), streams.end(), [](const SizeFollower &s) { return s.follows; });
}

// After an open that changed a channel's picture size, which of the streams joined to it end (their indices): when it got
// smaller, each one whose device doesn't follow the output's size, since its client set its decoder up for the size it had
// (issue 39); the ones that follow go on. A larger picture ends none: a channel goes back up only when every stream on it
// may (channel_may_return_to).
inline std::vector<std::size_t> streams_ended_by_resize(const std::vector<SizeFollower> &joined, int height_before,
                                                        int height_now) {
    std::vector<std::size_t> ended;
    if (height_now >= height_before) return ended;
    for (std::size_t i = 0; i < joined.size(); i++)
        if (!joined[i].follows) ended.push_back(i);
    return ended;
}

// Whether a reopen of a channel that followed the output down from `height` tries that size again: every stream on it still
// follows, and none asked for less (a device that joined at 1080p set its decoder up for that). Otherwise it stays at what it
// delivers, as a stream that doesn't follow keeps its size.
inline bool channel_may_return_to(const std::vector<SizeFollower> &streams, int height) {
    return channel_follows_size(streams) &&
           std::all_of(streams.begin(), streams.end(), [height](const SizeFollower &s) { return s.asked_height >= height; });
}

}  // namespace prospero
