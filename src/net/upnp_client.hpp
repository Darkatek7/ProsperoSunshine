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

// With UPnP off: removes what an earlier run noted in upnp_permanent.txt and left on the router. Only full current identity
// matches are removed; a replaced or uncertain rule keeps its record for the next start.
void remove_leftovers(std::stop_token stop);

}  // namespace prospero::upnp
