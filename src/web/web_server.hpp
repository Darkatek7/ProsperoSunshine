// SPDX-License-Identifier: GPL-3.0-only
// The host's web page (GitHub issue 21): HTTPS on Sunshine's web UI port (base port + 1, 47990 by default), for devices
// in the LAN and on Tailscale. It runs only while a login is set in the app (WEBLOGIN), and asks the host through the
// same commands as the app (control_server.hpp). The page itself, plain HTML and JavaScript, is embedded in the ELF.
#pragma once
#include "control/control_server.hpp"
#include "src/thread_safe.h"
#include <memory>
#include <stop_token>

namespace prospero::web {

class WebServer {
public:
    WebServer(ControlServer &control, std::shared_ptr<safe::event_t<bool>> shutdown);
    // Serves while a login exists and until `stop`; with no login it waits for one.
    void run(std::stop_token stop);

private:
    ControlServer &control_;
    std::shared_ptr<safe::event_t<bool>> shutdown_;
};

}  // namespace prospero::web
