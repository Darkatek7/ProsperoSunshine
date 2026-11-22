// SPDX-License-Identifier: GPL-3.0-only
#include "control/device_follow_size.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>
using namespace prospero;

namespace {
std::string read_all(const std::filesystem::path &file) {
    std::ifstream in(file);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}
}

int main() {
    // The text forms.
    assert(parse_follow_size("on") == std::optional<bool>(true));
    assert(parse_follow_size("off") == std::optional<bool>(false));
    for (const char *bad : {"", "On", "1", "yes", "on ", "unset"}) assert(!parse_follow_size(bad));
    assert(std::string(format_follow_size(true)) == "on" && std::string(format_follow_size(false)) == "off");

    const auto dir = std::filesystem::temp_directory_path() / ("device_follow_size_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    const auto file = dir / "device_follow_size.txt";
    {
        // No file yet: every device uses the default, whichever it is.
        DeviceFollowSize none(file);
        assert(!none.get("AAA") && !none.follows("AAA", false) && none.follows("AAA", true) && none.all().empty());
        assert(!std::filesystem::exists(file));
        // Unset for a device without a line changes nothing and writes nothing.
        assert(none.set("AAA", std::nullopt) && !std::filesystem::exists(file));
        assert(!none.set("", true));
    }
    {
        DeviceFollowSize follow(file);
        assert(follow.set("BBB", true) && follow.set("AAA", false));
        assert(read_all(file) == "AAA off\nBBB on\n");  // one "<uuid> on|off" a line, sorted
        assert(!std::filesystem::exists(dir / "device_follow_size.txt.new"));
        // Its own setting beats the default either way.
        assert(follow.follows("BBB", false) && !follow.follows("AAA", true) && follow.follows("CCC", true));
        assert(follow.set("BBB", true));  // the same again: nothing to write
    }
    {
        // Read back, then a device goes back to the default and another is forgotten (UNPAIR).
        DeviceFollowSize again(file);
        assert(again.get("BBB") == std::optional<bool>(true) && again.get("AAA") == std::optional<bool>(false));
        assert(again.set("BBB", std::nullopt) && read_all(file) == "AAA off\n");
        assert(again.forget("AAA") && read_all(file).empty() && again.all().empty());
    }
    {
        // A line that doesn't read gives that device the default; the others keep theirs.
        std::ofstream(file) << "AAA on\nBBB maybe\nCCC off extra\nDDD\n\nEEE off\n";
        DeviceFollowSize damaged(file);
        assert(damaged.get("AAA") == std::optional<bool>(true) && damaged.get("EEE") == std::optional<bool>(false));
        assert(!damaged.get("BBB") && !damaged.get("CCC") && !damaged.get("DDD") && damaged.all().size() == 2);
    }
    {
        // A file that can't be written: set() says so and changes nothing.
        DeviceFollowSize blocked(dir / "missing" / "device_follow_size.txt");
        assert(!blocked.set("AAA", true) && !blocked.get("AAA"));
    }
    std::filesystem::remove_all(dir);

    // Every stream on a channel follows the output's size, as going back up needs.
    assert(!channel_follows_size({}));
    assert(channel_follows_size({{true, 2160}}));
    assert(channel_follows_size({{true, 2160}, {true, 1080}}));
    assert(!channel_follows_size({{true, 2160}, {false, 2160}}));
    assert(!channel_follows_size({{false, 2160}}));
    // Back to the size it delivered before: every stream still follows and asked for at least that much. A device that
    // joined at 1080p set its decoder up for that, and one that no longer follows keeps what it gets now.
    assert(channel_may_return_to({{true, 2160}}, 2160));
    assert(channel_may_return_to({{true, 2160}, {true, 1600}}, 1440));
    assert(!channel_may_return_to({{true, 2160}, {true, 1080}}, 2160));
    assert(!channel_may_return_to({{true, 2160}, {false, 2160}}, 2160));
    assert(!channel_may_return_to({}, 2160));
    // A smaller picture ends the joined streams whose devices don't follow; the ones that follow go on. A larger one or the
    // same size ends none (going back up needs every stream to follow, above).
    using ended = std::vector<std::size_t>;
    assert(streams_ended_by_resize({{true, 2160}, {false, 2160}, {true, 1440}, {false, 1080}}, 2160, 1080) == ended({1, 3}));
    assert(streams_ended_by_resize({{true, 2160}, {true, 2160}}, 2160, 1080).empty());
    assert(streams_ended_by_resize({{false, 1440}}, 1440, 1080) == ended({0}));
    assert(streams_ended_by_resize({{false, 2160}}, 1080, 2160).empty());
    assert(streams_ended_by_resize({{false, 2160}}, 2160, 2160).empty());
    assert(streams_ended_by_resize({}, 2160, 1080).empty());
    std::cout << "PASS device_follow_size_test\n";
}
