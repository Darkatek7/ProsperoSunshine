// SPDX-License-Identifier: GPL-3.0-only
// UPnP decisions in src/net/upnp.hpp, fed with what a router (or anything that claims to be one) answers: addresses, a
// description URL, the lease it reports and the entry it says exists. The SSDP and SOAP/XML parsing itself is miniupnpc's,
// a third-party library built from deps.lock, and is not reachable without it.
#include "fuzz_common.hpp"
#include "net/upnp.hpp"

using namespace prospero::upnp;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider in(data, size);
    const std::string a = in.ConsumeRandomLengthString(40), b = in.ConsumeRandomLengthString(40);
    const std::string c = in.ConsumeRandomLengthString(80), d = in.ConsumeRandomLengthString(40);
    const int error = in.ConsumeIntegral<int>();

    // One reader of dotted addresses must agree with the other about what is one.
    const auto priv = private_ipv4(a);
    const int same = shared_prefix(a, a);
    FUZZ_CHECK(priv.has_value() == (same == 32));
    FUZZ_CHECK(public_ipv4(a) == (priv.has_value() && !*priv));
    const int bits = shared_prefix(a, b);
    FUZZ_CHECK(bits >= -1 && bits <= 32);
    FUZZ_CHECK(bits == shared_prefix(b, a));
    if (bits >= 0) FUZZ_CHECK(private_ipv4(a) && private_ipv4(b));

    const std::string host = url_host(c);
    FUZZ_CHECK(host.find_first_of(":/") == std::string::npos);
    FUZZ_CHECK(host.size() <= c.size());

    const uint16_t expected_port = in.ConsumeIntegral<uint16_t>();
    const std::string internal_port = in.ConsumeBool() ? std::to_string(expected_port) : b;
    const std::string label = in.ConsumeBool() ? c : std::string(label_prefix) + "HTTP";
    const Existing existing = classify(error, a, internal_port, c, d, expected_port, label);
    if (error == 714) FUZZ_CHECK(existing == Existing::none);
    if (error != 0 && error != 714) FUZZ_CHECK(existing == Existing::unknown);
    if (error == 0 && a.empty() && internal_port.empty()) FUZZ_CHECK(existing == Existing::none);
    if (host_label(c)) FUZZ_CHECK(c.starts_with(label_prefix) && c.size() > label_prefix.size());
    if (existing == Existing::mine)
        FUZZ_CHECK(c == label && !label.empty() && a == d && !a.empty() && internal_port == std::to_string(expected_port));

    if (renew_by_removing(a)) {
        FUZZ_CHECK(!a.empty() && a.size() <= 10);
        for (const char ch : a) FUZZ_CHECK(ch >= '0' && ch <= '9');
    }

    std::vector<std::string> hosts;
    while (in.remaining_bytes() > 0 && hosts.size() < 8) hosts.push_back(in.ConsumeRandomLengthString(20));
    const int outer = outer_router(a, b, hosts);
    FUZZ_CHECK(outer >= -1 && outer < static_cast<int>(hosts.size()));
    if (outer >= 0) {
        FUZZ_CHECK(hosts[static_cast<size_t>(outer)] != b);
        FUZZ_CHECK(shared_prefix(a, hosts[static_cast<size_t>(outer)]) >= 16);
    }

    PortStatus port;
    port.mapping = {in.ConsumeIntegral<uint16_t>(), in.ConsumeBool(), "x"};
    port.outcome = static_cast<Outcome>(in.ConsumeIntegralInRange<int>(0, 3));
    port.detail = c;
    port.added = in.ConsumeBool();
    FUZZ_CHECK(!describe(port).empty());
    FUZZ_CHECK(name(port.outcome)[0] != '\0' && name(static_cast<State>(in.ConsumeIntegralInRange<int>(0, 3)))[0] != '\0');
    return 0;
}
