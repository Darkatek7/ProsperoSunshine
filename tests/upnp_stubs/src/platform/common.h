// SPDX-License-Identifier: GPL-3.0-only
// Test-only replacement for the external interface.
#pragma once
#include <filesystem>
namespace platf { inline std::filesystem::path test_appdata; inline auto appdata() { return test_appdata; } }
