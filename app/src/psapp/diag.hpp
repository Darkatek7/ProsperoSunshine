// SPDX-License-Identifier: GPL-3.0-only
// The pure parts of the diagnostic report: what the host's breadcrumb file says (host_diag.log,
// src/platform/ps5/diag.h), the notice the home page shows for it, and the scrubbing that makes the
// report fit to post in a public issue.
#pragma once

#include "app/psapp/mask.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace psapp::diag
{

// Firmware as kernel_get_fw_version() and kern.sdk_version give it: 0x13000040 is 13.00.
std::string firmware_text(std::uint32_t firmware);
// The only firmware everything was measured on.
bool firmware_verified(std::uint32_t firmware);
// Whether an Experimental switch (its sunshine.conf key, experimental_list.h) counts on this firmware. Where not, the host
// ignores it. An unknown firmware (0) keeps the switch: the host still decides.
bool experimental_needed(std::uint32_t firmware, const std::string &key);
// Whether it doesn't count because it can't do anything here, not because the function is verified.
bool experimental_no_effect(std::uint32_t firmware, const std::string &key);

enum class Capture
{
    unknown,     // no attempt in the file
    opening,     // "open start" without an outcome: still going, or the process went down there
    ok,
    failed,
};

// One "experimental result=" line of the file: one try to open the capture while a switch counts.
struct Attempt
{
    bool ok = false;
    std::string trial;   // the functions it had to show: hevc, hdr, large, bitrate, comma separated; "none"
    std::string codec;   // hevc, h264
    std::string bits;    // 8, 10
    std::string mode;    // 1920x1080
    std::string fps;
    std::string kbit;    // the average it opened with
    std::string variant; // standard, minimal, full
    std::string step;    // where it failed: OpenVideo, Start, first frame, ... ; "none"
    std::string rc;      // the raw code as hex text; "0" when the step has none
};

// The "capture RUNNING outcome=" line: how the wait for the resource ended, when it ended without it.
struct RunningWait
{
    std::string outcome;   // timeout, refused, revoked; empty when there is no such line
    std::string value;     // data[0] of a refusal; -1 otherwise
    std::string waited_ms;
    std::string list_rc;   // list()'s result; 0 is fine
    std::string active;    // the active resources, comma separated; "none"
};

struct HostLog
{
    std::uint32_t firmware = 0;    // from the header; 0 when there is none
    std::string version;
    std::string commit;            // the build's commit; "unknown" when its build had none; empty from older hosts
    RunningWait running;           // of the last attempt
    bool refused = false;          // the host could not find the ResourceArbitrator calls
    Capture capture = Capture::unknown;
    std::string failed_step;       // "open failed step=": where it stopped
    int failed_error = 0;          // PS_CAPTURE_*
    std::string failed_rc;         // the last raw Avcap2 code, as hex text; "0" when none
    std::string last_step;         // the last capture line of an attempt without an outcome
    bool stopped = false;          // the last line says the host ended on its own
    std::string switches;          // the switches this host uses; empty when none is set
    std::string switches_set;      // what sunshine.conf had when it started, also where they don't count
    std::vector<Attempt> attempts; // this host's, the last kMaxAttempts
    std::vector<std::string> disabled; // functions off until the host restarts: hevc, hdr, large, bitrate
    std::string pending_trial;     // an attempt without an outcome had these on trial
};

constexpr std::size_t kMaxAttempts = 24;

HostLog parse_host_log(const std::string &text);

// The report's line about the capture: how the last open ended, why the wait for the resource did, and when the firmware is
// refused outright, which no switch can change.
std::string capture_summary(const HostLog &log);

// The report's part about the switches: which are set, each function's result and rc, how each attempt went, what is off now. `conf_switches` is what
// sunshine.conf holds now, which the host only reads when it starts. Only known fields, so nothing about anyone.
std::string experimental_report(const HostLog &log, const std::vector<std::string> &conf_switches);

// One line for the home page about this firmware and the last capture attempt; empty when there is
// nothing to say. `bad` says it is a failure, not a caution.
struct Notice
{
    std::string text;
    bool bad = false;
};
Notice notice(std::uint32_t firmware, const HostLog &log, bool host_running);

// The home page's line with the development switch control_lan in front of it, when the host reports it open (STATUS lan=1):
// the control socket, which has no login, then answers anyone in the network.
Notice with_control_lan(Notice notice, bool control_lan);

// Moonlight sends the same placeholder, "roth", from every device. What to call a device waiting to pair ...
std::string pairing_label(const std::string &name);
// ... and the name to offer for it: the client's own if it sent one, else the first free "Moonlight <n>".
std::string suggested_device_name(const std::string &name, const std::vector<std::string> &paired);
// What was typed as a device's name, without what the app's scan of the pairing file trips over.
std::string clean_device_name(const std::string &text);
// Denying a request (CANCELPIN on the control socket): the line to send, and what the host's answer means.
enum class DenyOutcome
{
    denied,      // "OK cancelled": Moonlight was told no
    gone,        // "ERR no such request": it ended or was answered meanwhile
    unsupported, // "ERR unknown command": a host from before CANCELPIN
    failed,      // anything else, or no answer
};
std::string deny_request(const std::string &id);
DenyOutcome parse_deny(const std::string &answer);

// A paired device, as the host's DEVICES lists it. The UUID and the mode stay in the app: neither
// goes into the report.
struct Device
{
    std::string id;
    std::string name;
    std::string mode; // the mode it last asked for, like "1280x720@60"; "" when none is known
    bool streaming = false;
};
// "OK <uuid> <streaming 0|1> <mode or -> <name>": the name is the rest of the line and may be empty.
bool parse_device_line(const std::string &line, Device *device);
std::string device_label(const Device &device);
std::string device_info(const Device &device);
// The paired devices' names: what the running host lists with DEVICES when it answered, so a device paired from the web
// page shows up at once; otherwise named_devices from sunshine_state.json, which the app reads only now and then.
std::vector<std::string> paired_names(bool devices_known, const std::vector<Device> &devices,
                                      const std::vector<std::string> &from_file);

// A running stream, as the host's STREAMS lists it. Like a Device, it stays in the app.
struct Stream
{
    std::string id;      // the device's UUID, for ENDSTREAM; "" when the host does not know the device
    std::string name;
    std::string address; // "" when unknown
    std::string codec;   // H.264, HEVC, AV1
    bool hdr = false;
    std::string mode;    // what it gets, like "1920x1080@60"; "" when unknown
    long long kbps = 0;
    long long seconds = 0; // since it started
    int pads = 0;          // its virtual controllers
    std::string role;      // capture (its stream holds the capture), joined (gets that picture), starting
    long long sent_kbps = -1; // video it really sent lately; -1 while unknown
    long long rtt_ms = -1;    // round-trip time to the device; -1 while unknown
    long long recoveries = 0; // times the device lost a picture and asked for a new one
};
// "OK uuid=<uuid> address=<ip> codec=<c> hdr=<0|1> mode=<WxH@fps> kbps=<n> secs=<n> pads=<n> role=<r> name=<name>":
// key=value in any order, unknown keys skipped, the name the rest of the line. "-" stands for an unknown value.
bool parse_stream_line(const std::string &line, Stream *stream);
std::string stream_label(const Stream &stream);
// "HEVC HDR, 1920x1080@60, 20 Mbit/s, 1 controller": what the stream sends and how many pads it has.
std::string stream_info(const Stream &stream);
// "18.3 Mbit/s · 4 ms · 2 losses": what really reaches the device; "" while the host knows none of it.
std::string stream_network(const Stream &stream);
// "45 s", "12 min", "1 h 05 min".
std::string duration_text(long long seconds);

// What the report hides, so that it fits a public issue (app/src/psapp/mask.hpp has the stand-ins and why): addresses of every kind
// (<lan-1>, <tailscale-1>, <link-1>, <public-1>, and <mac-1>; the same address has the same tag in the whole report), host names
// of URLs and Host headers, the names of the paired devices and the console (<device-3fa2> from the first characters of the
// device's uuid, <console-name>, <name-N>), client ids and uuids (<id-N>, <uuid-3fa2>), and game titles (<title>). Other text
// stays: the report is for finding a bug.
// One Scrubber per report, for all of its sections.
class Scrubber
{
public:
    Scrubber();
    Scrubber(const Scrubber &) = delete;
    Scrubber &operator=(const Scrubber &) = delete;
    // A paired device; with its uuid (as DEVICES lists it) its tag matches the uuid's.
    void add_device(const std::string &name, const std::string &uuid = {});
    void add_console(const std::string &name);
    // Anyone else: the client that streams, a request waiting to pair.
    void add_name(const std::string &name);
    // `names` false: only what has a shape (addresses, ids, titles), for text that holds no one's name.
    std::string apply(const std::string &text, bool names = true);
    // The last `count` lines, scrubbed and then cut to `width` bytes each (cut first, a cut address would escape the scrub).
    std::string tail(const std::string &text, std::size_t count, std::size_t width = 200);

private:
    void add(const std::string &name, const std::string &tag);
    mask::Masker masker_;
    std::vector<std::pair<std::string, std::string>> names_; // name and its tag, the longest name first
};
// A Scrubber for one text, with `names` as plain names.
std::string scrub(const std::string &text, const std::vector<std::string> &names);
// The last `count` lines, each cut to `width` bytes.
std::string tail(const std::string &text, std::size_t count, std::size_t width = 200);
// A name for the errno values a file or socket call gives here.
std::string errno_name(int error);

} // namespace psapp::diag
