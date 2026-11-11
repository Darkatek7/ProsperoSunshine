// SPDX-License-Identifier: GPL-3.0-only
// The control socket's guard against a browser (src/control/http_guard.hpp): a browser's request line must always be caught
// and a command line the socket answers to must (nearly) never be, whatever the arguments.
#include "fuzz_common.hpp"
#include "control/http_guard.hpp"

#include <algorithm>
#include <string_view>

using prospero::control::http_request_line;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider in(data, size);
    // Any line at all.
    const bool verdict = http_request_line(in.ConsumeRandomLengthString(200));
    (void)verdict;

    // What a browser sends first: an upper-case method, one target without a space, and a version.
    std::string method;
    for (size_t i = 0, n = in.ConsumeIntegralInRange<size_t>(1, 8); i < n; ++i) method += static_cast<char>('A' + in.ConsumeIntegralInRange<int>(0, 25));
    std::string target;
    for (const char c : in.ConsumeRandomLengthString(100)) target += c == ' ' ? '%' : c;
    // After the version a browser sends nothing but the line's end, which the socket takes off.
    std::string tail = in.ConsumeRandomLengthString(10);
    tail.erase(std::remove(tail.begin(), tail.end(), ' '), tail.end());
    const std::string request = method + ' ' + target + " HTTP/" + static_cast<char>('0' + in.ConsumeIntegralInRange<int>(0, 9)) + tail;
    FUZZ_CHECK(http_request_line(request));

    // The socket's own commands with any arguments that don't themselves end in "HTTP/<digit>": none is taken for a request.
    static constexpr std::string_view commands[] = {"STATUS", "PAIRINGS", "PIN", "DEVICES", "STREAMS", "ENDSTREAM", "RENAME", "UNPAIR",
                                                    "USERS", "PROFILES", "SETPROFILES", "APPS", "SETAPPS", "REPORT", "AUTOLOAD",
                                                    "RELISTEN", "PROBE", "FILE", "INSTALL", "CANCELPIN", "WEBLOGIN", "QUIT"};
    const std::string_view command = commands[in.ConsumeIntegralInRange<size_t>(0, std::size(commands) - 1)];
    std::string line(command);
    for (size_t i = 0, n = in.ConsumeIntegralInRange<size_t>(0, 3); i < n; ++i) line += ' ' + in.ConsumeRandomLengthString(30);
    const bool ends_like_version = line.size() >= 6 && line.rfind(" HTTP/") == line.size() - 7 && line.back() >= '0' && line.back() <= '9';
    if (!ends_like_version && line.find(" HTTP/") == std::string::npos) FUZZ_CHECK(!http_request_line(line));
    return 0;
}
