// SPDX-License-Identifier: GPL-3.0-only
#include "apps/titles.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <unistd.h>

using namespace prospero;
namespace fs = std::filesystem;

namespace {

std::string param_json(const std::string &name, int category) {
    return R"({"applicationCategoryType":)" + std::to_string(category) +
           R"(,"contentBadgeType":1,"list":[1,{"a":"}"}],"localizedParameters":{"defaultLanguage":"en-US","de-DE":{"titleName":"Deutsch"},"en-US":{"titleName":")" +
           name + R"("}},"titleId":"PPSA00000"})";
}

std::string sfo(const std::string &title, const std::string &category) {
    // CATEGORY and TITLE, as PS4 titles carry them.
    const std::string keys = std::string("CATEGORY\0TITLE\0", 15);
    const std::string values = category + std::string(1, '\0') + title + std::string(1, '\0');
    std::string out("\0PSF\x01\x01\0\0", 8);
    const auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) out += static_cast<char>(v >> (8 * i)); };
    const auto u16 = [&](uint16_t v) { for (int i = 0; i < 2; i++) out += static_cast<char>(v >> (8 * i)); };
    const uint32_t key_table = 20 + 2 * 16, value_table = key_table + static_cast<uint32_t>(keys.size());
    u32(key_table); u32(value_table); u32(2);
    u16(0); u16(0x0204); u32(static_cast<uint32_t>(category.size() + 1)); u32(4); u32(0);
    u16(9); u16(0x0204); u32(static_cast<uint32_t>(title.size() + 1)); u32(128); u32(static_cast<uint32_t>(category.size() + 1));
    return out + keys + values;
}

std::optional<TitleMeta> parse_sfo(const std::string &data) {
    return parse_param_sfo({reinterpret_cast<const uint8_t *>(data.data()), data.size()});
}

void write(const fs::path &path, const std::string &content) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << content;
}

}  // namespace

int main() {
    assert(valid_title_id("PPSA12345"));
    assert(valid_title_id("CUSA00001"));
    for (const char *bad : {"", "PPSA1234", "PPSA123456", "ppsa12345", "PPSA1234a", "../../etc", "NPXS40087x"}) assert(!valid_title_id(bad));
    assert(title_app_id("PPSA12345") == title_app_id("PPSA12345"));
    assert(title_app_id("PPSA12345") != title_app_id("PPSA12346"));
    assert(title_app_id("PPSA12345") > 1);

    auto meta = parse_param_json(param_json("Game", 0));
    assert(meta && meta->name == "Game" && meta->game);
    meta = parse_param_json(param_json("Video", 65536));
    assert(meta && meta->name == "Video" && !meta->game);
    meta = parse_param_json(param_json("Caf\\u00e9 \\ud83c\\udfae \\\"x\\\"", 0));
    assert(meta && meta->name == "Café 🎮 \"x\"");
    // A surrogate escape that is not half of a pair becomes U+FFFD, not invalid UTF-8 for the app list's XML (found by
    // tests/fuzz/titles_fuzz.cpp).
    meta = parse_param_json(param_json("a\\ud800b\\udc00c\\ud800", 0));
    assert(meta && meta->name == "a\xef\xbf\xbd" "b\xef\xbf\xbd" "c\xef\xbf\xbd");
    // The console's own copies end in zero bytes.
    meta = parse_param_json(param_json("Game", 0) + std::string(3, '\0'));
    assert(meta && meta->name == "Game");
    meta = parse_param_json(R"({"applicationCategoryType":0,"localizedParameters":{"fr-FR":{"titleName":"Seul"}}})");
    assert(meta && meta->name == "Seul" && meta->english == "Seul" && meta->concept_id == 0);
    // The Store concept and the English name, for the keyboard pad's profile by game.
    meta = parse_param_json(R"({"applicationCategoryType":0,"conceptId":"10003550","localizedParameters":{"defaultLanguage":"ja-JP",)"
                            R"("ja-JP":{"titleName":"\u30b2\u30fc\u30e0"},"en-US":{"titleName":"Game"}}})");
    assert(meta && meta->name == "ゲーム" && meta->english == "Game" && meta->concept_id == 10003550);
    meta = parse_param_json(param_json("Game", 0));
    assert(meta->concept_id == 0 && meta->english == "Game");
    for (const char *bad : {"", "{", "[]", R"({"localizedParameters":{}})", R"({"a":)", R"({"localizedParameters":{"en-US":{"titleName":"x)"})
        assert(!parse_param_json(bad));
    std::string deep = "{\"a\":";
    for (int i = 0; i < 100000; i++) deep += '[';
    assert(!parse_param_json(deep));

    meta = parse_sfo(sfo("Old Game", "gd"));
    assert(meta && meta->name == "Old Game" && meta->game);
    meta = parse_sfo(sfo("Old App", "gdk"));
    assert(meta && !meta->game);
    const auto whole = sfo("Old Game", "gd");
    for (size_t size = 0; size < whole.size(); size++) parse_sfo(whole.substr(0, size));  // no crash under the sanitizers
    assert(!parse_sfo("not an sfo file, long enough for a header"));

    const auto root = fs::temp_directory_path() / ("prospero_titles_" + std::to_string(getpid()));
    fs::remove_all(root);
    const auto game = [&](const std::string &id, const std::string &name, int category) {
        fs::create_directories(root / "app" / id);
        write(root / "appmeta" / id / "param.json", param_json(name, category));
    };
    game("PPSA00002", "zebra", 0);
    game("PPSA00001", "Apple", 0);
    write(root / "appmeta" / "PPSA00001" / "icon0.png", "png");
    game("PPSA00003", "Media app", 65536);
    fs::create_directories(root / "app" / "not-a-title");
    fs::create_directories(root / "app" / "PPSA00004");  // no metadata
    fs::create_directories(root / "app" / "CUSA00001");
    write(root / "appmeta" / "CUSA00001" / "param.sfo", sfo("Middle", "gd"));
    fs::create_directories(root / "app" / "PPSA00005");
    write(root / "app" / "PPSA00005" / "sce_sys" / "param.json", param_json("Own sce_sys", 0));
    write(root / "app" / "PPSA00005" / "sce_sys" / "icon0.png", "png");
    const auto titles = list_titles(root);
    assert(titles.size() == 4);
    assert(titles[0].id == "PPSA00001" && titles[0].name == "Apple" && titles[0].cover == root / "appmeta" / "PPSA00001" / "icon0.png");
    assert(titles[1].id == "CUSA00001" && titles[1].cover.empty());
    assert(titles[2].id == "PPSA00005" && titles[2].cover == root / "app" / "PPSA00005" / "sce_sys" / "icon0.png");
    assert(titles[3].id == "PPSA00002" && titles[3].app_id == title_app_id("PPSA00002"));
    assert(read_title_meta(root, "PPSA00005")->name == "Own sce_sys" && read_title_meta(root, "CUSA00001")->english == "Middle");
    assert(!read_title_meta(root, "PPSA00004"));

    // Titles on other storage (the M.2 SSD): the folder with the package is there, the data under appmeta as for the others.
    const auto m2 = root / "ext1" / "user" / "app", usb = root / "ext0" / "user" / "app";
    fs::create_directories(m2 / "PPSA00006");
    write(root / "appmeta" / "PPSA00006" / "param.json", param_json("Big game", 0));
    write(root / "appmeta" / "PPSA00006" / "icon0.png", "png");
    fs::create_directories(m2 / "PPSA00007");
    write(root / "appmeta" / "PPSA00007" / "param.json", param_json("Media app on the SSD", 65536));
    fs::create_directories(m2 / "PPSA00008");  // no metadata
    fs::create_directories(m2 / "PPSA00001");  // also on the internal storage
    fs::create_directories(m2 / "not-a-title");
    fs::create_directories(usb / "CUSA00002");
    write(root / "appmeta" / "external" / "CUSA00002" / "param.sfo", sfo("Old game on USB", "gd"));
    write(root / "appmeta" / "external" / "CUSA00002" / "icon0.png", "png");
    assert(list_titles(root).size() == 4);  // not without their folders
    const fs::path more[] = {m2, usb, root / "ext2" / "user" / "app"};  // the last one doesn't exist
    const auto all = list_titles(root, more);
    assert(all.size() == 6);
    assert(all[0].id == "PPSA00001" && all[1].id == "PPSA00006" && all[1].name == "Big game");
    assert(all[1].cover == root / "appmeta" / "PPSA00006" / "icon0.png" && all[1].app_id == title_app_id("PPSA00006"));
    assert(all[2].id == "CUSA00001" && all[3].id == "CUSA00002" && all[3].name == "Old game on USB");
    assert(all[3].cover == root / "appmeta" / "external" / "CUSA00002" / "icon0.png");
    assert(all[4].id == "PPSA00005" && all[5].id == "PPSA00002");
    fs::remove_all(root);
    assert(list_titles(root).empty());

    std::puts("PASS: titles (IDs, param.json, param.sfo, games only, covers, order, other storage, Store concept, English name)");
    return 0;
}
