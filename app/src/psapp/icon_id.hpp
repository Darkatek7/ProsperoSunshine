// SPDX-License-Identifier: GPL-3.0-only
// The app's icons by name, apart from their drawing (icons.hpp), so that sunshine.conf's options (conf.hpp) can name
// theirs without the graphics, for the PC tests and the host's web page.
#pragma once

namespace psapp::icons
{

enum class Icon
{
    home,
    sliders,
    power,
    restart,
    boot,     // a rocket-like arrow out of a tray: started with the console
    link,     // pairing
    monitor,  // a client device
    tag,      // the host name
    log,      // log level
    timer,    // ping timeout
    lock,     // encryption
    globe,    // encryption over the internet
    plug,     // port
    check,
    save,
    undo,
    wifi,     // the console's address
    trash,    // forget the web page's login
};

} // namespace psapp::icons
