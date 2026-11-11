// SPDX-License-Identifier: GPL-3.0-only
#include "control/file_bridge.hpp"
#include "../../app/src/psapp/file_wire.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <vector>

namespace prospero::file_bridge {
namespace fs = std::filesystem;
using namespace std::literals;

namespace {
constexpr auto host_elf = "/data/prosperosunshine/prospero_sunshine.elf"sv;
constexpr auto app_elf = "/data/homebrew/PPSA47989/payloads/prospero_sunshine.elf"sv;
constexpr std::size_t largest_read = 4u << 20;

// What the app reads: its host.hpp and autoload.hpp.
constexpr std::array readable{
    "/data/prosperosunshine"sv,
    "/data/prosperosunshine/sunshine.conf"sv,
    "/data/prosperosunshine/sunshine_state.json"sv,
    "/data/prosperosunshine/host_diag.log"sv,
    "/data/prosperosunshine/sunshine.log"sv,
    "/data/prosperosunshine/diagnostics.txt"sv,
    "/data/prosperosunshine/pad_log"sv,
    host_elf,
    "/data/pldmgr"sv,
    "/data/pldmgr/autoload.txt"sv,
    "/data/pldmgr/pldmgr_config.txt"sv,
    "/data/pldmgr/payloads/local/prospero-sunshine"sv,
    "/data/pldmgr/payloads/local/prospero-sunshine/prospero_sunshine.elf"sv,
};
constexpr std::array writable{
    "/data/prosperosunshine/sunshine.conf"sv,
    "/data/prosperosunshine/diagnostics.txt"sv,
};

template <std::size_t N> bool listed(const std::array<std::string_view, N> &list, std::string_view path) {
    return std::find(list.begin(), list.end(), path) != list.end();
}

fs::path under(const fs::path &root, std::string_view path) {
    return root.empty() ? fs::path(path) : root / path.substr(1);
}

bool read_file(const fs::path &path, std::string &out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), {});
    return !file.bad();
}

// Through a temporary file, as the app writes it.
bool replace_file(const fs::path &path, std::string_view bytes, fs::perms mode) {
    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.write(bytes.data(), static_cast<std::streamsize>(bytes.size())) || !file.flush()) {
            file.close();
            std::remove(temporary.c_str());
            return false;
        }
    }
    // A rename that works clears ec, so a failed chmod is checked before it.
    std::error_code ec;
    fs::permissions(temporary, mode, ec);
    if (!ec) fs::rename(temporary, path, ec);
    if (ec) std::remove(temporary.c_str());
    return !ec;
}

std::string stat_line(const fs::path &path) {
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0) return "ERR missing";
    char mode[16];
    std::snprintf(mode, sizeof mode, "%04o", static_cast<unsigned>(info.st_mode & 07777));
    if (S_ISDIR(info.st_mode)) return "OK dir 0 "s + mode;
    return "OK file " + std::to_string(static_cast<long long>(info.st_size)) + ' ' + mode;
}
}

namespace {
constexpr std::array denied_keys{"pkey"sv, "cert"sv, "log_path"sv, "file_state"sv, "credentials_file"sv, "file_apps"sv,
                                 "global_prep_cmd"sv};

bool is_denied(std::string_view key) { return std::find(denied_keys.begin(), denied_keys.end(), key) != denied_keys.end(); }

// How Sunshine reads sunshine.conf (parse_option and parse_config in its src/config.cpp), step for step, since it is the one
// that decides which keys count. It is not line by line: a lone CR ends a line too, and the character after a CR or after a
// closing ] is skipped whatever it is, so "a=b\r\rpkey=x" and "a=[]\npkey=x" hold a pkey that splitting at \n does not show
// (found by tests/fuzz/file_bridge_fuzz.cpp). The keys in file order.
bool line_end(char c) { return c == '\r' || c == '\n'; }
bool blank(char c) { return c == ' ' || c == '\t'; }
bool white(char c) { return blank(c) || line_end(c); }

std::vector<std::string> sunshine_keys(std::string_view conf) {
    std::vector<std::string> keys;
    const char *pos = conf.data(), *const end = conf.data() + conf.size();
    while (pos < end) {
        const char *begin = std::find_if_not(pos, end, white);
        const char *endl = std::find_if(begin, end, line_end);
        const char *endc = std::find(begin, endl, '#');
        while (endc > begin && white(endc[-1])) --endc;
        const char *eq = std::find(begin, endc, '=');
        if (eq != endc && eq != begin) {
            const char *end_name = eq;
            while (end_name > begin && blank(end_name[-1])) --end_name;
            const char *begin_value = std::find_if_not(eq + 1, endc, blank);
            if (begin_value != endl) {
                // A list goes on over lines up to its closing bracket, nested ones included.
                if (*begin_value == '[') {
                    int depth = 1;
                    endl = begin_value + 1;
                    while (endl != end && depth) {
                        if (*endl == '[') ++depth;
                        if (*endl == ']') --depth;
                        ++endl;
                    }
                }
                // The name up to a comment, as Sunshine copies it (a # cannot be inside it: the = would not be found).
                keys.emplace_back(begin, std::find(begin, end_name, '#'));
            }
        }
        pos = endl;
        if (pos != end) pos = std::min(end, pos + (*pos == '\r' ? 2 : 1));
    }
    return keys;
}
}

std::string path_key(const std::string &conf) {
    for (const std::string &key : sunshine_keys(conf))
        if (is_denied(key)) return key;
    // And what a plain look at each line finds, in case Sunshine's reading changes: a key in a list's line counts too.
    std::istringstream lines(conf);
    for (std::string line; std::getline(lines, line);) {
        const size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') continue;
        const size_t end = line.find_first_of(" \t=", start);
        const std::string_view key = std::string_view(line).substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (is_denied(key)) return std::string(key);
    }
    return {};
}

std::string file(const std::string &request, const fs::path &root) {
    std::istringstream in(request);
    std::string action, path;
    in >> action >> path;
    if (action == "PUT") {
        if (!listed(writable, path)) return "ERR denied";
        std::string hex, bytes;
        in >> hex;
        if (!psapp::file_wire::from_hex(hex, &bytes)) return "ERR not hex";
        if (path.ends_with("/sunshine.conf"))
            if (const std::string key = path_key(bytes); !key.empty()) return "ERR not allowed: " + key;
        constexpr auto everyone = fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::group_write |
                                  fs::perms::others_read | fs::perms::others_write;
        return replace_file(under(root, path), bytes, everyone) ? "OK" : "ERR cannot write";
    }
    if (action != "STAT" && action != "GET") return "ERR usage: FILE STAT|GET|PUT <path>";
    if (!listed(readable, path)) return "ERR denied";
    const fs::path real = under(root, path);
    if (action == "STAT") return stat_line(real);
    std::size_t limit = largest_read;
    in >> limit;
    std::error_code ec;
    const auto size = fs::file_size(real, ec);
    if (ec) return fs::exists(real, ec) ? "ERR cannot read" : "ERR missing";
    if (size > std::min(limit, largest_read)) return "ERR too large";
    std::string bytes;
    if (!read_file(real, bytes)) return "ERR cannot read";
    return "OK " + psapp::file_wire::to_hex(bytes);
}

std::string install(bool check_only, const fs::path &root) {
    std::string carried, installed;
    if (!read_file(under(root, app_elf), carried) || carried.empty()) return "ERR the app carries no host";
    const bool same = read_file(under(root, host_elf), installed) && installed == carried;
    if (same) return "OK same";
    if (check_only) return "OK differs";
    return replace_file(under(root, host_elf), carried, fs::perms::all) ? "OK installed" : "ERR cannot write the host";
}

}
