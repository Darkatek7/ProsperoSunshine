// SPDX-License-Identifier: GPL-3.0-only
// What happens to a log record before it is written (src/log/log_filter.hpp, app/src/psapp/mask.hpp) and the remote log's
// address (src/log/remote_log.hpp). A record can hold anything a client sent: query arguments, headers, a certificate.
#include "fuzz_common.hpp"
#include "log/log_filter.hpp"
#include "log/remote_log.hpp"

#include <algorithm>
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

    // The masker keeps its promises for text of its own kinds: an address of any kind comes out as the tag of its kind, whatever
    // the text around it.
    {
        psapp::mask::Masker masker;
        const std::string masked = masker.apply(record);
        FUZZ_CHECK(masked.size() <= record.size() * 2 + 8);
        const auto octet = [&] { return in.ConsumeIntegralInRange<int>(0, 255); };
        const std::string around_before = in.ConsumeBool() ? "from " : "[", around_after = in.ConsumeBool() ? " ok" : ":5]";
        // IPv4: private, Tailscale, link-local and public ranges, as the tag says.
        {
            int a = octet(), b = octet();
            std::string expected;
            if (a == 0 || a == 127 || a >= 224) expected = "";
            else if (a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168)) expected = "<lan-";
            else if (a == 100 && b >= 64 && b <= 127) expected = "<tailscale-";
            else if (a == 169 && b == 254) expected = "<link-";
            else expected = "<public-";
            const std::string ip = std::to_string(a) + "." + std::to_string(b) + "." + std::to_string(octet()) + "." + std::to_string(octet());
            const std::string out = masker.apply(around_before + ip + around_after);
            if (expected.empty()) FUZZ_CHECK(out == around_before + ip + around_after);
            else FUZZ_CHECK(out.rfind(around_before + expected, 0) == 0 && out.find(ip) == std::string::npos && out.ends_with(">" + around_after));
            // The same address, the same tag: in the mapped form of a dual-stack socket too.
            if (!expected.empty()) FUZZ_CHECK(masker.apply("::ffff:" + ip) == masker.apply(ip));
        }
        // IPv6 in each range, written in full or as Sunshine writes a peer (address:port, no brackets).
        {
            static constexpr const char *tags[] = {"<public6-", "<lan6-", "<link6-", "<tailscale6-"};
            const size_t kind = in.ConsumeIntegralInRange<size_t>(0, 3);
            const auto group = [&](int first_digit) {
                std::string g;
                for (int i = 0; i < 4; ++i) g += "0123456789abcdef"[i == 0 && first_digit >= 0 ? first_digit : in.ConsumeIntegralInRange<int>(0, 15)];
                return g;
            };
            std::string ip;
            switch (kind) {
            case 0: ip = group(in.ConsumeIntegralInRange<int>(2, 3)); break;                    // 2000::/3
            case 1: ip = "fd" + group(-1).substr(0, 2); break;                                 // fd00::/8
            case 2: ip = "fe80"; break;                                                        // link-local
            default: ip = "fd7a:115c:a1e0:" + group(-1) + ":" + group(-1) + ":" + group(-1) + ":" + group(-1) + ":" + group(-1); break;
            }
            while (std::count(ip.begin(), ip.end(), ':') < 7) ip += ":" + group(1);
            // With a port the text goes on with a space: an address, ":50123" and ":5]" is no form of a peer.
            const bool with_port = in.ConsumeBool();
            const std::string text = around_before + ip + (with_port ? ":50123 ok" : around_after);
            const std::string out = masker.apply(text);
            FUZZ_CHECK(out.find(ip) == std::string::npos);
            FUZZ_CHECK(out.rfind(around_before + tags[kind], 0) == 0);
        }
        const char *sep = in.ConsumeBool() ? ":" : "-";
        std::string mac;
        for (int i = 0; i < 6; ++i) {
            if (i) mac += sep;
            for (int k = 0; k < 2; ++k) mac += "0123456789abcdefABCDEF"[in.ConsumeIntegralInRange<int>(0, 21)];
        }
        psapp::mask::Masker fresh;  // the numbers of `masker` depend on the record
        FUZZ_CHECK(fresh.apply("mac " + mac + " end") == "mac <mac-1> end");
        // A host name behind "Host" or in a URL.
        std::string label;
        for (int i = in.ConsumeIntegralInRange<int>(1, 10); i > 0; --i) label += "abcdefghijklmnopqrstuvwxyz-"[in.ConsumeIntegralInRange<int>(0, 25)];
        const std::string name = label + ".example.org";
        FUZZ_CHECK(masker.apply("Host -- " + name + ":47984").find(name) == std::string::npos);
        FUZZ_CHECK(masker.apply("target :: rtsp://" + name + ":48010").find(name) == std::string::npos);
    }
    // A public address never reaches a file of the normal build either; the home's stays as it is.
    {
        Filter normal;
        const auto now = Clock::time_point{} + std::chrono::hours(1);
        const std::string before = in.ConsumeRandomLengthString(20), after = in.ConsumeRandomLengthString(20);
        std::string message = before + " 203.0.113.99 " + after;
        const auto out = normal.take(2, message, now);
        FUZZ_CHECK(out.size() == 1);
        FUZZ_CHECK(out[0].message.find(" 203.0.113.99 ") == std::string::npos);
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
