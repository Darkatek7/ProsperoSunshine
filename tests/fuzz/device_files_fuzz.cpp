// SPDX-License-Identifier: GPL-3.0-only
// The small files the host keeps in its data folder and reads at start, which a person or an older version may have left in
// any state: device_profiles.txt, device_seats.txt, device_modes.txt, menu_profiles.txt, web_trusted.txt. Whatever is in them,
// a device never ends with more profiles than its line grants, and what the host writes reads back as it was.
#include "fuzz_common.hpp"
#include "control/device_modes.hpp"
#include "control/device_profiles.hpp"
#include "control/menu_access.hpp"
#include "control/uuid_lines.hpp"

#include <fstream>
#include <map>
#include <sstream>

using namespace prospero;
namespace fs = std::filesystem;

namespace {

void write_file(const fs::path &path, const std::string &text) { std::ofstream(path, std::ios::binary | std::ios::trunc) << text; }

bool plain_word(const std::string &word) { return !word.empty() && word.find_first_of(" \t\r\n\v\f") == std::string::npos; }

uint32_t user_id(FuzzedDataProvider &in) {
    for (;;) {
        const auto id = in.ConsumeIntegral<uint32_t>();
        if (id != 0 && id != 0xffffffffu) return id;
        if (in.remaining_bytes() == 0) return 1;
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider in(data, size);
    const fs::path dir = fuzz::scratch();

    // A profile list from a line or the web page: either refused or read, and a read one writes out and reads back the same.
    const std::string list = in.ConsumeRandomLengthString(120);
    if (const auto set = parse_profiles(list)) {
        FUZZ_CHECK(set->users.size() <= max_profiles);
        FUZZ_CHECK(std::is_sorted(set->users.begin(), set->users.end()));
        FUZZ_CHECK(std::adjacent_find(set->users.begin(), set->users.end()) == set->users.end());
        FUZZ_CHECK(!set->free || (set->users.empty() && !set->guest));
        const std::string text = format_profiles(*set);
        FUZZ_CHECK(text.find_first_of(" \t\r\n") == std::string::npos);
        const auto again = parse_profiles(text);
        FUZZ_CHECK(again && *again == *set);
    }
    if (const auto menus = parse_menu_profiles(list)) {
        FUZZ_CHECK(menus->users.size() <= max_profiles && std::is_sorted(menus->users.begin(), menus->users.end()));
        const auto again = parse_menu_profiles(format_menu_profiles(*menus));
        FUZZ_CHECK(again && *again == *menus);
    }
    if (const auto id = parse_user_id(list)) FUZZ_CHECK(*id != 0 && *id != 0xffffffffu && parse_user_id(format_user_id(*id)) == id);

    // A set built the way the web page builds one reads back.
    {
        ProfileSet set;
        set.free = in.ConsumeBool();
        if (!set.free) {
            for (size_t i = 0, n = in.ConsumeIntegralInRange<size_t>(0, 20); i < n; ++i) {
                const uint32_t id = user_id(in);
                if (std::find(set.users.begin(), set.users.end(), id) == set.users.end()) set.users.push_back(id);
            }
            if (set.users.size() > max_profiles) set.users.resize(max_profiles);
            std::sort(set.users.begin(), set.users.end());
            set.guest = in.ConsumeBool();
        }
        const auto again = parse_profiles(format_profiles(set));
        FUZZ_CHECK(again && *again == set);

        // The next profile of a controller: one of the device's, not the current one, not taken, or 0.
        if (!set.users.empty()) {
            const uint32_t current = in.ConsumeBool() ? set.users[0] : user_id(in);
            const uint32_t blocked = in.ConsumeIntegral<uint32_t>();
            const uint32_t next = next_seat(set, current, [&](uint32_t user) { return (user ^ blocked) % 3 == 0; });
            if (next != 0) {
                FUZZ_CHECK(std::find(set.users.begin(), set.users.end(), next) != set.users.end());
                FUZZ_CHECK(next != current && (next ^ blocked) % 3 != 0);
            }
        }
        const auto menus = parse_menu_profiles(in.ConsumeBool() ? "none" : format_user_id(user_id(in)));
        (void)screen_offered(menus, set);
        (void)menu_access(menus, set, in.ConsumeIntegral<uint32_t>());
    }

    // device_profiles.txt as found on disk.
    const std::string profiles_text = in.ConsumeRandomLengthString(300);
    {
        const fs::path file = dir / "device_profiles.txt";
        write_file(file, profiles_text);
        DeviceProfiles profiles(file);
        FUZZ_CHECK(profiles.readable());
        for (const auto &[uuid, set] : profiles.all()) {
            FUZZ_CHECK(plain_word(uuid));
            FUZZ_CHECK(parse_profiles(format_profiles(set)) == std::optional<ProfileSet>(set));
        }
        // A line that does not read grants nothing; it never gives a device a free choice by itself. A uuid on two lines
        // keeps the last.
        std::map<std::string, std::optional<ProfileSet>> expected;
        std::istringstream lines(profiles_text);
        for (std::string line; std::getline(lines, line);) {
            std::istringstream fields(line);
            std::string uuid, text, extra;
            if (!(fields >> uuid >> text)) continue;
            expected[uuid] = (fields >> extra) ? std::nullopt : parse_profiles(text);
        }
        for (const auto &[uuid, read] : expected) {
            const auto got = profiles.get(uuid);
            FUZZ_CHECK(got.has_value());
            if (read) FUZZ_CHECK(*got == *read);
            else FUZZ_CHECK(!got->free && got->users.empty() && !got->guest);
        }
        FUZZ_CHECK(profiles.all().size() == expected.size());
        // Setting a device writes the file, a new reader sees the same table.
        const std::string uuid = "dev-" + std::to_string(size % 7);
        ProfileSet set;
        set.users = {user_id(in)};
        FUZZ_CHECK(profiles.set(uuid, set));
        const DeviceProfiles reread(file);
        FUZZ_CHECK(reread.all() == profiles.all());
        FUZZ_CHECK(reread.get(uuid) == std::optional<ProfileSet>(set));
        FUZZ_CHECK(profiles.forget(uuid) && !DeviceProfiles(file).get(uuid));
        // The same bytes as a directory entry that is not a file fail closed.
        fs::remove(file);
        fs::create_directory(file);
        DeviceProfiles broken(file);
        FUZZ_CHECK(!broken.readable() && !broken.set("a", ProfileSet{}) && broken.get("a")->none());
        fs::remove(file);
    }

    // device_seats.txt: lines of "<uuid> <controller> <id>".
    {
        const fs::path file = dir / "device_seats.txt";
        write_file(file, in.ConsumeRandomLengthString(200));
        DeviceSeats seats(file);
        const uint32_t user = user_id(in);
        const int controller = in.ConsumeIntegralInRange<int>(-1, 17);
        seats.remember("x", controller, user);
        if (controller < 0 || controller > 15) FUZZ_CHECK(seats.get("x", controller) == 0);
        if (controller >= 0 && controller <= 15) {
            FUZZ_CHECK(seats.get("x", controller) == user);
            FUZZ_CHECK(DeviceSeats(file).get("x", controller) == user);
            seats.forget("x");
            FUZZ_CHECK(DeviceSeats(file).get("x", controller) == 0);
        }
    }

    // device_modes.txt.
    {
        const fs::path file = dir / "device_modes.txt";
        write_file(file, in.ConsumeRandomLengthString(200));
        DeviceModes modes(file);
        const std::string mode = format_mode(in.ConsumeIntegral<int>(), in.ConsumeIntegral<int>(), in.ConsumeIntegral<int>());
        FUZZ_CHECK(mode.empty() || (plain_word(mode) && mode.find('x') != std::string::npos && mode.find('@') != std::string::npos));
        if (!mode.empty()) {
            modes.remember("u1", mode);
            FUZZ_CHECK(DeviceModes(file).mode("u1") == mode);
            modes.forget("u1");
            FUZZ_CHECK(DeviceModes(file).mode("u1").empty());
        }
    }

    // menu_profiles.txt as found, and as written.
    {
        const fs::path file = dir / "menu_profiles.txt";
        write_file(file, in.ConsumeRandomLengthString(200));
        const auto menus = read_menu_profiles(file);
        FUZZ_CHECK(menus.has_value());
        FUZZ_CHECK(menus->users.size() <= max_profiles && std::is_sorted(menus->users.begin(), menus->users.end()));
        for (const uint32_t id : menus->users) FUZZ_CHECK(id != 0 && id != 0xffffffffu);
        FUZZ_CHECK(write_menu_profiles(file, menus) && read_menu_profiles(file) == menus);
        FUZZ_CHECK(write_menu_profiles(file, std::nullopt) && !read_menu_profiles(file));
    }

    // web_trusted.txt: lines that are one word, at most 64 bytes.
    {
        const fs::path file = dir / "web_trusted.txt";
        write_file(file, in.ConsumeRandomLengthString(200));
        for (const auto &uuid : read_uuid_lines(file)) FUZZ_CHECK(!uuid.empty() && uuid.find_first_of(" \t\r\n") == std::string::npos && uuid.size() <= 64);
        const std::string uuid = "id-" + std::to_string(in.ConsumeIntegralInRange<int>(0, 9));
        FUZZ_CHECK(change_uuid_lines(file, {uuid}, true));
        const auto added = read_uuid_lines(file);
        const auto copies = std::count(added.begin(), added.end(), uuid);
        FUZZ_CHECK(copies >= 1);
        FUZZ_CHECK(change_uuid_lines(file, {uuid, uuid}, false));
        const auto removed = read_uuid_lines(file);
        FUZZ_CHECK(std::count(removed.begin(), removed.end(), uuid) == std::max<long>(0, copies - 2));
    }
    return 0;
}
