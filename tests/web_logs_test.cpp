// SPDX-License-Identifier: GPL-3.0-only
#include "web/web_logs.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <string>
using namespace prospero::web;
using Query = std::multimap<std::string, std::string>;

int main() {
    // As before the parameter: no query is sunshine.log, raw.
    auto r = log_request(Query{});
    assert(r && !r->diag && !r->masked);
    r = log_request(Query{{"file", "sunshine"}});
    assert(r && !r->diag && !r->masked);
    r = log_request(Query{{"file", "diag"}});
    assert(r && r->diag && !r->masked);
    r = log_request(Query{{"file", "diag"}, {"masked", "1"}});
    assert(r && r->diag && r->masked);
    r = log_request(Query{{"file", "sunshine"}, {"masked", "0"}});
    assert(r && !r->masked);
    r = log_request(Query{{"masked", "1"}});
    assert(r && !r->diag && r->masked);

    // Anything that isn't one of the two names is refused, not defaulted: no path, no traversal, no other file of the folder.
    for (const char *file : {"", "Diag", "sunshine.log", "host_diag.log", "../sunshine.log", "/etc/passwd", "diag/../x", "diag\n",
                             "sunshine,diag", "%2e%2e", "report"})
        assert(!log_request(Query{{"file", file}}));
    assert(!log_request(Query{{"file", std::string("diag\0x", 6)}}));
    // masked is exactly 0 or 1.
    for (const char *masked : {"", "true", "yes", "2", "01", "1 ", "on"})
        assert(!log_request(Query{{"file", "diag"}, {"masked", masked}}));
    std::cout << "PASS web_logs_test\n";
}
