// SPDX-License-Identifier: GPL-3.0-only
// The UPnP client: finds the router, maps the stream ports and keeps their leases, and removes what it mapped when the
// host stops (src/net/upnp.hpp has the decisions). Runs only with `upnp` on in sunshine.conf.
#pragma once
#include "net/upnp.hpp"
#include <stop_token>
#include <vector>

namespace prospero::upnp {

// Until stop: one pass now, then one every 2 minutes, as Sunshine does, since routers lose mappings when they restart
// or get a new address. Returns after removing the mappings it added.
void run(std::stop_token stop, std::vector<Mapping> mappings);

// The last pass's result, for the web page. State::off while run() is not running.
Status status();

// Removes the mappings an earlier run added without a lease (upnp_permanent.txt, upnp.hpp) and couldn't remove, after a crash
// or a power cut, except those on the ports in `mappings`, which a run renews. run() calls it first; with upnp off the host
// calls it with no mappings, so turning UPnP off removes them too. Only mappings still the host's go; a router that doesn't
// answer keeps its notes for the next start. Returns at once without the file.
void remove_leftovers(std::stop_token stop, const std::vector<Mapping> &mappings);

}  // namespace prospero::upnp
