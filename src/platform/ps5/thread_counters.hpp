// SPDX-License-Identifier: GPL-3.0-only
// Host only: this process's threads and their counters (thread_counters.cpp), for diag/thread_load.hpp.
#pragma once
#include "diag/thread_load.hpp"

namespace prospero::load {
bool read_snapshot(Snapshot &out);
}
