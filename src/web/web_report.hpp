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

// A log file's text through a fresh scrubber with the names the report hides (the console's, the paired devices', the streaming
// client's, those waiting to pair), for the page's masked download: the same tags as in the report, from 1 again.
std::string mask_log(ControlServer &control, const std::string &text);

}  // namespace prospero::web
