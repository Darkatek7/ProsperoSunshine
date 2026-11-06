// SPDX-License-Identifier: GPL-3.0-only
#include "web/web_report.hpp"
#include "control/control_server.hpp"
#include "web/web_lines.hpp"
#include "app/psapp/conf.hpp"
#include "app/psapp/diag.hpp"
#include "app/psapp/experimental_list.h"
#include "src/config.h"
#include "src/platform/common.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <netinet/in.h>
#include <optional>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <ps5/kernel.h>

namespace prospero::web {

namespace {

constexpr std::uint16_t loader_port = 9021;  // the ELF loader the app sends the host to

// The file's last `limit` bytes (all of a smaller one); empty optional when it cannot be opened.
std::optional<std::string> read_tail(const std::filesystem::path &path, std::size_t limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    const std::streamoff from = size > static_cast<std::streamoff>(limit) ? size - static_cast<std::streamoff>(limit) : 0;
    in.seekg(from, std::ios::beg);
    std::string text(static_cast<std::size_t>(size - from), '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(in.gcount()));
    return text;
}

// Whether something listens on 127.0.0.1:<port>; half a second at most.
bool port_open(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool open = ::connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof address) == 0;
    if (!open && errno == EINPROGRESS) {
        pollfd wait{fd, POLLOUT, 0};
        if (::poll(&wait, 1, 500) > 0) {
            int error = 0;
            socklen_t length = sizeof error;
            open = ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0;
        }
    }
    ::close(fd);
    return open;
}

// What the host sees at `path`: kind, size and mode.
std::string describe_path(const std::filesystem::path &path) {
    std::error_code ec;
    const auto info = std::filesystem::status(path, ec);
    if (info.type() == std::filesystem::file_type::not_found) return "missing";
    if (ec) return "status " + ec.message();
    char mode[16];
    std::snprintf(mode, sizeof mode, "%04o", static_cast<unsigned>(info.permissions()) & 07777u);
    std::string text;
    if (std::filesystem::is_directory(info)) {
        text = "folder";
    } else if (std::filesystem::is_regular_file(info)) {
        const auto size = std::filesystem::file_size(path, ec);
        text = ec ? std::string("file") : "file size=" + std::to_string(static_cast<unsigned long long>(size));
    } else {
        text = "other";
    }
    return text + " mode=" + mode;
}

std::string no_file(const std::filesystem::path &path) {
    std::error_code ec;
    return "  no file: " + std::string(std::filesystem::exists(path, ec) ? "cannot read " : "missing ") + path.string() + "\n";
}

}  // namespace

std::string build_report(ControlServer &control) {
    const std::filesystem::path data = platf::appdata();
    const std::filesystem::path conf_path = config::sunshine.config_file;
    const std::filesystem::path diag_path = data / "host_diag.log";
    const std::filesystem::path log_path = config::sunshine.log_file;
    const std::filesystem::path elf_path = data / "prospero_sunshine.elf";

    // What the host knows about the devices and the stream, the way the app asks for it.
    const std::vector<std::string> devices = ok_lines(control.handle("DEVICES", ControlServer::Caller::web));
    const std::vector<std::string> status_lines = ok_lines(control.handle("STATUS", ControlServer::Caller::web));
    const std::vector<std::string> pairings = ok_lines(control.handle("PAIRINGS", ControlServer::Caller::web));
    const Record state = status_lines.empty() ? Record() : status(status_lines.front());
    const auto state_value = [&state](const char *key) {
        const auto it = state.find(key);
        return it == state.end() ? std::string() : it->second;
    };

    // sunshine.conf as the app reads it: the keys of option_specs(), sorted like the app's map.
    std::map<std::string, std::optional<std::string>> settings;
    {
        const psapp::Config config = psapp::Config::parse(read_tail(conf_path, 1u << 20).value_or(std::string()));
        for (const psapp::OptionSpec &spec : psapp::option_specs()) settings[spec.key] = psapp::option_value(config, spec);
    }

    const std::optional<std::string> host_file = read_tail(diag_path, 4u << 20);
    const std::optional<std::string> log_file = read_tail(log_path, 4u << 20);
    const std::filesystem::path older_diag_path = data / "host_diag.log.1";
    const std::optional<std::string> older_host_file = read_tail(older_diag_path, 4u << 20);
    const psapp::diag::HostLog host_log = psapp::diag::parse_host_log(host_file.value_or(std::string()));
    const std::uint32_t firmware = kernel_get_fw_version();

    std::string out = "ProsperoSunshine diagnostics (built by the host for the web page)\n\n";
    const auto line = [&out](const std::string &name, const std::string &value) { out += name + ": " + value + "\n"; };
    line("Requested from", "web page");
    line("App", "unknown");
    char code[16];
    std::snprintf(code, sizeof code, "%#x", static_cast<unsigned>(firmware));
    line("Firmware", firmware == 0 ? std::string("unknown")
                                   : psapp::diag::firmware_text(firmware) + " (" + code + ")" +
                                         (psapp::diag::firmware_verified(firmware) ? ", verified" : ", untested"));
    const std::string commit = PROJECT_VERSION_COMMIT;
    line("Host", std::string("running") + (state_value("running") == "1" ? ", streaming" : "") + ", version " +
                     PROJECT_VERSION + ", commit " + (commit.empty() ? "unknown" : commit));
    {
        std::error_code ec;
        const auto size = std::filesystem::file_size(elf_path, ec);
        line("Host ELF", ec ? std::string("not in the data folder")
                            : std::to_string(static_cast<unsigned long long>(size)) + " bytes (not compared with the app's)");
    }
    line("Capture", psapp::diag::capture_summary(host_log));
    const psapp::diag::Notice notice = psapp::diag::notice(firmware, host_log, true);
    line("Notice", notice.text.empty() ? "none" : notice.text);
    line("Paired devices", std::to_string(devices.size()));
    line("Network", "has a LAN address (the web page reached the host)");
    const auto port = [](std::uint16_t number) { return std::to_string(number) + (port_open(number) ? " open" : " closed"); };
    line("Ports", "ELF loader " + port(loader_port) + ", control " + port(ControlServer::port) + ", stream " +
                      port(config::sunshine.port));
    line("Web page", "on");

    out += "\nPaths (the host's view, uid " + std::to_string(::getuid()) + "):\n";
    for (const std::filesystem::path &path : {data, conf_path, elf_path, diag_path, log_path})
        out += "  " + path.string() + ": " + describe_path(path) + "\n";

    out += "\nSettings changed from the defaults:\n";
    bool any = false;
    for (const auto &[key, value] : settings) {
        // The console's name stays out; the other options say nothing about anyone.
        if (!value) continue;
        out += "  " + key + " = " + (key == "sunshine_name" ? std::string("<name>") : *value) + "\n";
        any = true;
    }
    if (!any) out += "  none\n";

    // Everything that names someone: the paired devices, the console, the streaming client and those waiting to pair.
    std::vector<std::string> names;
    for (const std::string &device_line : devices) names.push_back(device(device_line)["name"]);
    if (const auto it = settings.find("sunshine_name"); it != settings.end() && it->second) names.push_back(*it->second);
    names.push_back(config::nvhttp.sunshine_name);
    names.push_back(state_value("client"));
    for (const std::string &pairing_line : pairings) {
        const std::string name = pairing(pairing_line)["name"];
        if (psapp::diag::pairing_label(name) != "Moonlight") names.push_back(name);
    }
    std::vector<std::string> switches;
    for (int i = 0; i < PS_EXP_SWITCH_COUNT; ++i) {
        const char *key = ps_exp_switch_at(i)->key;
        const auto it = settings.find(key);
        if (it != settings.end() && it->second && *it->second == "1") switches.push_back(key);
    }
    out += "\nExperimental switches:\n";
    out += psapp::diag::scrub(psapp::diag::experimental_report(host_log, switches), names);
    out += "\nHost breadcrumbs (host_diag.log):\n";
    out += host_file ? psapp::diag::scrub(psapp::diag::tail(*host_file, 120), names) : no_file(diag_path);
    if (older_host_file) {
        // How the last run ended, or how a long run started (the app's report has the same section).
        out += "\nOlder breadcrumbs (host_diag.log.1: the last run, or the lines before the newest), last lines:\n";
        out += psapp::diag::scrub(psapp::diag::tail(*older_host_file, 60), names);
    }
    out += "\nsunshine.log, last lines:\n";
    out += log_file ? psapp::diag::scrub(psapp::diag::tail(*log_file, 80), names) : no_file(log_path);
    return out;
}

}  // namespace prospero::web
