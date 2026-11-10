// SPDX-License-Identifier: GPL-3.0-only
// The app's readers of what the host writes and says (app/src/psapp/diag.cpp, autoload.cpp): host_diag.log, the DEVICES and
// STREAMS lines, and the Payload Manager's autoload files. The host's own STREAMS line must come back through the app's reader.
#include "fuzz_common.hpp"
#include "app/psapp/autoload.hpp"
#include "app/psapp/diag.hpp"
#include "control/stream_list.hpp"

#include <algorithm>
#include <vector>

using namespace psapp;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider in(data, size);
    const std::uint32_t firmware = in.ConsumeBool() ? 0x13000040u : in.ConsumeIntegral<std::uint32_t>();
    const std::string log = in.ConsumeRandomLengthString(600);
    const bool running = in.ConsumeBool();

    const diag::HostLog parsed = diag::parse_host_log(log);
    FUZZ_CHECK(parsed.attempts.size() <= diag::kMaxAttempts);
    (void)diag::capture_summary(parsed);
    (void)diag::experimental_report(parsed, {in.ConsumeRandomLengthString(10), "upnp"});
    (void)diag::notice(firmware, parsed, running);
    (void)diag::firmware_text(firmware);
    (void)diag::experimental_needed(firmware, in.ConsumeRandomLengthString(20));

    // The report is meant to be posted: a name given to the scrub, the addresses and long hex runs are gone from it.
    {
        const std::string name = in.ConsumeRandomLengthString(12);
        const std::string scrubbed = diag::scrub(log + " " + name, {name, "ab"});
        if (name.size() >= 3 && name.find("<name>") == std::string::npos && log.find("<name>") == std::string::npos)
            FUZZ_CHECK(scrubbed.find(name) == std::string::npos || name.find_first_of("<>") != std::string::npos || log.find('<') != std::string::npos ||
                       scrubbed.find("<name>") != std::string::npos);
        FUZZ_CHECK(diag::scrub("host 192.168.1.77 and " + std::string(40, 'a'), {}).find("192.168.1.77") == std::string::npos);
    }
    const std::string cut = diag::tail(log, in.ConsumeIntegralInRange<std::size_t>(0, 5), in.ConsumeIntegralInRange<std::size_t>(0, 30));
    FUZZ_CHECK(cut.empty() || cut.back() == '\n');
    (void)diag::duration_text(in.ConsumeIntegral<long long>());
    (void)diag::errno_name(in.ConsumeIntegral<int>());

    // Lines the host answers with.
    const std::string line = in.ConsumeRandomLengthString(200);
    diag::Device device;
    (void)diag::parse_device_line(line, &device);
    (void)diag::device_label(device);
    (void)diag::device_info(device);
    diag::Stream stream;
    if (diag::parse_stream_line(line, &stream)) {
        (void)diag::stream_label(stream);
        (void)diag::stream_info(stream);
        (void)diag::stream_network(stream);
    }
    (void)diag::pairing_label(line);
    const std::string cleaned = diag::clean_device_name(line);
    FUZZ_CHECK(cleaned.find('\n') == std::string::npos && cleaned.find('\r') == std::string::npos);
    FUZZ_CHECK(diag::clean_device_name(cleaned) == cleaned);
    (void)diag::suggested_device_name(line, {in.ConsumeRandomLengthString(10), "Moonlight 1"});
    (void)diag::parse_deny(line);
    (void)diag::deny_request(line);

    // The host's STREAMS line as the app reads it.
    {
        prospero::StreamFacts facts;
        facts.address = in.ConsumeBool() ? "192.168.1.20" : "::ffff:10.0.0.5";
        facts.width = in.ConsumeIntegral<int>();
        facts.height = in.ConsumeIntegral<int>();
        facts.fps = in.ConsumeIntegral<int>();
        facts.kbps = in.ConsumeIntegral<int>();
        facts.video_format = in.ConsumeIntegralInRange<int>(0, 2);
        facts.hdr = in.ConsumeBool();
        facts.seconds = in.ConsumeIntegralInRange<long long>(-5, 99999999999999LL);  // the app reads at most 15 digits
        facts.pads = in.ConsumeIntegralInRange<int>(-2, 300);
        facts.sent_kbps = in.ConsumeIntegralInRange<int>(-1, 100000);
        facts.rtt_ms = in.ConsumeIntegralInRange<int>(-1, 5000);
        facts.recoveries = in.ConsumeIntegral<int>();
        std::string name = in.ConsumeRandomLengthString(40);
        const std::string hostline = prospero::stream_line(facts, {}, "uuid-1", name);
        diag::Stream read;
        FUZZ_CHECK(diag::parse_stream_line(hostline, &read));
        FUZZ_CHECK(read.id == "uuid-1" && read.codec == prospero::codec_name(facts.video_format));
        FUZZ_CHECK(read.kbps == std::max(0, facts.kbps) && read.seconds == std::max(0LL, facts.seconds) && read.pads == std::min(std::max(0, facts.pads), 99));
        FUZZ_CHECK(read.sent_kbps == (facts.sent_kbps < 0 ? -1 : facts.sent_kbps) && read.rtt_ms == (facts.rtt_ms < 0 ? -1 : facts.rtt_ms));
        FUZZ_CHECK(read.recoveries == std::max(0, facts.recoveries));
        FUZZ_CHECK(read.role == "starting");
        for (char &c : name)
            if (c == '\n' || c == '\r') c = ' ';
        FUZZ_CHECK(read.name == name);
        (void)diag::stream_info(read);
        (void)diag::stream_network(read);
    }

    // Payload Manager's lists.
    {
        const std::string list = in.ConsumeRandomLengthString(200), config = in.ConsumeRandomLengthString(200);
        std::string key = in.ConsumeBool() ? std::string(autoload::kSlotKey) : in.ConsumeRandomLengthString(12);
        key.erase(std::remove_if(key.begin(), key.end(), [](char c) { return c == '\n' || c == '\r' || c == ' ' || c == '\t'; }), key.end());
        if (!key.empty()) {
            const std::string with = autoload::with_entry(list, key);
            FUZZ_CHECK(autoload::contains(with, key));
            FUZZ_CHECK(autoload::with_entry(with, key) == with);
            FUZZ_CHECK(!autoload::contains(autoload::without_entry(with, key), key));
        }
        (void)autoload::manager_autoload_enabled(config);
        (void)autoload::slot_meta(in.ConsumeRandomLengthString(20), in.ConsumeRandomLengthString(20));
    }
    return 0;
}
