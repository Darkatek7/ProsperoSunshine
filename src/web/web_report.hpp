// SPDX-License-Identifier: GPL-3.0-only
// The diagnostics report for the web page: what the app's report says (Host::build_report in app/src/psapp/host.cpp), built
// inside the host from its own view. Same sections and the same scrubbing (app/src/psapp/diag.hpp), so it fits a public issue:
// no addresses, device names, ids or game titles.
#pragma once
#include <string>

namespace prospero {
class ControlServer;
}

namespace prospero::web {

// Asks `control` for the devices and the stream (DEVICES, STATUS, PAIRINGS) and reads sunshine.conf, host_diag.log and
// sunshine.log. Takes a moment: it also tries a few local ports.
std::string build_report(ControlServer &control);

}  // namespace prospero::web
