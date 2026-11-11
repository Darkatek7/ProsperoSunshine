// SPDX-License-Identifier: GPL-3.0-only
// keyboard_profiles.txt, the player's own list of keyboard and mouse profiles (src/input/title_profile.hpp), and the title
// names it is matched with.
#include "fuzz_common.hpp"
#include "input/title_profile.hpp"

#include <vector>

using namespace prospero;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider in(data, size);
    const std::string existing = in.ConsumeRandomLengthString(300);
    const std::string name = in.ConsumeRandomLengthString(40);

    const ProfileList list = parse_profile_list(existing);
    size_t lines = 1;
    for (const char c : existing) lines += c == '\n';
    FUZZ_CHECK(list.problems.size() <= lines);
    for (const auto &[id, profile] : list.titles) FUZZ_CHECK(console_title_id(id) && profile == fuzz::lower(profile));
    for (const auto &[profile_name_, own] : list.own) {
        FUZZ_CHECK(own && !profile_name_.empty() && own->name == profile_name_ && !parse_profile(profile_name_));
    }
    // Every listed title gets a profile; a name that names nothing falls back to the chosen one.
    for (const auto &[id, profile] : list.titles) {
        const ProfileChoice choice = choose_profile(id, in.ConsumeIntegral<uint32_t>(), name, list, Profile::standard, in.ConsumeBool());
        if (choice.own) FUZZ_CHECK(choice.source == ProfileSource::list && list.own.contains(choice.own->name));
        (void)source_name(choice.source);
        (void)profile_name(choice.profile);
    }
    (void)choose_profile(in.ConsumeRandomLengthString(12), in.ConsumeIntegral<uint32_t>(), name, list, Profile::standard);

    // The names the file may use for keys and buttons, and the match name of a title.
    for (const std::string &word : {name, in.ConsumeRandomLengthString(8)}) {
        for (const uint16_t code : key_codes(word)) FUZZ_CHECK(code != 0);
        (void)control_by_name(word);
        (void)title_key(word);
        const std::string matched = match_name(word);
        FUZZ_CHECK(match_name(matched) == matched);
        FUZZ_CHECK(matched.find("  ") == std::string::npos && (matched.empty() || (matched.front() != ' ' && matched.back() != ' ')));
        for (const char c : matched) FUZZ_CHECK((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ');
        FUZZ_CHECK(name_hash(matched) == name_hash(match_name(word)));
    }
    FUZZ_CHECK(title_key("PPSA00001") == 1u && title_key("CUSA00001") == 100001u);

    // The titles the host adds to the file are all comments: the player's own lines read the same afterwards, and a second
    // round adds nothing.
    std::vector<ListedTitle> titles;
    for (int i = 0, n = in.ConsumeIntegralInRange<int>(0, 4); i < n; ++i) {
        ListedTitle title;
        title.id = in.ConsumeBool() ? "PPSA" + std::to_string(10000 + in.ConsumeIntegralInRange<int>(0, 89999)) : in.ConsumeRandomLengthString(9);
        title.name = in.ConsumeRandomLengthString(30);
        if (in.ConsumeBool()) title.mapped = static_cast<Profile>(in.ConsumeIntegralInRange<size_t>(0, profile_names.size() - 1));
        titles.push_back(std::move(title));
    }
    const std::string added = profile_list_additions(existing, titles);
    const std::string grown = existing + added;
    const ProfileList after = parse_profile_list(grown);
    FUZZ_CHECK(after.titles == list.titles);
    FUZZ_CHECK(after.own.size() == list.own.size());
    FUZZ_CHECK(after.problems == list.problems);
    FUZZ_CHECK(profile_list_additions(grown, titles).empty());
    return 0;
}
