// SPDX-License-Identifier: GPL-3.0-only
#include "control/device_profiles.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace prospero;

int main() {
    // The text forms.
    assert(parse_profiles("free")->free);
    assert(parse_profiles("none")->none());
    const auto two = parse_profiles("1B1D9382,1b1d9381,guest");
    assert(two && !two->free && two->guest);
    assert((two->users == std::vector<uint32_t>{0x1b1d9381, 0x1b1d9382}));  // sorted
    assert(format_profiles(*two) == "1b1d9381,1b1d9382,guest");
    assert(format_profiles(*parse_profiles("guest")) == "guest");
    assert(format_profiles(*parse_profiles("1")) == "00000001");
    assert(format_profiles(ProfileSet{}) == "none");
    ProfileSet any;
    any.free = true;
    assert(format_profiles(any) == "free");
    for (const char *bad : {"", ",", "guest,guest", "1b1d9381,1b1d9381", "0", "ffffffff", "123456789", "xyz", "free,guest",
                            "1b1d9381,", ",guest", "none,guest", "1b1d9381 guest"})
        assert(!parse_profiles(bad));
    std::string many;
    for (int i = 1; i <= 17; ++i) many += (many.empty() ? "" : ",") + format_user_id(static_cast<uint32_t>(i));
    assert(!parse_profiles(many));                                 // more than the console's 16 profiles
    assert(parse_profiles(many.substr(0, many.rfind(','))));       // 16 is fine
    assert(format_user_id(0x1b1d9381) == "1b1d9381");

    // The file.
    const auto dir = std::filesystem::temp_directory_path() / ("device_profiles_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    const auto file = dir / "device_profiles.txt";
    {
        DeviceProfiles profiles(file);  // no file yet
        assert(!profiles.get("a"));
        profiles.set("a", parse_profiles("1b1d9381"));
        profiles.set("b", parse_profiles("free"));
        profiles.set("c", parse_profiles("none"));
        profiles.set("", parse_profiles("free"));
        profiles.set("a", parse_profiles("1b1d9381,guest"));
        assert(profiles.get("a")->guest);
        assert(profiles.all().size() == 3);
    }
    {
        DeviceProfiles profiles(file);  // survives a restart
        assert(format_profiles(*profiles.get("a")) == "1b1d9381,guest");
        assert(profiles.get("b")->free);
        assert(profiles.get("c")->none());  // 0 profiles is a setting, not "not set"
        profiles.forget("b");
        profiles.set("c", std::nullopt);
        profiles.forget("never there");
        assert(!profiles.get("b") && !profiles.get("c"));
    }
    {
        // A line damaged by hand: its device only watches, never a free choice; the others stay. One without profiles
        // names no setting.
        std::ofstream(file, std::ios::app) << "d not,a,list\ne\nf 1b1d9383 extra\n";
        DeviceProfiles profiles(file);
        assert(profiles.readable());
        assert(format_profiles(*profiles.get("a")) == "1b1d9381,guest");
        assert(profiles.get("d")->none() && profiles.get("f")->none() && !profiles.get("e"));
        assert(profiles.all().size() == 3);
    }
    {
        // A file there that can't be read (a folder in its place here): every device watches, and nothing is written.
        const auto unreadable = dir / "unreadable_profiles.txt";
        std::filesystem::create_directories(unreadable);
        DeviceProfiles profiles(unreadable);
        assert(!profiles.readable());
        assert(profiles.get("a") && profiles.get("a")->none() && !profiles.get("a")->free);
        assert(!profiles.get(""));
        assert(!profiles.set("a", parse_profiles("free")) && profiles.get("a")->none());
        assert(std::filesystem::is_directory(unreadable));
    }
    // The profile a controller switches to.
    const auto three = *parse_profiles("a,b,c,guest");
    const auto none_taken = [](uint32_t) { return false; };
    assert(next_seat(three, 0xa, none_taken) == 0xb);
    assert(next_seat(three, 0xc, none_taken) == 0xa);  // round to the start
    assert(next_seat(three, 0xa, [](uint32_t u) { return u == 0xb; }) == 0xc);
    assert(next_seat(three, 0xa, [](uint32_t u) { return u != 0xa; }) == 0);
    assert(next_seat(three, 0x99, none_taken) == 0xa);  // not on the list (any more): the first one
    assert(next_seat(*parse_profiles("a"), 0xa, none_taken) == 0);
    assert(next_seat(*parse_profiles("guest"), 0, none_taken) == 0);
    assert(next_seat(ProfileSet{}, 0, none_taken) == 0);

    // What each controller switched to.
    const auto seats_file = dir / "device_seats.txt";
    {
        DeviceSeats seats(seats_file);
        assert(seats.get("a", 0) == 0);
        seats.remember("a", 0, 0x1b1d9387);
        seats.remember("a", 1, 0x1b1d9384);
        seats.remember("b", 0, 0x1b1d9384);
        seats.remember("a", 16, 0x1b1d9384);  // no such controller number
        seats.remember("", 0, 0x1b1d9384);
        seats.remember("a", 2, 0);
        seats.remember("a", 0, 0x1b1d9385);   // the last switch counts
    }
    {
        DeviceSeats seats(seats_file);  // survives a restart
        assert(seats.get("a", 0) == 0x1b1d9385 && seats.get("a", 1) == 0x1b1d9384 && seats.get("b", 0) == 0x1b1d9384);
        assert(seats.get("a", 2) == 0 && seats.get("a", 16) == 0);
        seats.forget("a");  // unpaired
        assert(seats.get("a", 0) == 0 && seats.get("a", 1) == 0 && seats.get("b", 0) == 0x1b1d9384);
    }
    {
        std::ofstream(seats_file, std::ios::app) << "c x 1b1d9384\nd 0 zz\ne 0\nf 17 1b1d9384\n";
        DeviceSeats seats(seats_file);
        assert(seats.get("b", 0) == 0x1b1d9384 && seats.get("d", 0) == 0 && seats.get("f", 17) == 0);
    }
    {
        // Two of them at once (the platform's and UNPAIR's): a change made through one isn't undone by the other.
        DeviceSeats platform(seats_file), unpair(seats_file);
        unpair.forget("b");
        platform.remember("g", 0, 0x1b1d9387);
        DeviceSeats after(seats_file);
        assert(after.get("b", 0) == 0 && after.get("g", 0) == 0x1b1d9387);
    }
    {
        // A file that can't be written: set says so, and nothing changed.
        DeviceProfiles profiles(dir / "no such folder" / "device_profiles.txt");
        assert(!profiles.set("a", parse_profiles("1b1d9381")));
        assert(!profiles.get("a"));
        DeviceProfiles fine(file);
        assert(fine.set("z", parse_profiles("1b1d9381")) && fine.set("z", parse_profiles("1b1d9381")));
        assert(fine.forget("z") && fine.forget("z"));
    }
    std::filesystem::remove_all(dir);
    std::cout << "device_profiles_test: ok\n";
}
