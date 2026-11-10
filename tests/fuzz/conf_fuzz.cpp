// SPDX-License-Identifier: GPL-3.0-only
// The app's view of sunshine.conf and the host's answers (app/src/psapp/conf.cpp, web.cpp): the file may hold anything a
// person typed, and the app writes it back with only its own keys changed.
#include "fuzz_common.hpp"
#include "app/psapp/conf.hpp"
#include "app/psapp/web.hpp"

#include <algorithm>
#include <string_view>

using namespace psapp;

namespace {

bool key_char(char c) { return (c >= 'a' && c <= 'z') || c == '_'; }

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider in(data, size);
    const std::string text = in.ConsumeRandomLengthString(400);
    Config config = Config::parse(text);

    // Reading the text the app would write back gives the same values for every key it offers.
    const Config again = Config::parse(config.text());
    for (const OptionSpec &spec : option_specs()) {
        FUZZ_CHECK(config.get(spec.key) == again.get(spec.key));
        (void)option_value(config, spec);
    }

    // Changing one option: its value is the new one, and no other offered option changed.
    const auto &specs = option_specs();
    const OptionSpec &spec = specs[in.ConsumeIntegralInRange<size_t>(0, specs.size() - 1)];
    std::string value = in.ConsumeRandomLengthString(70);
    if (spec.kind == OptionKind::choice && !spec.choices.empty() && in.ConsumeBool())
        value = spec.choices[in.ConsumeIntegralInRange<size_t>(0, spec.choices.size() - 1)].value;
    if (spec.kind == OptionKind::toggle && in.ConsumeBool()) value = in.ConsumeBool() ? "1" : "0";
    if (spec.kind == OptionKind::number && in.ConsumeBool()) value = std::to_string(in.ConsumeIntegralInRange<long>(spec.min - 2, spec.max + 2));
    const auto problem = validate(spec, value);
    if (!problem) {
        FUZZ_CHECK(!value.empty() && value.find('\n') == std::string::npos && value.find('#') == std::string::npos);
        if (spec.kind == OptionKind::number) {
            FUZZ_CHECK(value.find_first_not_of("0123456789") == std::string::npos && std::stol(value) >= spec.min && std::stol(value) <= spec.max);
        }
        std::vector<std::optional<std::string>> before;
        for (const OptionSpec &other : specs) before.push_back(config.get(other.key));
        config.set(spec.key, value);
        const Config saved = Config::parse(config.text());
        FUZZ_CHECK(config.get(spec.key) == value && saved.get(spec.key) == value);
        for (size_t i = 0; i < specs.size(); ++i)
            if (std::string_view(specs[i].key) != spec.key) FUZZ_CHECK(saved.get(specs[i].key) == before[i]);
        config.set(spec.key, std::nullopt);
        FUZZ_CHECK(!config.get(spec.key) && !Config::parse(config.text()).get(spec.key));
    } else {
        FUZZ_CHECK(!problem->empty());
    }
    // Any other key the file has can be set and removed too.
    std::string key = in.ConsumeRandomLengthString(12);
    key.erase(std::remove_if(key.begin(), key.end(), [](char c) { return !key_char(c); }), key.end());
    if (!key.empty()) {
        config.set(key, "v1");
        FUZZ_CHECK(Config::parse(config.text()).get(key) == "v1");
    }

    // What the host answers to WEBLOGIN, and its ports and addresses.
    const std::string line = in.ConsumeRandomLengthString(80);
    if (const auto state = web::parse_state(line)) {
        FUZZ_CHECK(!state->user.empty() || !state->enabled);
        FUZZ_CHECK(line.starts_with("OK user="));
    }
    const auto outcome = web::parse_outcome(line);
    FUZZ_CHECK(!(outcome.ok && outcome.unknown));
    const auto port = web::port(in.ConsumeBool() ? std::optional<std::string>(line) : std::nullopt);
    FUZZ_CHECK(port >= 2 && port <= 65535);
    (void)web::address(line, port);
    const std::string user = in.ConsumeRandomLengthString(40), password = in.ConsumeRandomLengthString(140);
    FUZZ_CHECK(web::valid_user(user) == web::user_problem(user).empty());
    FUZZ_CHECK(web::valid_password(password) == web::password_problem(password).empty());
    if (web::valid_user(user) && web::valid_password(password)) {
        // One control line, whose first two words are the command and the user.
        const std::string request = web::set_request(user, password);
        FUZZ_CHECK(request.find('\n') == request.size() - 1);
        FUZZ_CHECK(request == "WEBLOGIN SET " + user + " " + password + "\n");
        FUZZ_CHECK(request.find(' ', 13) == 13 + user.size());
    }
    return 0;
}
