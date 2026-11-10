// SPDX-License-Identifier: GPL-3.0-only
// One made-up line of every kind of log line that can hold something private, through the three things that stand between the
// console and a public issue, and a check that the private value does not come out:
//   report  - the diagnostic report's scrub (app/src/psapp/diag.cpp, Scrubber): what the app and the web page post
//   debug   - the debug build's log filter (src/log/log_filter.hpp, mask_addresses): sunshine.log, the kernel log and the remote log
//   normal  - the normal build's log filter: what stays on the console unless the user posts the file itself
// The lines are what Sunshine, the patches and the host really write (the shapes are read from the code and from logs of a
// console; every value here is made up). When a log statement is added or changed, scripts/check_log_sites.sh fails until the
// statement is listed; add its sample here first. What is hidden where, and why: README "What the report contains and hides".
#include "app/psapp/diag.hpp"
#include "log/log_filter.hpp"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// Made-up values. A real one never goes in here.
constexpr const char *lan_client = "192.168.1.23";
constexpr const char *lan_other = "192.168.1.31";
constexpr const char *lan_router = "192.168.1.1";
constexpr const char *public_a = "203.0.113.45";
constexpr const char *public_b = "198.51.100.77";
constexpr const char *public_v6 = "2001:db8:aa::77";
constexpr const char *link_v6 = "fe80::a1b2";
constexpr const char *tailnet = "100.64.7.8";
constexpr const char *mac = "00:11:22:33:44:5a";
constexpr const char *host_name = "deck.home.example.org";
constexpr const char *phone = "Tom's iPhone";
constexpr const char *phone_uuid = "1b4e28ba-2fa1-11d2-883f-0016d3cca427";
constexpr const char *console_name = "Deck Room";
constexpr const char *client_id = "FEDCBA9876543210";
constexpr const char *request_uuid = "6f1c0d3e-9a2b-4c5d-8e7f-a1b2c3d4e5f6";
constexpr const char *stream_key = "00112233445566778899aabbccddeeff";
constexpr const char *ping_payload = "4341453744303335";
constexpr const char *fingerprint = "0123456789abcdef";
constexpr const char *title_id = "PPSA99310";
constexpr const char *game = "Mario 64";

struct Sample {
    const char *kind;      // what it is, and where it comes from
    std::string line;
    // Values that must not come out of each of the three; "keep" must come out of all of them (what a developer needs).
    std::vector<std::string> report, debug, normal, keep;
};

std::string cat(std::initializer_list<const char *> parts) {
    std::string out;
    for (const char *part : parts) out += part;
    return out;
}

std::vector<Sample> samples() {
    std::vector<Sample> out;
    // ---- Addresses in what Sunshine writes at info level and above (so also in the normal build) ------------------------------
    out.push_back({"stream.cpp: Rejected connection (warning)", cat({"Rejected connection from [", lan_client, "]: it's not properly set up"}),
                   {lan_client}, {lan_client}, {}, {"not properly set up"}});
    out.push_back({"stream.cpp: Couldn't send gamepad feedback / HDR / termination (warning)", cat({"Couldn't send gamepad feedback to [", public_a, ":50123]"}),
                   {public_a}, {public_a}, {public_a}, {"Couldn't send gamepad feedback", ":50123]"}});
    out.push_back({"stream.cpp: Ping Timeout (info), a v6 client", cat({"[", public_v6, "]: Ping Timeout"}), {public_v6}, {public_v6}, {public_v6}, {"Ping Timeout"}});
    out.push_back({"stream.cpp: Ping Timeout (info), Sunshine's address form without brackets", cat({public_v6, ":50123: Ping Timeout"}), {public_v6}, {public_v6}, {public_v6}, {"Ping Timeout"}});
    out.push_back({"patch 0030: Pairing refused (info)", cat({"Pairing refused for ", public_a, ": it isn't from the LAN or Tailscale, and pair_over_internet is off (3 more since the last line)"}),
                   {public_a}, {public_a}, {public_a}, {"Pairing refused", "3 more"}});
    out.push_back({"patch 0021: SSL Verification error (warning), a client over IPv6", cat({"SSL Verification error :: self-signed certificate (client ", fingerprint, " at ", public_v6, ")"}),
                   {public_v6, fingerprint}, {public_v6}, {public_v6}, {"SSL Verification error", "self-signed certificate"}});
    out.push_back({"patch 0021: SSL Verification error (warning), a client from home", cat({"SSL Verification error :: certificate has expired (client ", fingerprint, " at ", lan_client, ")"}),
                   {lan_client, fingerprint}, {lan_client}, {}, {"certificate has expired"}});
    out.push_back({"web_server.cpp: login (info)", cat({"Web page: login from ", lan_client, " (remembered for 30 days)"}), {lan_client}, {lan_client}, {}, {"remembered for 30 days"}});
    out.push_back({"web_server.cpp: failed login (warning), from the internet side of a forward", cat({"Web page: failed login from ", public_a}), {public_a}, {public_a}, {public_a}, {"failed login"}});
    out.push_back({"web_server.cpp: a Tailscale forward (loopback is kept, it says what the address means)", "Web page: failed login from 127.0.0.1 (the console itself, or a local forward such as ps5-tailscale)",
                   {}, {}, {}, {"127.0.0.1", "ps5-tailscale"}});
    // ---- UPnP (src/net/upnp_client.cpp): the router, the address the internet sees, a device that holds the port -----------------
    out.push_back({"upnp_client.cpp: router and internet side (info)", cat({"UPnP: outer router ", lan_router, ", internet side ", public_b}), {lan_router, public_b}, {lan_router, public_b}, {public_b}, {"internet side"}});
    out.push_back({"upnp_client.cpp: private internet side (info)", cat({"UPnP: inner router ", lan_router, ", internet side ", tailnet, " (private: another router or the provider's NAT is in front)"}),
                   {lan_router, tailnet}, {lan_router, tailnet}, {}, {"(private: another router or the provider's NAT is in front)"}});
    out.push_back({"upnp.hpp describe: port forwarded to another device (warning)", cat({"UPnP: ", lan_router, " TCP 47984 forwarded to another device (", lan_other, "), left as it is"}),
                   {lan_router, lan_other}, {lan_router, lan_other}, {}, {"TCP 47984", "left as it is"}});
    out.push_back({"upnp_client.cpp: removal refused (warning)", cat({"UPnP: ", lan_router, " UDP 47998 not removed: ConflictInMappingEntry"}), {lan_router}, {lan_router}, {}, {"UDP 47998"}});
    // ---- The debug build's remote log line --------------------------------------------------------------------------------------
    out.push_back({"main_ps5.cpp: remote_log (info), the address the user typed", cat({"remote_log: each log line also goes to ", lan_other, ":5514 (UDP syslog)"}), {lan_other}, {lan_other}, {}, {"UDP syslog"}});
    // ---- What Sunshine dumps at debug level (also reachable in the normal build with min_log_level = debug) ---------------------
    out.push_back({"nvhttp.cpp print_req: Host header with an address", cat({"Host -- ", lan_client, ":47984"}), {lan_client}, {lan_client}, {}, {":47984"}});
    out.push_back({"nvhttp.cpp print_req: Host header with a name (DDNS, MagicDNS, mDNS)", cat({"Host -- ", host_name, ":47984"}), {host_name}, {host_name}, {}, {":47984"}});
    out.push_back({"rtsp.cpp: target URL", cat({"target :: rtsp://", host_name, ":48010"}), {host_name}, {host_name}, {}, {":48010"}});
    out.push_back({"rtsp.cpp: target URL with an address, encrypted RTSP", cat({"target :: rtspenc://", public_a, ":48010"}), {public_a}, {public_a}, {public_a}, {":48010"}});
    out.push_back({"rtsp.cpp: Found Host", cat({"Found Host: ", host_name}), {host_name}, {host_name}, {}, {"Found Host"}});
    out.push_back({"rtsp.cpp: Found Host 0.0.0.0 (what Sunshine reads for the audio quality)", "Found Host: 0.0.0.0", {}, {}, {}, {"0.0.0.0"}});
    out.push_back({"rtsp.cpp: the client's SDP", cat({"o=android 0 14 IN IPv4 ", lan_client}), {lan_client}, {lan_client}, {}, {"o=android 0 14 IN IPv4"}});
    out.push_back({"stream.cpp: Control peer address", cat({"Control peer address [", lan_client, ":50035]"}), {lan_client}, {lan_client}, {}, {":50035]"}});
    out.push_back({"stream.cpp: Control peer address over IPv6", cat({"Control peer address [", public_v6, ":50035]"}), {public_v6}, {public_v6}, {public_v6}, {":50035]"}});
    out.push_back({"stream.cpp: RAISE (mapped IPv4 of a dual-stack socket)", cat({"RAISE: ::ffff:", public_a, ":50035 :: type"}), {public_a}, {public_a}, {public_a}, {"RAISE", ":50035 :: type"}});
    out.push_back({"stream.cpp: Received ping, the payload ties packets to a stream", cat({"Received ping [v2] from ", lan_client, ":50035 [", ping_payload, "]"}), {lan_client}, {lan_client, ping_payload}, {ping_payload}, {"Received ping [v2]"}});
    out.push_back({"stream.cpp: Received ping from a link-local v6 address", cat({"Received ping [v1] from ", link_v6, ":50035 [", ping_payload, "]"}), {link_v6}, {link_v6, ping_payload}, {ping_payload}, {"Received ping [v1]"}});
    out.push_back({"rtsp.cpp: raw RTSP response, ping payload", cat({"X-SS-Ping-Payload: ", ping_payload}), {}, {ping_payload}, {ping_payload}, {"X-SS-Ping-Payload"}});
    out.push_back({"rtsp.cpp: raw RTSP response, connect data", "X-SS-Connect-Data: 305419896", {}, {"305419896"}, {"305419896"}, {"X-SS-Connect-Data"}});
    out.push_back({"nvhttp.cpp print_req: the stream key of /launch", cat({"rikey -- ", stream_key}), {}, {stream_key}, {stream_key}, {"rikey"}});
    out.push_back({"nvhttp.cpp print_req: the client's own id", cat({"uniqueid -- ", client_id}), {client_id}, {client_id}, {}, {"uniqueid"}});
    out.push_back({"nvhttp.cpp print_req: the id of a request", cat({"uuid -- ", request_uuid}), {request_uuid}, {request_uuid}, {}, {"uuid"}});
    out.push_back({"nvhttp.cpp print_req: the device name of a pairing", cat({"devicename -- ", phone}), {phone}, {phone}, {}, {"devicename"}});
    out.push_back({"nvhttp.cpp print_req: pairing handshake", "clientchallenge -- 00112233445566778899aabbccddeeff", {}, {"00112233445566778899aabbccddeeff"}, {"00112233445566778899aabbccddeeff"}, {"clientchallenge"}});
    out.push_back({"nvhttp.cpp: the client's certificate", "-----BEGIN CERTIFICATE-----\nMIIBszCCAVmgAwIBAgIJ\n-----END CERTIFICATE-----\n", {"MIIBszCCAVmgAwIBAgIJ"}, {"MIIBszCCAVmgAwIBAgIJ"}, {"MIIBszCCAVmgAwIBAgIJ"}, {}});
    out.push_back({"web page / confighttp: login headers", "Cookie :: prospero_session=abcdef0123456789", {}, {"abcdef0123456789"}, {"abcdef0123456789"}, {"Cookie"}});
    // ---- Names the report knows (paired devices, the console, the client that streams) ---------------------------------------------
    out.push_back({"a device's name in any line (the report knows the names of the paired devices)", cat({"Stream of ", phone, " ended; ", console_name, " idle"}), {phone, console_name}, {}, {}, {"Stream of", "ended"}});
    out.push_back({"a device's uuid", cat({"device ", phone_uuid, " unpaired"}), {phone_uuid}, {}, {}, {"unpaired"}});
    // ---- Games ----------------------------------------------------------------------------------------------------------------------
    out.push_back({"process_ps5.cpp: Session app (info): the report hides the game, the log keeps it", cat({"Session app [", game, "] marked running"}), {game}, {}, {}, {"marked running"}});
    out.push_back({"process_ps5.cpp: Started / Not starting a title (info)", cat({"Not starting ", title_id, ": CUSA12345 is in front and closing it could lose progress"}), {title_id, "CUSA12345"}, {}, {}, {"Not starting", "is in front"}});
    out.push_back({"the settings app and the system's own titles stay in the report", "Starting PPSA47989 and NPXS40000", {}, {}, {}, {"PPSA47989", "NPXS40000"}});
    // ---- Other shapes ---------------------------------------------------------------------------------------------------------------
    out.push_back({"a MAC address (the console's own is in the serverinfo for paired devices, not in a log; masked wherever it shows up)", cat({"interface mac ", mac}), {mac}, {mac}, {}, {"interface mac"}});
    out.push_back({"Sunshine: the ranges of the LAN, from its own lines", "Info: Configuration UI available at [https://deck.local:47990]", {"deck.local"}, {"deck.local"}, {}, {":47990]"}});
    // ---- What a developer needs stays (nothing private in these) -----------------------------------------------------------------
    out.push_back({"host_diag.log: the capture steps and codes", "ProsperoSunshine: capture OpenVideo rc=0x80c78000 step=Start", {}, {}, {}, {"rc=0x80c78000", "step=Start"}});
    out.push_back({"host_diag.log: the header", "ProsperoSunshine: host version=0.6.0-debug commit=8fa42cd5 firmware=0x13000040 known=1 started=2026-11-10T18:01:13", {}, {}, {}, {"0.6.0-debug", "8fa42cd5", "0x13000040", "2026-11-10T18:01:13"}});
    out.push_back({"host_diag.log: the ports a bind waits on", "ProsperoSunshine: -> bind the RTSP port TCP 48010", {}, {}, {}, {"48010"}});
    out.push_back({"sunshine.log: the stats line of a stream", "Stream stats: stream=1 role=capture channel=0 codec=H.264 hdr=0 mode=1920x1080@60 kbps=20000 sent_kbps=18300 rtt_ms=4 recoveries=0(+0) missing_frames=0(+0) pads=1 secs=25",
                   {}, {}, {}, {"mode=1920x1080@60", "rtt_ms=4", "pads=1"}});
    out.push_back({"sunshine.log: times and counters are not addresses", "[2026-11-10 18:01:13.081]: Info: PS5: Avcap2 passthrough, H.264, HEVC and HEVC HDR, 640x360 to 3840x2160, TSC 1.59627 ticks/ns", {}, {}, {}, {"18:01:13.081", "TSC 1.59627 ticks/ns"}});
    out.push_back({"the report's own Host line is no Host header", "Host: running, streaming, version 0.6.0, commit 8fa42cd5", {}, {}, {}, {"running, streaming", "version 0.6.0"}});
    out.push_back({"the probe's own host line", "host: QUIT answered \"OK bye \", host stopped", {}, {}, {}, {"QUIT answered", "host stopped"}});
    out.push_back({"loopback and wildcard stay (a request from 127.0.0.1 is a forward)", "Control socket on port 47995 (local only), bound to 127.0.0.1, peer 0.0.0.0", {}, {}, {}, {"127.0.0.1", "0.0.0.0"}});
    out.push_back({"Tailscale's address is masked with its kind", cat({"client ", tailnet, " asked"}), {tailnet}, {tailnet}, {}, {"client", "asked"}});
    return out;
}

int failures = 0;

void check(const Sample &sample, const char *sink, const std::string &output, const std::vector<std::string> &forbidden) {
    for (const std::string &value : forbidden)
        if (output.find(value) != std::string::npos) {
            std::printf("FAIL [%s] %s: \"%s\" comes out in: %s\n", sink, sample.kind, value.c_str(), output.c_str());
            ++failures;
        }
    for (const std::string &needed : sample.keep)
        if (output.find(needed) == std::string::npos) {
            std::printf("FAIL [%s] %s: \"%s\" is needed and is gone from: %s\n", sink, sample.kind, needed.c_str(), output.c_str());
            ++failures;
        }
}

}  // namespace

int main() {
    using namespace prospero::logs;
    const auto now = Clock::now();
    for (const Sample &sample : samples()) {
        // The report: what the app and the web page build from the lines of the files.
        {
            psapp::diag::Scrubber scrubber;
            scrubber.add_console(console_name);
            scrubber.add_device(phone, phone_uuid);
            check(sample, "report", scrubber.apply(sample.line), sample.report);
            // The whole report goes through a last pass without names: addresses and ids still go.
            check(sample, "report, last pass", scrubber.apply(sample.line, false), [&] {
                std::vector<std::string> shaped;
                for (const std::string &value : sample.report)
                    if (value != phone && value != console_name && value != game) shaped.push_back(value);
                return shaped;
            }());
        }
        // The debug build's files, and what the report makes of them after that.
        {
            Filter debug({.mask_addresses = true});
            std::string written;
            for (const Record &record : debug.take(1, sample.line, now)) written += record.message;
            check(sample, "debug log", written, sample.debug);
            psapp::diag::Scrubber scrubber;
            scrubber.add_console(console_name);
            scrubber.add_device(phone, phone_uuid);
            std::vector<std::string> both = sample.report;
            both.insert(both.end(), sample.debug.begin(), sample.debug.end());
            check(sample, "debug log, then report", scrubber.apply(written), both);
        }
        // The normal build's files: what the user posts if they post sunshine.log itself.
        {
            Filter normal;
            std::string written;
            for (const Record &record : normal.take(1, sample.line, now)) written += record.message;
            check(sample, "normal log", written, sample.normal);
        }
    }
    // The same address has the same tag in every line of a file, and the tag says what the address was: a developer can follow one client.
    {
        Filter debug({.mask_addresses = true});
        const auto one = debug.take(2, cat({"Rejected connection from [", lan_client, "]: x"}), now);
        const auto two = debug.take(2, cat({"Received ping [v2] from ", lan_client, ":5 [00]"}), now);
        const auto three = debug.take(2, cat({"Pairing refused for ", public_a, ": y"}), now);
        if (one.size() != 1 || two.size() != 1 || three.size() != 1 || one[0].message.find("<lan-1>") == std::string::npos ||
            two[0].message.find("<lan-1>:5") == std::string::npos || three[0].message.find("<public-1>") == std::string::npos) {
            std::puts("FAIL: one address must have one tag in a file, and the tag must name its kind");
            ++failures;
        }
    }
    if (failures) {
        std::printf("%d leaks or losses\n", failures);
        return 1;
    }
    std::puts("PASS: privacy (every kind of log line of the inventory through the report, the debug log and the normal log)");
    return 0;
}
