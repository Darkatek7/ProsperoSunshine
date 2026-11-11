// SPDX-License-Identifier: GPL-3.0-only
// The first bytes of a connection to the web page's port, before any TLS and any login (src/web/web_plain.hpp): whatever a
// client sends in its plain request ends up in a redirect the host writes, so nothing of it may reach the answer unchecked.
#include "fuzz_common.hpp"
#include "web/web_plain.hpp"

#include <string_view>

using namespace prospero::web;

namespace {

bool host_char(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-' || c == ':' || c == '[' ||
           c == ']';
}

size_t count(std::string_view text, std::string_view needle) {
    size_t n = 0;
    for (size_t at = text.find(needle); at != std::string_view::npos; at = text.find(needle, at + needle.size())) ++n;
    return n;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider in(data, size);
    const uint16_t port = in.ConsumeIntegral<uint16_t>();
    // Half the inputs get a Host header of their own so the redirect branch is reached often.
    const bool framed = in.ConsumeBool();
    const std::string value = in.ConsumeRandomLengthString(300);
    std::string head = in.ConsumeRemainingBytesAsString();
    if (framed) head = "GET / HTTP/1.1\r\nHost:" + value + "\r\nAccept: */*\r\n\r\n" + head;

    const std::string host = plain_host(host_header(head));
    for (const char c : host) FUZZ_CHECK(host_char(c));
    FUZZ_CHECK(host.size() <= 255);
    {
        const std::string_view header = host_header(head);
        if (!header.empty()) FUZZ_CHECK(header.data() >= head.data() && header.data() + header.size() <= head.data() + head.size());
    }

    const std::string answer = plain_http_answer(head, port);
    const size_t split = answer.find("\r\n\r\n");
    FUZZ_CHECK(split != std::string::npos);
    const std::string_view heads = std::string_view(answer).substr(0, split), body = std::string_view(answer).substr(split + 4);
    // Status line, optional Location, then exactly four fixed headers: nothing the client sent starts a header of its own.
    FUZZ_CHECK(host.empty() ? count(heads, "\r\n") == 4 : count(heads, "\r\n") == 5);
    FUZZ_CHECK(heads.starts_with(host.empty() ? "HTTP/1.1 400 " : "HTTP/1.1 302 "));
    FUZZ_CHECK(count(heads, "\n") == count(heads, "\r\n") && count(heads, "\r") == count(heads, "\r\n"));
    FUZZ_CHECK(heads.find("Content-Length: " + std::to_string(body.size())) != std::string_view::npos);
    if (!host.empty()) {
        const std::string url = "https://" + host + ":" + std::to_string(port) + "/";
        FUZZ_CHECK(heads.find("\r\nLocation: " + url + "\r\n") != std::string_view::npos);
        FUZZ_CHECK(body.find("<a href=\"" + url + "\">" + url + "</a>") != std::string_view::npos);
    }
    // The body's markup is the fixed text: no tag, quote or entity from the client.
    FUZZ_CHECK(count(body, "<") == (host.empty() ? 8 : 10));
    FUZZ_CHECK(count(body, "\"") == (host.empty() ? 2 : 4));
    FUZZ_CHECK(count(body, "&") == (host.empty() ? 2 : 0));
    return 0;
}
