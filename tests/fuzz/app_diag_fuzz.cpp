// SPDX-License-Identifier: GPL-3.0-only
// The app's readers of what the host writes and says (app/src/psapp/diag.cpp, autoload.cpp): host_diag.log, the DEVICES and
// STREAMS lines, and the Payload Manager's autoload files. The host's own STREAMS line must come back through the app's reader.
#include "fuzz_common.hpp"
#include "app/psapp/autoload.hpp"
#include "app/psapp/diag.hpp"
#include "control/stream_list.hpp"

#include <algorithm>
#include <cctype>
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
        if (name.size() >= 3 && name.find("<") == std::string::npos && log.find('<') == std::string::npos && name.find('\0') == std::string::npos && name.find('\1') == std::string::npos) {
            // The name is hidden whatever its case, except where the scrub's own tags (<lan-1>, <name-1>, ...) happen to spell it.
            std::string lower, wanted = name;
            bool in_tag = false;
            for (const char c : scrubbed) {
                if (c == '<') in_tag = true;
                if (!in_tag) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (c == '>' && in_tag) {
                    in_tag = false;
                    lower += '\1';  // a tag is a gap: what stands on both sides of it is not one word
                }
            }
            for (char &c : wanted) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            FUZZ_CHECK(lower.find(wanted) == std::string::npos);
        }
        FUZZ_CHECK(diag::scrub("host 192.168.1.77 and " + std::string(40, 'a'), {}).find("192.168.1.77") == std::string::npos);
    }
    // One Scrubber for a whole report: an address of any kind is gone from the text, from its tail cut to any width, and the
    // same address has the same tag in the next call.
    {
        diag::Scrubber scrubber;
        const std::string device = in.ConsumeRandomLengthString(10);
        scrubber.add_device(device, in.ConsumeRandomLengthString(40));
        scrubber.add_console(in.ConsumeRandomLengthString(10));
        std::string before = in.ConsumeRandomLengthString(30), after = in.ConsumeRandomLengthString(30);
        // The planted addresses must be gone; the same text made up by the fuzzer around them, glued to a word ("x2001:db8:aa::7"
        // or "2001:db8:aa::7lot"), isn't an address the masker can know.
        for (std::string *piece : {&before, &after})
            for (const char *planted : {"203.0.113", "2001:db8:aa::7", "00:11:22:33:44:55"})
                for (std::string::size_type at; (at = piece->find(planted)) != std::string::npos;) piece->erase(at, 1);
        const std::string text = before + "\nfrom 203.0.113.9 and [2001:db8:aa::7]:50123 and 00:11:22:33:44:55 then\n" + after;
        const std::string out = scrubber.apply(text);
        FUZZ_CHECK(out.find("203.0.113.9") == std::string::npos && out.find("2001:db8:aa::7") == std::string::npos &&
                   out.find("00:11:22:33:44:55") == std::string::npos);
        const std::size_t width = in.ConsumeIntegralInRange<std::size_t>(0, 60);
        const std::string cut = scrubber.tail(text, in.ConsumeIntegralInRange<std::size_t>(1, 6), width);
        FUZZ_CHECK(cut.find("203.0.113.9") == std::string::npos);
        FUZZ_CHECK(cut.empty() || cut.back() == '\n');
        FUZZ_CHECK(scrubber.apply("x 203.0.113.9 y") == scrubber.apply("x 203.0.113.9 y"));
        FUZZ_CHECK(scrubber.apply("x 203.0.113.9 y", false).find("203.0.113.9") == std::string::npos);
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
