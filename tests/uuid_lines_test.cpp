// SPDX-License-Identifier: GPL-3.0-only
#include "control/uuid_lines.hpp"
#include <cassert>
#include <unistd.h>
using namespace prospero;

int main() {
    const auto dir = std::filesystem::temp_directory_path() / ("uuid_lines_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    const auto file = dir / "web_trusted.txt";
    assert(read_uuid_lines(file).empty());  // no file yet
    assert(change_uuid_lines(file, {"a", "b"}, true));
    assert(change_uuid_lines(file, {"b", "c"}, true));  // b once
    assert((read_uuid_lines(file) == std::vector<std::string>{"a", "b", "c"}));
    assert(change_uuid_lines(file, {"b", "x"}, false));  // UNPAIR of b; x was never there
    assert((read_uuid_lines(file) == std::vector<std::string>{"a", "c"}));
    assert(change_uuid_lines(file, {"x"}, false));  // nothing to change
    assert(!change_uuid_lines(dir / "no such folder" / "f.txt", {"a"}, true));
    std::filesystem::remove_all(dir);
    return 0;
}
