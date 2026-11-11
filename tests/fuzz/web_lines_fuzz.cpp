// SPDX-License-Identifier: GPL-3.0-only
// The web page turns the control socket's answer lines into records (src/web/web_lines.hpp). The answers carry device names
// that a client chose, so the host's own line formats must come back through the readers intact.
#include "fuzz_common.hpp"
#include "control/stream_list.hpp"
#include "web/web_lines.hpp"

using namespace prospero::web;

namespace {

// The text with every record's keys, so that a reader's output is looked at as a whole.
size_t touch(const Record &record) {
    size_t n = 0;
    for (const auto &[key, value] : record) n += key.size() + value.size();
    return n;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider in(data, size);
    // The rate in a STREAMS line is an int, whatever the counters say.
    {
        const auto bytes = in.ConsumeIntegral<unsigned long long>();
        const double seconds = in.ConsumeBool() ? in.ConsumeFloatingPointInRange<double>(0.0, 20.0) : in.ConsumeFloatingPoint<double>();
        const int rate = prospero::sent_rate_kbps(bytes, seconds);
        FUZZ_CHECK(rate >= -1);
        if (!(seconds >= 0.5)) FUZZ_CHECK(rate == -1);
    }
    const std::string answer = in.ConsumeRandomLengthString(400);
    const std::string last = in.ConsumeRandomLengthString(10);
    const std::string text = in.ConsumeRemainingBytesAsString();

    for (const std::string &line : ok_lines(answer)) FUZZ_CHECK(line.find('\n') == std::string::npos);
    FUZZ_CHECK(ok_lines(answer).size() <= static_cast<size_t>(std::count(answer.begin(), answer.end(), '\n')) + 1);
    size_t total = 0;
    for (const std::string *line : {&answer, &text}) {
        total += touch(fields(*line, last)) + touch(status(*line)) + touch(device(*line)) + touch(pairing(*line)) +
                 touch(console_user(*line)) + touch(device_profiles(*line)) + touch(app_entry(*line));
    }
    (void)total;
    for (size_t limit : {size_t{0}, size_t{1}, size_t{7}, size_t{100}}) {
        const std::string cut = tail(text, limit);
        FUZZ_CHECK(cut.size() <= limit);
        FUZZ_CHECK(text.ends_with(cut));
        if (text.size() > limit) FUZZ_CHECK(cut.size() == limit || text[text.size() - cut.size() - 1] == '\n');
    }
    // PAIRINGS and DEVICES put a name in the middle or at the end: the readers give back what was put in.
    {
        std::string name = text.substr(0, 40);
        for (char &c : name)
            if (c == '\n' || c == '\r') c = ' ';
        const Record device_record = device("0123 1 1280x720@60 " + name);
        FUZZ_CHECK(device_record.at("name") == name && device_record.at("mode") == "1280x720@60" && device_record.at("streaming") == "1");
        const Record app = app_entry("PPSA12345 shown " + name);
        FUZZ_CHECK(app.at("name") == name && app.at("state") == "shown");
        const Record user = console_user("1a2b " + name);
        FUZZ_CHECK(user.at("name") == name);
    }
    // The STREAMS line of the host, with a name of any bytes, is one line and its name comes out whole.
    {
        prospero::StreamFacts facts;
        facts.address = "192.168.1.20";
        facts.width = static_cast<int>(size % 5000);
        facts.height = static_cast<int>(size % 3000);
        facts.fps = static_cast<int>(size % 200);
        facts.kbps = static_cast<int>(size * 1000);
        facts.seconds = static_cast<long long>(size) * 77;
        const std::string name = text.substr(0, 60);
        const std::string line = prospero::stream_line(facts, {}, "uuid-1", name);
        FUZZ_CHECK(line.find('\n') == std::string::npos && line.find('\r') == std::string::npos);
        const auto lines = ok_lines(line + "\nEND\n");
        FUZZ_CHECK(lines.size() == 1);
        const Record record = fields(lines[0], "name");
        std::string clean = name;
        for (char &c : clean)
            if (c == '\n' || c == '\r') c = ' ';
        FUZZ_CHECK(record.at("name") == clean);
        FUZZ_CHECK(record.at("uuid") == "uuid-1" && record.at("address") == "192.168.1.20");
    }
    return 0;
}
