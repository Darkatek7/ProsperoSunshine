// SPDX-License-Identifier: GPL-3.0-only
// The console's installed games, as apps for the Moonlight list. Portable, so that the tests can run it.
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prospero {

struct Title {
    std::string id, name;
    std::filesystem::path cover;  // empty when the title has none
    int app_id;
};

struct TitleMeta {
    std::string name;
    bool game;
    uint32_t concept_id;  // the PS Store concept of a PS5 title (conceptId), shared by its regions; 0 if none
    std::string english;  // its English name if it has one, else name
};

// Four capitals and five digits, such as PPSA12345.
bool valid_title_id(std::string_view id);

// Clients remember apps by this number, so it depends on the title ID alone. Always above 1, which is the screen app.
int title_app_id(std::string_view id);

// sce_sys/param.json of a PS5 title; param.sfo of a PS4 title. Nothing if the data can't be read.
std::optional<TitleMeta> parse_param_json(std::string_view json);
std::optional<TitleMeta> parse_param_sfo(std::span<const uint8_t> sfo);

// One title's data under user_dir: param.json or param.sfo, from appmeta or the app's sce_sys. Nothing if unreadable.
std::optional<TitleMeta> read_title_meta(const std::filesystem::path &user_dir, const std::string &id);

// The games under user_dir (/user on the console), sorted by name.
std::vector<Title> list_titles(const std::filesystem::path &user_dir);

}  // namespace prospero
