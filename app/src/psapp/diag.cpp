// SPDX-License-Identifier: GPL-3.0-only
#include "app/psapp/diag.hpp"

#include "app/psapp/experimental_list.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace psapp::diag
{

namespace
{

// The file's capture lines come from src/platform/ps5/capture.c; PS_CAPTURE_* from capture.h.
constexpr int kBusy = -2;
constexpr int kVr = -4;
constexpr const char *kPrefix = "ProsperoSunshine: ";

std::string field(const std::string &line, const std::string &name)
{
    const std::size_t start = line.find(" " + name + "=");
    if (start == std::string::npos)
        return {};
    const std::size_t value = start + name.size() + 2;
    return line.substr(value, line.find(' ', value) - value);
}

bool starts_with(const std::string &text, const char *prefix)
{
    return text.rfind(prefix, 0) == 0;
}

bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

bool is_hex(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Replaces dotted quads in `text`.
std::string without_addresses(const std::string &text)
{
    std::string out;
    std::size_t i = 0;
    while (i < text.size())
    {
        if (is_digit(text[i]) && (i == 0 || !(is_digit(text[i - 1]) || text[i - 1] == '.')))
        {
            std::size_t at = i;
            int parts = 0;
            for (;;)
            {
                std::size_t digits = 0;
                while (at < text.size() && is_digit(text[at]) && digits < 4)
                {
                    ++at;
                    ++digits;
                }
                if (digits == 0 || digits > 3)
                    break;
                ++parts;
                if (parts == 4 || at >= text.size() || text[at] != '.')
                    break;
                ++at;
            }
            if (parts == 4 && (at >= text.size() || !is_digit(text[at])))
            {
                out += "<address>";
                i = at;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

std::string without_long_hex(const std::string &text)
{
    constexpr std::size_t kLongest = 16; // a firmware code or an address stays
    std::string out;
    std::size_t i = 0;
    while (i < text.size())
    {
        std::size_t end = i;
        while (end < text.size() && (is_hex(text[end]) || text[end] == '-'))
            ++end;
        if (end - i >= kLongest)
            out += "<id>";
        else
            out.append(text, i, end - i);
        if (end < text.size())
            out += text[end];
        i = end + 1;
    }
    return out;
}

// What someone plays isn't for a public issue: title IDs and the name in "Session app [...]". The settings app, the system's
// own titles and "PS5 Screen" (a leading space, as nvhttp sorts it first) stay.
std::string without_titles(const std::string &text)
{
    constexpr std::string_view kSession = "Session app [";
    const auto is_upper = [](char c) { return c >= 'A' && c <= 'Z'; };
    std::string out;
    std::size_t i = 0;
    while (i < text.size())
    {
        if (text.compare(i, kSession.size(), kSession) == 0)
        {
            const std::size_t from = i + kSession.size();
            const std::size_t line_end = std::min(text.find('\n', from), text.size());
            const std::size_t close = text.rfind(']', line_end);
            if (close != std::string::npos && close >= from && text[from] != ' ')
            {
                out.append(kSession).append("<title>");
                i = close;
                continue;
            }
        }
        const bool starts = i == 0 || !(is_upper(text[i - 1]) || is_digit(text[i - 1]));
        if (starts && i + 9 <= text.size() && std::all_of(text.begin() + i, text.begin() + i + 4, is_upper) &&
            std::all_of(text.begin() + i + 4, text.begin() + i + 9, is_digit) &&
            (i + 9 == text.size() || !(is_upper(text[i + 9]) || is_digit(text[i + 9]))))
        {
            const std::string_view id(text.data() + i, 9);
            if (id != "PPSA47989" && id.substr(0, 4) != "NPXS")
            {
                out += "<title>";
                i += 9;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

std::string feature_name(const std::string &feature)
{
    for (int i = 0; i < PS_EXP_SWITCH_COUNT; ++i)
    {
        if (feature == ps_exp_switch_at(i)->name)
            return ps_exp_switch_at(i)->shown;
    }
    return feature;
}

int switch_of_key(const std::string &key)
{
    for (int i = 0; i < PS_EXP_SWITCH_COUNT; ++i)
    {
        if (key == ps_exp_switch_at(i)->key)
            return i;
    }
    return -1;
}

// "hevc,large" as "HEVC and 1440p/4K".
std::string feature_names(const std::vector<std::string> &features)
{
    std::string out;
    for (std::size_t i = 0; i < features.size(); ++i)
    {
        if (i > 0)
            out += i + 1 == features.size() ? " and " : ", ";
        out += feature_name(features[i]);
    }
    return out;
}

std::vector<std::string> split_list(const std::string &list)
{
    std::vector<std::string> out;
    if (list == "none")
        return out;
    for (std::size_t at = 0; at < list.size();)
    {
        const std::size_t end = std::min(list.find(',', at), list.size());
        if (end > at)
            out.push_back(list.substr(at, end - at));
        at = end + 1;
    }
    return out;
}

void add_once(std::vector<std::string> &list, const std::string &item)
{
    if (std::find(list.begin(), list.end(), item) == list.end())
        list.push_back(item);
}

} // namespace

std::string firmware_text(std::uint32_t firmware)
{
    char text[16];
    std::snprintf(text, sizeof text, "%x.%02x", firmware >> 24, (firmware >> 16) & 0xff);
    return text;
}

bool firmware_verified(std::uint32_t firmware)
{
    const std::uint32_t version = firmware & 0xffff0000u;
    return version == 0x13000000u || version == 0x10200000u;
}

bool experimental_needed(std::uint32_t firmware, const std::string &key)
{
    const int index = switch_of_key(key);
    return index >= 0 && (firmware == 0 || ps_exp_counts(index, firmware));
}

bool experimental_no_effect(std::uint32_t firmware, const std::string &key)
{
    const int index = switch_of_key(key);
    return index >= 0 && firmware != 0 && ps_exp_switch_at(index)->no_effect(firmware);
}

HostLog parse_host_log(const std::string &text)
{
    HostLog log;
    std::string last_capture;
    std::string last_line;
    std::size_t at = 0;
    while (at < text.size())
    {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        if (line.empty())
            continue;
        if (starts_with(line, kPrefix))
            line.erase(0, std::strlen(kPrefix));
        last_line = line;
        if (starts_with(line, "host version="))
        {
            log.version = field(line, "version");
            log.commit = field(line, "commit");
            log.firmware = static_cast<std::uint32_t>(std::strtoul(field(line, "firmware").c_str(), nullptr, 16));
        }
        else if (starts_with(line, "ra refusing firmware"))
        {
            log.refused = true;
        }
        else if (starts_with(line, "experimental switches="))
        {
            log.switches = field(line, "switches");
            log.switches_set = field(line, "set");
        }
        else if (starts_with(line, "experimental attempt "))
        {
            log.pending_trial = field(line, "trial");
        }
        else if (starts_with(line, "experimental result="))
        {
            Attempt attempt;
            attempt.ok = field(line, "result") == "ok";
            attempt.trial = field(line, "trial");
            attempt.codec = field(line, "codec");
            attempt.bits = field(line, "bits");
            attempt.mode = field(line, "mode");
            attempt.fps = field(line, "fps");
            attempt.kbit = field(line, "kbit");
            attempt.variant = field(line, "variant");
            attempt.rc = field(line, "rc");
            // The step's name may hold spaces and ends the line.
            const std::size_t step = line.find(" step=");
            attempt.step = step == std::string::npos ? "unknown" : line.substr(step + 6);
            log.attempts.push_back(std::move(attempt));
            if (log.attempts.size() > kMaxAttempts)
                log.attempts.erase(log.attempts.begin());
            log.pending_trial.clear();
        }
        else if (starts_with(line, "experimental disabled "))
        {
            add_once(log.disabled, field(line, "feature"));
        }
        else if (starts_with(line, "capture open start"))
        {
            log.capture = Capture::opening;
            log.last_step.clear();
            log.running = {};
        }
        else if (starts_with(line, "capture open ok"))
        {
            log.capture = Capture::ok;
            log.last_step.clear();
            log.running = {};
        }
        else if (starts_with(line, "capture RUNNING outcome="))
        {
            log.running.outcome = field(line, "outcome");
            log.running.value = field(line, "value");
            log.running.waited_ms = field(line, "waited_ms");
            log.running.list_rc = field(line, "list_rc");
            log.running.active = field(line, "active");
            if (log.capture == Capture::opening)
                log.last_step = line.substr(8);
        }
        else if (starts_with(line, "capture open failed"))
        {
            log.capture = Capture::failed;
            // The step's name may hold spaces; "error=" comes next.
            const std::size_t step = line.find(" step=");
            const std::size_t after = line.find(" error=");
            if (step != std::string::npos && after != std::string::npos && after > step)
                log.failed_step = line.substr(step + 6, after - step - 6);
            log.failed_error = std::atoi(field(line, "error").c_str());
            log.failed_rc = field(line, "rc");
            log.last_step.clear();
        }
        else if (starts_with(line, "capture ") && log.capture == Capture::opening)
        {
            log.last_step = line.substr(8);
        }
    }
    log.stopped = last_line == "host stopped";
    return log;
}

Notice notice(std::uint32_t firmware, const HostLog &log, bool host_running)
{
    const std::string name = firmware != 0 ? "Firmware " + firmware_text(firmware) : std::string("This firmware");
    if (log.refused)
        return {name + " is not supported yet: the host cannot find the capture calls. See Diagnostics.", true};
    const std::string post = " Please post the Diagnostics report on GitHub.";
    if (!log.disabled.empty())
        return {"Experimental " + feature_names(log.disabled) + " failed on " + name + " (off until the host restarts)." + post, true};
    if (!host_running && !split_list(log.pending_trial).empty())
        return {"Experimental " + feature_names(split_list(log.pending_trial)) + " took the host down on " + name + "." + post, true};
    if (log.capture == Capture::failed)
    {
        if (log.failed_error == kVr)
            return {"The stream cannot run while PS VR is in use.", true};
        if (log.failed_error == kBusy)
            return {"Something else is using the console's capture. Close it, or restart the console.", true};
        return {"Capture failed at " + log.failed_step + " on " + name + ". See Diagnostics.", true};
    }
    if (log.capture == Capture::opening && !host_running)
        return {"The host went down while starting capture on " + name + ". See Diagnostics.", true};
    std::vector<std::string> worked;
    for (const Attempt &attempt : log.attempts)
    {
        for (const std::string &feature : split_list(attempt.trial))
        {
            if (attempt.ok)
                add_once(worked, feature);
        }
    }
    if (!worked.empty())
        return {"Experimental " + feature_names(worked) + " started on " + name + ". Is the picture right?" + post, false};
    if (firmware != 0 && !firmware_verified(firmware))
        return {name + " is untested: only 13.00 and 10.20 are verified. See Diagnostics if streaming fails.", false};
    return {};
}

std::string capture_summary(const HostLog &log)
{
    std::string out;
    if (log.refused)
        out = "refused: the ResourceArbitrator calls were not found on this firmware, so there is nothing to call and no switch can help. ";
    if (log.capture == Capture::ok)
        return out + "last open ok";
    if (log.capture == Capture::opening)
        return out + "last open has no outcome" + (log.last_step.empty() ? "" : ", after: " + log.last_step);
    if (log.capture == Capture::unknown)
        return out + "not tried since the host started";
    out += "last open failed at " + log.failed_step + " (error " + std::to_string(log.failed_error) + ", rc " + log.failed_rc + ")";
    if (!log.running.outcome.empty())
    {
        const RunningWait &wait = log.running;
        out += wait.outcome == "refused"   ? ": the system refused the resource, data[0]=" + wait.value
               : wait.outcome == "revoked" ? std::string(": the system took the resource away")
                                           : std::string(": no answer from the system");
        out += ", waited " + wait.waited_ms + " ms, active resources: " + wait.active;
        if (wait.list_rc != "0")
            out += " (list rc " + wait.list_rc + ")";
    }
    return out;
}

std::string experimental_report(const HostLog &log, const std::vector<std::string> &conf_switches)
{
    const auto listed = [](const std::string &list) { return list.empty() ? std::string("none") : list; };
    std::string conf;
    for (const std::string &name : conf_switches)
        conf += (conf.empty() ? "" : ",") + name;
    std::string out = "  In sunshine.conf: " + listed(conf) + "\n";
    out += "  The host started with: " + listed(log.switches_set) + ", and uses: " + listed(log.switches) + "\n";
    // Each function in use: how its last try went. Its name is in `trial` while it still has to show that it works.
    const std::vector<std::string> in_use = split_list(log.switches);
    for (int i = 0; i < PS_EXP_SWITCH_COUNT; ++i)
    {
        const std::string name = ps_exp_switch_at(i)->name;
        const Attempt *last = nullptr;
        for (const Attempt &attempt : log.attempts)
        {
            const std::vector<std::string> trial = split_list(attempt.trial);
            if (std::find(trial.begin(), trial.end(), name) != trial.end())
                last = &attempt;
        }
        if (last == nullptr && std::find(in_use.begin(), in_use.end(), name) == in_use.end())
            continue;
        out += "  " + std::string(ps_exp_switch_at(i)->shown) + ": ";
        if (last == nullptr)
            out += "not tried since the host started";
        else if (last->ok)
            out += "ok, rc " + last->rc + " (" + last->variant + ")";
        else
            out += "failed at " + last->step + ", rc " + last->rc + " (" + last->variant + ")";
        if (std::find(log.disabled.begin(), log.disabled.end(), name) != log.disabled.end())
            out += ", off until the host restarts";
        out += "\n";
    }
    if (log.attempts.empty())
        out += "  No attempt since the host started.\n";
    for (const Attempt &attempt : log.attempts)
    {
        out += "  " + attempt.codec + " " + attempt.bits + " bit " + attempt.mode + "@" + attempt.fps + " " + attempt.kbit + " kbit/s variant " + attempt.variant +
               " trial " + attempt.trial + ": ";
        out += (attempt.ok ? std::string("ok") : "failed at " + attempt.step) + ", rc " + attempt.rc;
        out += "\n";
    }
    if (!split_list(log.pending_trial).empty())
        out += "  An attempt has no outcome yet, on trial: " + log.pending_trial + "\n";
    if (!log.disabled.empty())
        out += "  Off until the host restarts: " + feature_names(log.disabled) + "\n";
    return out;
}

std::string pairing_label(const std::string &name)
{
    return name.empty() || name == "roth" ? "Moonlight" : name;
}

std::string clean_device_name(const std::string &text)
{
    std::string name;
    for (const char c : text)
    {
        if (c != '"' && c != '\\' && static_cast<unsigned char>(c) >= 0x20)
            name += c;
    }
    const std::size_t first = name.find_first_not_of(' ');
    if (first == std::string::npos)
        return {};
    return name.substr(first, name.find_last_not_of(' ') - first + 1);
}

std::string deny_request(const std::string &id)
{
    return "CANCELPIN " + id + "\n";
}

DenyOutcome parse_deny(const std::string &answer)
{
    if (answer == "OK cancelled" || answer == "OK")
        return DenyOutcome::denied;
    if (answer == "ERR no such request")
        return DenyOutcome::gone;
    if (answer == "ERR unknown command")
        return DenyOutcome::unsupported;
    return DenyOutcome::failed;
}

bool parse_device_line(const std::string &line, Device *device)
{
    if (line.rfind("OK ", 0) != 0)
        return false;
    const std::size_t id_end = line.find(' ', 3);
    if (id_end == std::string::npos || id_end == 3 || id_end + 2 >= line.size() || line[id_end + 2] != ' ')
        return false;
    const std::size_t mode_end = line.find(' ', id_end + 3);
    if (mode_end == std::string::npos || mode_end == id_end + 3)
        return false;
    device->id = line.substr(3, id_end - 3);
    device->streaming = line[id_end + 1] == '1';
    device->mode = line.substr(id_end + 3, mode_end - id_end - 3);
    if (device->mode == "-")
        device->mode.clear();
    device->name = line.substr(mode_end + 1);
    return true;
}

std::string device_label(const Device &device)
{
    return device.name.empty() ? "Unnamed device" : device.name;
}

std::string device_info(const Device &device)
{
    if (device.streaming)
        return device.mode.empty() ? "Streaming now" : "Streaming now, " + device.mode;
    return device.mode.empty() ? "No stream recorded yet" : "Last asked for " + device.mode;
}

std::vector<std::string> paired_names(bool devices_known, const std::vector<Device> &devices,
                                      const std::vector<std::string> &from_file)
{
    if (!devices_known)
        return from_file;
    std::vector<std::string> names;
    names.reserve(devices.size());
    for (const Device &device : devices)
        names.push_back(device_label(device));
    return names;
}

namespace
{
long long field_number(const std::string &text)
{
    if (text.empty() || text.size() > 15)
        return 0;
    long long value = 0;
    for (const char c : text)
    {
        if (c < '0' || c > '9')
            return 0;
        value = value * 10 + (c - '0');
    }
    return value;
}
} // namespace

bool parse_stream_line(const std::string &line, Stream *stream)
{
    if (line.rfind("OK ", 0) != 0)
        return false;
    // The values before the name hold no spaces, so the first " name=" is the key.
    const std::size_t name_at = line.find(" name=");
    if (name_at == std::string::npos)
        return false;
    Stream parsed;
    bool have_id = false;
    for (std::size_t at = 3; at < name_at;)
    {
        std::size_t end = line.find(' ', at);
        if (end == std::string::npos || end > name_at)
            end = name_at;
        const std::string token = line.substr(at, end - at);
        at = end + 1;
        const std::size_t eq = token.find('=');
        if (eq == std::string::npos || eq == 0)
            continue;
        const std::string key = token.substr(0, eq);
        std::string value = token.substr(eq + 1);
        if (value == "-")
            value.clear();
        if (key == "uuid")
        {
            parsed.id = value;
            have_id = true;
        }
        else if (key == "address")
            parsed.address = value;
        else if (key == "codec")
            parsed.codec = value;
        else if (key == "hdr")
            parsed.hdr = value == "1";
        else if (key == "mode")
            parsed.mode = value;
        else if (key == "kbps")
            parsed.kbps = field_number(value);
        else if (key == "secs")
            parsed.seconds = field_number(value);
        else if (key == "pads")
            parsed.pads = static_cast<int>(std::min<long long>(field_number(value), 99));
        else if (key == "role")
            parsed.role = value;
        else if (key == "sent_kbps")
            parsed.sent_kbps = value.empty() ? -1 : field_number(value);
        else if (key == "rtt_ms")
            parsed.rtt_ms = value.empty() ? -1 : field_number(value);
        else if (key == "recoveries")
            parsed.recoveries = field_number(value);
    }
    if (!have_id)
        return false;
    parsed.name = line.substr(name_at + 6);
    *stream = parsed;
    return true;
}

std::string stream_label(const Stream &stream)
{
    return stream.name.empty() ? "Unnamed device" : stream.name;
}

namespace
{
// "20 Mbit/s", "6.5 Mbit/s", "800 kbit/s".
std::string rate_text(long long kbps)
{
    if (kbps >= 10000)
        return std::to_string((kbps + 500) / 1000) + " Mbit/s";
    if (kbps >= 1000)
        return std::to_string(kbps / 1000) + "." + std::to_string(kbps % 1000 / 100) + " Mbit/s";
    return std::to_string(kbps) + " kbit/s";
}
}

std::string stream_info(const Stream &stream)
{
    std::string info = stream.codec.empty() ? std::string("Video") : stream.codec;
    if (stream.hdr)
        info += " HDR";
    if (!stream.mode.empty())
        info += ", " + stream.mode;
    if (stream.kbps > 0)
        info += ", " + rate_text(stream.kbps);
    info += stream.pads == 0 ? ", no controller" : stream.pads == 1 ? ", 1 controller" : ", " + std::to_string(stream.pads) + " controllers";
    return info;
}

std::string stream_network(const Stream &stream)
{
    std::string text;
    const auto add = [&](const std::string &part) { text += (text.empty() ? "" : " \xC2\xB7 ") + part; };
    if (stream.sent_kbps >= 0)
        add(rate_text(stream.sent_kbps));
    if (stream.rtt_ms >= 0)
        add(std::to_string(stream.rtt_ms) + " ms");
    if (stream.recoveries > 0)
        add(stream.recoveries == 1 ? std::string("1 loss") : std::to_string(stream.recoveries) + " losses");
    return text;
}

std::string duration_text(long long seconds)
{
    if (seconds < 0)
        seconds = 0;
    if (seconds < 60)
        return std::to_string(seconds) + " s";
    if (seconds < 3600)
        return std::to_string(seconds / 60) + " min";
    const long long minutes = seconds / 60 % 60;
    return std::to_string(seconds / 3600) + " h " + (minutes < 10 ? "0" : "") + std::to_string(minutes) + " min";
}

std::string suggested_device_name(const std::string &name, const std::vector<std::string> &paired)
{
    if (pairing_label(name) != "Moonlight")
        return name;
    for (int number = 1;; ++number)
    {
        const std::string candidate = "Moonlight " + std::to_string(number);
        if (std::find(paired.begin(), paired.end(), candidate) == paired.end())
            return candidate;
    }
}

std::string scrub(const std::string &text, const std::vector<std::string> &names)
{
    std::string out = text;
    for (const std::string &name : names)
    {
        if (name.size() < 3)
            continue;
        for (std::size_t at = out.find(name); at != std::string::npos; at = out.find(name, at + 6))
            out.replace(at, name.size(), "<name>");
    }
    return without_titles(without_long_hex(without_addresses(out)));
}

std::string tail(const std::string &text, std::size_t count, std::size_t width)
{
    std::size_t start = text.size();
    if (start > 0 && text[start - 1] == '\n')
        --start;
    for (std::size_t lines = 0; start > 0; --start)
    {
        if (text[start - 1] == '\n' && ++lines == count)
            break;
    }
    std::string out;
    for (std::size_t at = start; at < text.size();)
    {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        out.append(text, at, end - at > width ? width : end - at);
        out += '\n';
        at = end + 1;
    }
    return out;
}

std::string errno_name(int error)
{
    const char *name = nullptr;
    switch (error)
    {
    case EPERM:
        name = "operation not permitted";
        break;
    case ENOENT:
        name = "no such file";
        break;
    case EIO:
        name = "I/O error";
        break;
    case EACCES:
        name = "permission denied";
        break;
    case ENOTDIR:
        name = "not a folder";
        break;
    case EISDIR:
        name = "is a folder";
        break;
    case EROFS:
        name = "read-only file system";
        break;
    case ENOSPC:
        name = "no space left";
        break;
    default:
        break;
    }
    return "errno " + std::to_string(error) + (name != nullptr ? std::string(" (") + name + ")" : std::string());
}

} // namespace psapp::diag
