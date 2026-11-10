// SPDX-License-Identifier: GPL-3.0-only
// FILE on the control socket (src/control/file_bridge.cpp): requests are lines, answers are lines, and a sunshine.conf the app
// sends may not carry the keys that point the host at other files. Those keys are found by path_key(), which has to see
// every key Sunshine's own parser would read, so the reference parser of Sunshine (src/config.cpp parse_option and
// parse_config, copied here) decides what counts as a key.
#include "fuzz_common.hpp"
#include "../../app/src/psapp/file_wire.hpp"
#include "control/file_bridge.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;
using namespace prospero::file_bridge;
using namespace std::literals;

namespace {

// ---- Sunshine's config parser, as in the patched source the host is built from (src/config.cpp). ----
bool endline(char ch) { return ch == '\r' || ch == '\n'; }
bool space_tab(char ch) { return ch == ' ' || ch == '\t'; }
bool whitespace(char ch) { return space_tab(ch) || endline(ch); }

template <class It> It skip_list(It skipper, It end) {
    int stack = 1;
    while (skipper != end && stack) {
        if (*skipper == '[') ++stack;
        if (*skipper == ']') --stack;
        ++skipper;
    }
    return skipper;
}

std::string to_string(const char *begin, const char *end) {
    std::string result;
    for (auto pos = begin; pos != end;) {
        auto comment = std::find(pos, end, '#');
        auto endl = std::find_if(comment, end, endline);
        result.append(pos, comment);
        pos = endl;
    }
    return result;
}

std::pair<std::string_view::const_iterator, std::optional<std::pair<std::string, std::string>>> parse_option(
    std::string_view::const_iterator begin, std::string_view::const_iterator end) {
    begin = std::find_if_not(begin, end, whitespace);
    auto endl = std::find_if(begin, end, endline);
    auto endc = std::find(begin, endl, '#');
    endc = std::find_if(std::make_reverse_iterator(endc), std::make_reverse_iterator(begin), std::not_fn(whitespace)).base();
    auto eq = std::find(begin, endc, '=');
    if (eq == endc || eq == begin) return std::make_pair(endl, std::nullopt);
    auto end_name = std::find_if_not(std::make_reverse_iterator(eq), std::make_reverse_iterator(begin), space_tab).base();
    auto begin_val = std::find_if_not(eq + 1, endc, space_tab);
    if (begin_val == endl) return std::make_pair(endl, std::nullopt);
    if (*begin_val == '[') {
        endl = skip_list(begin_val + 1, end);
        if (endl == end && end == begin_val + 1) return std::make_pair(endl, std::nullopt);
    }
    return std::make_pair(endl, std::make_pair(to_string(begin, end_name), to_string(begin_val, endl)));
}

std::set<std::string> sunshine_keys(std::string_view content) {
    std::set<std::string> keys;
    auto pos = std::begin(content);
    auto end = std::end(content);
    while (pos < end) {
        auto [endl, var] = parse_option(pos, end);
        pos = endl;
        if (pos != end) pos += (*pos == '\r') ? 2 : 1;
        if (var) keys.insert(var->first);
    }
    return keys;
}

// The keys file_bridge.cpp refuses; keep in step with path_key().
constexpr std::array denied{"pkey"sv, "cert"sv, "log_path"sv, "file_state"sv, "credentials_file"sv, "file_apps"sv, "global_prep_cmd"sv};

// ---- The folder the requests run against. ----
const fs::path &root() {
    static const fs::path dir = [] {
        const fs::path path = fuzz::scratch() / "console";
        fs::create_directories(path / "data/prosperosunshine");
        fs::create_directories(path / "data/pldmgr");
        return path;
    }();
    return dir;
}

constexpr std::array paths{
    "/data/prosperosunshine"sv, "/data/prosperosunshine/sunshine.conf"sv, "/data/prosperosunshine/sunshine_state.json"sv,
    "/data/prosperosunshine/host_diag.log"sv, "/data/prosperosunshine/diagnostics.txt"sv, "/data/prosperosunshine/pad_log"sv,
    "/data/pldmgr/autoload.txt"sv, "/data/prosperosunshine/prospero_sunshine.elf"sv, "/data/prosperosunshine/../x"sv,
    "/etc/passwd"sv, "data/prosperosunshine/sunshine.conf"sv, ""sv};

void check_answer(const std::string &answer) {
    FUZZ_CHECK(answer.find('\n') == std::string::npos && answer.find('\r') == std::string::npos);
    FUZZ_CHECK(answer.starts_with("OK") || answer.starts_with("ERR "));
    if (answer.starts_with("OK ") && answer.size() > 3 && std::isxdigit(static_cast<unsigned char>(answer[3]))) {
        std::string bytes;
        const std::string hex = answer.substr(3);
        if (hex.find_first_not_of("0123456789abcdef") == std::string::npos) {
            FUZZ_CHECK(psapp::file_wire::from_hex(hex, &bytes));
            FUZZ_CHECK(psapp::file_wire::to_hex(bytes) == hex);
        }
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider in(data, size);

    // The hex of the wire.
    {
        const std::string text = in.ConsumeRandomLengthString(60);
        std::string bytes;
        if (psapp::file_wire::from_hex(text, &bytes)) FUZZ_CHECK(psapp::file_wire::to_hex(bytes) == text && bytes.size() * 2 == text.size());
        std::string back;
        FUZZ_CHECK(psapp::file_wire::from_hex(psapp::file_wire::to_hex(text), &back) && back == text);
    }

    // A conf the app sends: Sunshine would read one of the denied keys only if path_key() says so.
    const std::string conf = in.ConsumeRandomLengthString(200);
    {
        const std::string key = path_key(conf);
        if (!key.empty()) FUZZ_CHECK(std::find(denied.begin(), denied.end(), key) != denied.end());
        for (const auto &sunshine_key : sunshine_keys(conf))
            if (std::find(denied.begin(), denied.end(), sunshine_key) != denied.end())
                FUZZ_CHECK(!key.empty());  // Sunshine reads a path key that path_key() let through
    }

    // The request, either as a line of its own or built from the listed pieces.
    const int mode = in.ConsumeIntegralInRange<int>(0, 3);
    std::string request;
    if (mode == 0) {
        request = in.ConsumeRandomLengthString(150);
    } else {
        static constexpr std::array actions{"STAT"sv, "GET"sv, "PUT"sv, "DELETE"sv, ""sv};
        request = std::string(actions[in.ConsumeIntegralInRange<size_t>(0, actions.size() - 1)]) + ' ' +
                  std::string(paths[in.ConsumeIntegralInRange<size_t>(0, paths.size() - 1)]);
        if (mode == 1) request += ' ' + in.ConsumeRandomLengthString(20);
        if (mode == 2) request += ' ' + psapp::file_wire::to_hex(in.ConsumeRandomLengthString(80));
        if (mode == 3) request += ' ' + psapp::file_wire::to_hex(conf);
    }
    // The request line itself has no line break when it comes off the socket.
    request.erase(std::remove_if(request.begin(), request.end(), [](char c) { return c == '\n' || c == '\r'; }), request.end());
    const fs::path conf_file = root() / "data/prosperosunshine/sunshine.conf";
    std::ofstream(conf_file, std::ios::binary | std::ios::trunc) << "log_level = 1\n";
    const std::string answer = file(request, root());
    check_answer(answer);
    {
        std::istringstream words(request);
        std::string action, path, hex;
        words >> action >> path >> hex;
        const std::string_view listed_write = "/data/prosperosunshine/sunshine.conf"sv;
        std::string bytes;
        if (action == "PUT" && path == listed_write && psapp::file_wire::from_hex(hex, &bytes) && answer == "OK") {
            // A PUT that went through wrote exactly what was sent, and Sunshine reads no path key in it.
            std::ifstream written(conf_file, std::ios::binary);
            const std::string now((std::istreambuf_iterator<char>(written)), {});
            FUZZ_CHECK(now == bytes);
            for (const auto &key : sunshine_keys(now)) FUZZ_CHECK(std::find(denied.begin(), denied.end(), key) == denied.end());
        }
        if (path.find("..") != std::string::npos || !path.starts_with("/data/"))
            if (action == "PUT" || action == "GET" || action == "STAT") FUZZ_CHECK(answer == "ERR denied");
    }
    // Nothing outside the folder of this run was touched.
    FUZZ_CHECK(!fs::exists(root() / "x") && !fs::exists(root() / "data/x"));
    return 0;
}
