// SPDX-License-Identifier: GPL-3.0-only
// The query of /api/logs. The page never names a path: `file` picks one of two logs the host knows, and nothing else is
// accepted (a value the host doesn't know is an error, not the default). `masked=1` asks for the file after the same
// masking the diagnostic report gets. Portable (tests/web_logs_test.cpp).
#pragma once
#include <optional>
#include <string>

namespace prospero::web {

struct LogRequest {
    bool diag = false;    // host_diag.log; else sunshine.log
    bool masked = false;  // through a Scrubber, as the report
};

// `query` is any map of the query string's keys to values with find(): the web server's, or a std::multimap in the test.
// A missing `file` means sunshine.log and a missing `masked` the raw file, as before the parameter existed.
template <class Query>
std::optional<LogRequest> log_request(const Query &query) {
    LogRequest request;
    if (const auto file = query.find("file"); file != query.end()) {
        if (file->second == "diag") request.diag = true;
        else if (file->second != "sunshine") return std::nullopt;
    }
    if (const auto masked = query.find("masked"); masked != query.end()) {
        if (masked->second == "1") request.masked = true;
        else if (masked->second != "0") return std::nullopt;
    }
    return request;
}

}  // namespace prospero::web
