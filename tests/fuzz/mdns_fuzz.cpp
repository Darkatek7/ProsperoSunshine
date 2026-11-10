// SPDX-License-Identifier: GPL-3.0-only
// mDNS: what any machine on the LAN can send to UDP 5353 (parse_questions, build_response), and the packets the host
// sends back (build_response, build_announcement), which must be well-formed DNS whatever the console's name is.
#include "fuzz_common.hpp"
#include "net/mdns.hpp"

#include <span>
#include <vector>

using namespace prospero::mdns;

namespace {

uint16_t be16(const std::vector<uint8_t> &p, size_t at) { return static_cast<uint16_t>((p[at] << 8) | p[at + 1]); }

// A name of labels without compression, as the host writes it: every label at most 63 bytes, the whole at most 255.
// Advances pos past it; returns the dotted text.
std::string read_plain_name(const std::vector<uint8_t> &p, size_t &pos, size_t limit) {
    std::string name;
    size_t total = 1;
    for (;;) {
        FUZZ_CHECK(pos < limit);
        const uint8_t len = p[pos++];
        if (len == 0) break;
        FUZZ_CHECK(len <= 63);
        FUZZ_CHECK(pos + len <= limit);
        total += 1 + len;
        FUZZ_CHECK(total <= 255);
        if (!name.empty()) name += '.';
        name.append(reinterpret_cast<const char *>(&p[pos]), len);
        pos += len;
    }
    return name;
}

// The packet is a response with the sections it announces and nothing left over.
void check_response(const std::vector<uint8_t> &p) {
    FUZZ_CHECK(p.size() >= 12);
    FUZZ_CHECK(p[2] & 0x80);  // a response: parse_questions must never take it for a query
    FUZZ_CHECK(be16(p, 4) == 0 && be16(p, 8) == 0);
    const size_t records = static_cast<size_t>(be16(p, 6)) + be16(p, 10);
    size_t pos = 12;
    for (size_t i = 0; i < records; ++i) {
        read_plain_name(p, pos, p.size());
        FUZZ_CHECK(pos + 10 <= p.size());
        const uint16_t type = be16(p, pos);
        const size_t rdlength = be16(p, pos + 8);
        pos += 10;
        FUZZ_CHECK(pos + rdlength <= p.size());
        const size_t end = pos + rdlength;
        if (type == type_a) FUZZ_CHECK(rdlength == 4);
        if (type == type_ptr) {
            read_plain_name(p, pos, end);
            FUZZ_CHECK(pos == end);
        }
        if (type == type_srv) {
            FUZZ_CHECK(rdlength >= 7);
            pos += 6;
            read_plain_name(p, pos, end);
            FUZZ_CHECK(pos == end);
        }
        pos = end;
    }
    FUZZ_CHECK(pos == p.size());
    FUZZ_CHECK(parse_questions(p).empty());
}

void put_name(std::vector<uint8_t> &p, const std::string &dotted) {
    size_t from = 0;
    for (;;) {
        const size_t dot = dotted.find('.', from);
        const std::string label = dotted.substr(from, dot == std::string::npos ? std::string::npos : dot - from);
        p.push_back(static_cast<uint8_t>(label.size()));
        p.insert(p.end(), label.begin(), label.end());
        if (dot == std::string::npos) break;
        from = dot + 1;
    }
    p.push_back(0);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider in(data, size);
    Service service;
    service.instance = in.ConsumeRandomLengthString(100);
    service.host = host_label(in.ConsumeRandomLengthString(100));
    service.port = in.ConsumeIntegral<uint16_t>();
    for (auto &octet : service.ipv4) octet = in.ConsumeIntegral<uint8_t>();

    FUZZ_CHECK(host_label(service.host) == service.host || service.host.empty());
    const std::string label = instance_label(service.instance);
    FUZZ_CHECK(label.size() <= 63);
    FUZZ_CHECK(service.instance.starts_with(label));
    {  // a cut never lands inside a UTF-8 sequence unless the input was not UTF-8 there to begin with
        const std::string_view whole = service.instance;
        if (whole.size() > 63 && label.size() < whole.size()) FUZZ_CHECK((static_cast<uint8_t>(whole[label.size()]) & 0xc0) != 0x80 || label.empty());
    }

    check_response(build_announcement(service, false));
    check_response(build_announcement(service, true));

    // A question we know the answer to, written the way a resolver writes it, comes back out of parse_questions and gets
    // an answer.
    const bool any = in.ConsumeBool();
    const std::string names[] = {"_nvstream._tcp.local", "_services._dns-sd._udp.local", label + "._nvstream._tcp.local",
                                 service.host + ".local"};
    const std::string asked = names[in.ConsumeIntegralInRange<size_t>(0, 3)];
    const uint16_t type = any ? type_any : in.ConsumeIntegralInRange<uint16_t>(0, 300);
    std::vector<uint8_t> query{0x12, 0x34, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    put_name(query, asked);
    query.insert(query.end(), {static_cast<uint8_t>(type >> 8), static_cast<uint8_t>(type), 0x80, 1});
    const auto questions = parse_questions(query);
    // An instance with a dot or an empty label breaks the dotted form: only compare when it can be read back.
    if (asked.find("..") == std::string::npos && label.find('.') == std::string::npos && !label.empty()) {
        FUZZ_CHECK(questions.size() == 1);
        FUZZ_CHECK(questions[0].name == fuzz::lower(asked) && questions[0].type == type);
    }
    const auto answer = build_response(service, questions);
    if (!answer.empty()) check_response(answer);

    // Whatever else arrives: anything goes in, a well-formed packet or nothing comes out.
    const auto raw = in.ConsumeRemainingBytes<uint8_t>();
    const auto parsed = parse_questions(raw);
    FUZZ_CHECK(parsed.size() <= 65535);
    for (const auto &q : parsed) {
        FUZZ_CHECK(q.name.size() <= 255 + 64);
        FUZZ_CHECK(fuzz::lower(q.name) == q.name);
    }
    const auto reply = build_response(service, parsed);
    if (!reply.empty()) check_response(reply);
    return 0;
}
