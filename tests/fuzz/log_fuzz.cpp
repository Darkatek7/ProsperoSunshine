// SPDX-License-Identifier: GPL-3.0-only
// What happens to a log record before it is written (src/log/log_filter.hpp, src/probe_scrub.hpp) and the remote log's
// address (src/log/remote_log.hpp). A record can hold anything a client sent: query arguments, headers, a certificate.
#include "fuzz_common.hpp"
#include "log/log_filter.hpp"
#include "log/remote_log.hpp"
#include "probe_scrub.hpp"

#include <chrono>

using namespace prospero::logs;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider in(data, size);
    const std::string record = in.ConsumeRandomLengthString(400);

    // A certificate never reaches the log, wherever it starts or ends.
    const std::string hidden = hide_secrets(record);
    FUZZ_CHECK(hidden.find("-----BEGIN ") == std::string::npos);
    // A secret argument's value is gone: a line "<name> -- <value>" of any value and spelling of the name keeps only the name.
    {
        static constexpr const char *names[] = {"rikey", "RiKeyId", "salt", "clientcert", "PIN", "password", "Authorization", "COOKIE", "set-cookie"};
        const std::string name = names[in.ConsumeIntegralInRange<size_t>(0, std::size(names) - 1)];
        std::string value = in.ConsumeRandomLengthString(60);
        value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
        const std::string separator = in.ConsumeBool() ? " -- " : " :: ";
        const std::string before = in.ConsumeRandomLengthString(30);
        const std::string line = name + separator + value;
        const std::string out = hide_secrets(line);
        if (value.find("-----BEGIN ") == std::string::npos) FUZZ_CHECK(out == name + separator + "<hidden>");
        // On the line after another one.
        std::string earlier = before;
        earlier.erase(std::remove(earlier.begin(), earlier.end(), '\n'), earlier.end());
        if (value.find("-----BEGIN ") == std::string::npos && earlier.find("-----BEGIN ") == std::string::npos) {
            const std::string two = hide_secrets(earlier + "\n" + line);
            FUZZ_CHECK(two.ends_with("\n" + name + separator + "<hidden>"));
        }
    }
    FUZZ_CHECK(line_kind(record).size() <= 160);
    (void)hide_line(record);

    // The scrub of addresses keeps its promises for text of its own kinds.
    const std::string scrubbed = probe::scrub(record);
    FUZZ_CHECK(scrubbed.size() <= record.size() * 2 + 8);
    {
        std::string ip;
        for (int i = 0; i < 4; ++i) ip += (i ? "." : "") + std::to_string(in.ConsumeIntegralInRange<int>(0, 255));
        FUZZ_CHECK(probe::scrub("from " + ip + " ok") == "from <ip> ok");
        const char *sep = in.ConsumeBool() ? ":" : "-";
        std::string mac;
        for (int i = 0; i < 6; ++i) {
            if (i) mac += sep;
            for (int k = 0; k < 2; ++k) mac += "0123456789abcdefABCDEF"[in.ConsumeIntegralInRange<int>(0, 21)];
        }
        FUZZ_CHECK(probe::scrub("mac " + mac + " end") == "mac <mac> end");
    }

    // The filter over a run of records: nothing secret comes out, and a flood is held back with its summary.
    {
        Options options;
        options.mask_addresses = in.ConsumeBool();
        options.limit = in.ConsumeBool();
        options.burst = in.ConsumeIntegralInRange<int>(0, 5);
        options.window = std::chrono::milliseconds(in.ConsumeIntegralInRange<int>(1, 3000));
        options.kinds = in.ConsumeIntegralInRange<size_t>(1, 6);
        Filter filter(options);
        auto now = Clock::time_point{} + std::chrono::hours(1);
        size_t in_count = 0;
        while (in.remaining_bytes() > 0 && in_count < 300) {
            now += std::chrono::milliseconds(in.ConsumeIntegralInRange<int>(0, 1500));
            std::string message = in.ConsumeRandomLengthString(80);
            if (in.ConsumeIntegralInRange<int>(0, 7) == 0) message = "frame " + std::to_string(in.ConsumeIntegral<int>()) + " lost";
            if (in.ConsumeIntegralInRange<int>(0, 15) == 0) filter.flush_next();
            const auto records = filter.take(in.ConsumeIntegralInRange<int>(0, 5), message, now);
            ++in_count;
            if (!options.limit) FUZZ_CHECK(records.size() == 1);
            FUZZ_CHECK(records.size() <= options.kinds + 1);
            for (const auto &r : records) FUZZ_CHECK(r.message.find("-----BEGIN ") == std::string::npos);
        }
    }

    // remote_log = <address>[:<port>] as typed into sunshine.conf.
    {
        const std::string text = in.ConsumeRandomLengthString(40);
        if (const auto remote = parse_remote(text)) {
            FUZZ_CHECK(remote->port != 0);
            const uint32_t address = ntohl(remote->address.s_addr);
            FUZZ_CHECK(address != 0 && address != 0xffffffffu && (address >> 28) != 0xe);
        }
        const std::string datagram = syslog_datagram(in.ConsumeIntegral<int>(), "tag", record);
        FUZZ_CHECK(datagram.size() <= 1024 && datagram.starts_with("<") && datagram.find('>') != std::string::npos);
        const int priority = syslog_priority(in.ConsumeIntegral<int>());
        FUZZ_CHECK(priority >= 8 + 2 && priority <= 8 + 7);
    }
    return 0;
}
