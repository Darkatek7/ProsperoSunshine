// SPDX-License-Identifier: GPL-3.0-only
// The firmware probe's report is meant to be posted: anything shaped like a MAC address, an IPv4 address or an IPv6 address is
// replaced before a line is written, by the masker every posted file uses (app/src/psapp/mask.hpp): <lan-1>, <public-1>,
// <mac-1> and so on, the same tag for the same address within one run of the probe. The probe reads no address itself; this
// is the safety net. Portable, tested in tests/probe_scrub_test.cpp.
#pragma once
#include "../app/src/psapp/mask.hpp"
#include <string>

namespace probe {

// The probe's own masker: one per process, so that one address has one tag in report.txt, detail.log and the kernel log.
inline psapp::mask::Masker &masker() {
    static psapp::mask::Masker instance(psapp::mask::Scope::all);
    return instance;
}

inline std::string scrub(const std::string &s) { return masker().apply(s); }

}  // namespace probe
