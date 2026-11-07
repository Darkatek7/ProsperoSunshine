// SPDX-License-Identifier: GPL-3.0-only
#include "apps/app_access.hpp"
#include "control/uuid_lines.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
using namespace prospero;

int main() {
    static_assert(title_id_valid("PPSA01234") && title_id_valid("CUSA01234") && title_id_valid("FAKE77031"));
    static_assert(!title_id_valid("") && !title_id_valid("PPS") && !title_id_valid("ppsa01234") && !title_id_valid("PPSA 0123"));
    static_assert(!title_id_valid("PPSA01234\n") && !title_id_valid("../../etc") && !title_id_valid("PPSA012345678901234"));

    // Trust goes with a free choice of profile only.
    static_assert(trusted_with_apps(true, false, false));   // trusted, profiles not set (plays as any)
    static_assert(trusted_with_apps(true, true, true));     // trusted, set to free
    static_assert(!trusted_with_apps(true, true, false));   // trusted once, limited to some profiles or watching now
    static_assert(!trusted_with_apps(false, false, false) && !trusted_with_apps(false, true, true));

    // PS5 Screen always; a hidden app only for a trusted device; everything else for everyone.
    static_assert(app_allowed(true, true, false));
    static_assert(app_allowed(false, false, false));
    static_assert(!app_allowed(false, true, false));
    static_assert(app_allowed(false, true, true));

    // The list file: title IDs added and taken out, none twice, the rest kept.
    const auto file = std::filesystem::temp_directory_path() / "app_access_test_hidden_apps.txt";
    std::filesystem::remove(file);
    assert(change_uuid_lines(file, {"PPSA99000"}, true) && change_uuid_lines(file, {"FAKE77031", "PPSA99000"}, true));
    {
        std::scoped_lock lock(uuid_lines_mutex());
        const auto ids = read_uuid_lines(file);
        assert(ids.size() == 2 && ids[0] == "PPSA99000" && ids[1] == "FAKE77031");
    }
    assert(change_uuid_lines(file, {"PPSA99000"}, false));
    {
        std::scoped_lock lock(uuid_lines_mutex());
        const auto ids = read_uuid_lines(file);
        assert(ids.size() == 1 && ids[0] == "FAKE77031");
    }
    std::filesystem::remove(file);

    std::cout << "PASS: app access (title IDs, trust only with a free choice of profile, PS5 Screen always, the hidden list)\n";
}
