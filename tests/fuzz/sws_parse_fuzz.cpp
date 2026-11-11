// SPDX-License-Identifier: GPL-3.0-only
// Simple-Web-Server's parsers that the host's servers run on bytes from anybody before a route or a login: the request line and
// header reader, the query string and percent decoding, the header attribute reader. All are noexcept in the library, so a
// throw is std::terminate. Needs the patched sources (scripts/dep_headers.sh; scripts/fuzz.sh fetches them).
#include "fuzz_common.hpp"
#include "server_http.hpp"

#include <algorithm>
#include <sstream>

using namespace SimpleWeb;

namespace {

// What a request's first line has to look like for RequestMessage::parse to have taken it apart the way it says: the pieces
// put back together are the line's start.
void check_request(const std::string &bytes) {
    std::istringstream stream(bytes);
    std::string method, path, query, version;
    CaseInsensitiveMultimap header;
    const bool ok = RequestMessage::parse(stream, method, path, query, version, header);
    if (!ok) return;
    const std::string line = bytes.substr(0, bytes.find('\n'));
    FUZZ_CHECK(method.find(' ') == std::string::npos);
    FUZZ_CHECK(path.find(' ') == std::string::npos);
    FUZZ_CHECK(query.find(' ') == std::string::npos);
    // A '?' with nothing after it before the space leaves the query empty and is not in the path either.
    const std::string with_query = method + ' ' + path + '?' + query + " HTTP";
    const std::string without = method + ' ' + path + " HTTP";
    FUZZ_CHECK(line.compare(0, with_query.size(), with_query) == 0 || (query.empty() && line.compare(0, without.size(), without) == 0));
    for (const auto &[key, value] : header) {
        FUZZ_CHECK(key.find(':') == std::string::npos);
        FUZZ_CHECK(value.find('\n') == std::string::npos);
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider in(data, size);
    const std::string a = in.ConsumeRandomLengthString(700);
    const std::string b = in.ConsumeRemainingBytesAsString();

    // The request reader on bytes as they come, and on the same with a plausible first line in front.
    check_request(a);
    check_request("GET /" + a);
    check_request("GET /x?" + a + " HTTP/1.1\r\n" + b);
    check_request(a + b);

    // Percent decoding: never longer than its input, and encoding then decoding gives back every byte string.
    const std::string decoded = Percent::decode(a);
    FUZZ_CHECK(decoded.size() <= a.size());
    FUZZ_CHECK(Percent::decode(Percent::encode(a)) == a);
    FUZZ_CHECK(Percent::encode(a).find_first_not_of("0123456789ABCDEFabcdefghijklmnopqrstuvwxyzGHIJKLMNOPQRSTUVWXYZ-._~%") == std::string::npos);

    // The query string, and what the library builds from fields: the fields come back from their own query string.
    const auto fields = QueryString::parse(a);
    for (const auto &[key, value] : fields) {
        FUZZ_CHECK(!key.empty());
        FUZZ_CHECK(key.find('&') == std::string::npos);
        FUZZ_CHECK(key.find('=') == std::string::npos);
        (void)value;
    }
    CaseInsensitiveMultimap made;
    if (a.find_first_of("&=") == std::string::npos && !a.empty()) {
        made.emplace(a, b);
        const auto back = QueryString::parse(QueryString::create(made));
        FUZZ_CHECK(back.size() == 1 && back.begin()->first == a && back.begin()->second == b);
    }

    // Headers and attributes (Content-Disposition style), a response's status line and the hash that goes with equality.
    std::istringstream header_stream(a);
    const auto headers = HttpHeader::parse(header_stream);
    for (const auto &[key, value] : headers) FUZZ_CHECK(key.find(':') == std::string::npos);
    const auto attributes = HttpHeader::FieldValue::SemicolonSeparatedAttributes::parse(a);
    (void)attributes;
    std::istringstream response_stream(a + b);
    std::string version, status;
    CaseInsensitiveMultimap response_header;
    (void)ResponseMessage::parse(response_stream, version, status, response_header);
    FUZZ_CHECK(CaseInsensitiveEqual()(a, a));
    std::string shouted = a;
    for (char &c : shouted) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    FUZZ_CHECK(!CaseInsensitiveEqual()(a, shouted) || CaseInsensitiveHash()(a) == CaseInsensitiveHash()(shouted));

    // Status codes: any number is some text, and the text maps back to a code.
    const auto code = static_cast<StatusCode>(in.ConsumeIntegral<int>());
    const std::string text = status_code(code);
    (void)status_code(text);
    (void)status_code(a);
    return 0;
}
