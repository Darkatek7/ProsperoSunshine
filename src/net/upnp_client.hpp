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

// Checks all durable recovery records, also with UPnP disabled. Only full current identity matches are removed;
// ambiguous and legacy records remain for later inspection. The mappings argument is kept for existing callers;
// matching a port alone no longer excludes a record from recovery.
void remove_leftovers(std::stop_token stop, const std::vector<Mapping> &mappings);

}  // namespace prospero::upnp
