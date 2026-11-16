// SPDX-License-Identifier: GPL-3.0-only
// Test-only replacement for the external interface.
#pragma once
#include <sstream>
#define BOOST_LOG(level) std::ostringstream()
