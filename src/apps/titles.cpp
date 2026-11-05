// SPDX-License-Identifier: GPL-3.0-only
#include "apps/titles.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>

namespace prospero {
namespace {

// Just enough JSON for param.json. The tests run without the third-party builds, so this doesn't use a library.
class JsonReader {
public:
    explicit JsonReader(std::string_view text) : text_(text) {}

    char peek() {
        while (at_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[at_]))) ++at_;
        return at_ < text_.size() ? text_[at_] : '\0';
    }

    bool consume(char c) {
        if (peek() != c) return false;
        ++at_;
        return true;
    }

    bool string(std::string &out) {
        out.clear();
        if (!consume('"')) return false;
        while (at_ < text_.size()) {
            const char c = text_[at_++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ == text_.size()) return false;
            const char e = text_[at_++];
            switch (e) {
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': if (!unicode(out)) return false; break;
                default: out += e;
            }
        }
        return false;
    }

    bool integer(long &out) {
        peek();
        const size_t begin = at_;
        const bool negative = at_ < text_.size() && text_[at_] == '-';
        if (negative) ++at_;
        long value = 0;
        while (at_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[at_])) && value < 1000000000000L)
            value = value * 10 + (text_[at_++] - '0');
        if (at_ == begin + (negative ? 1 : 0)) return false;
        out = negative ? -value : value;
        return true;
    }

    // Walks an object; handle(key) reads or skips that key's value.
    template <typename Handle> bool object(Handle handle) {
        if (!consume('{')) return false;
        if (consume('}')) return true;
        std::string key;
        do {
            if (!string(key) || !consume(':') || !handle(key)) return false;
        } while (consume(','));
        return consume('}');
    }

    bool skip(int depth = 0) {
        if (depth > 32) return false;
        std::string ignored;
        switch (peek()) {
            case '"': return string(ignored);
            case '{': return object([&](const std::string &) { return skip(depth + 1); });
            case '[':
                ++at_;
                if (consume(']')) return true;
                do {
                    if (!skip(depth + 1)) return false;
                } while (consume(','));
                return consume(']');
            default: {
                const size_t begin = at_;
                while (at_ < text_.size() && !std::strchr(",}] \t\r\n", text_[at_])) ++at_;
                return at_ != begin;
            }
        }
    }

private:
    bool hex4(unsigned &out) {
        if (text_.size() - at_ < 4) return false;
        out = 0;
        for (int i = 0; i < 4; i++) {
            const auto c = static_cast<unsigned char>(text_[at_++]);
            if (!std::isxdigit(c)) return false;
            out = out * 16 + static_cast<unsigned>(std::isdigit(c) ? c - '0' : std::tolower(c) - 'a' + 10);
        }
        return true;
    }

    bool unicode(std::string &out) {
        unsigned code = 0;
        if (!hex4(code)) return false;
        if (code >= 0xd800 && code < 0xdc00 && text_.substr(at_, 2) == "\\u") {
            at_ += 2;
            unsigned low = 0;
            if (!hex4(low) || low < 0xdc00 || low > 0xdfff) return false;
            code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
        }
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xc0 | code >> 6);
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xe0 | code >> 12);
            out += static_cast<char>(0x80 | (code >> 6 & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | code >> 18);
            out += static_cast<char>(0x80 | (code >> 12 & 0x3f));
            out += static_cast<char>(0x80 | (code >> 6 & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        }
        return true;
    }

    std::string_view text_;
    size_t at_ = 0;
};

uint32_t le(std::span<const uint8_t> data, size_t at, size_t bytes) {
    uint32_t value = 0;
    for (size_t i = 0; i < bytes; i++) value |= static_cast<uint32_t>(data[at + i]) << (8 * i);
    return value;
}

std::string read_file(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string_view trimmed(std::string_view text) {
    const auto end = text.find_last_not_of(std::string_view(" \t\r\n\0", 5));
    return end == std::string_view::npos ? std::string_view{} : text.substr(0, end + 1);
}

std::optional<TitleMeta> read_meta(const std::filesystem::path &user_dir, const std::string &id) {
    const auto meta = user_dir / "appmeta" / id, sys = user_dir / "app" / id / "sce_sys";
    for (const auto &path : {meta / "param.json", sys / "param.json"})
        if (auto parsed = parse_param_json(read_file(path))) return parsed;
    for (const auto &path : {meta / "param.sfo", sys / "param.sfo"}) {
        const auto data = read_file(path);
        if (auto parsed = parse_param_sfo({reinterpret_cast<const uint8_t *>(data.data()), data.size()})) return parsed;
    }
    return std::nullopt;
}

std::string lowered(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

bool valid_title_id(std::string_view id) {
    if (id.size() != 9) return false;
    for (size_t i = 0; i < 9; i++) {
        const auto c = static_cast<unsigned char>(id[i]);
        if (i < 4 ? !std::isupper(c) : !std::isdigit(c)) return false;
    }
    return true;
}

int title_app_id(std::string_view id) {
    uint32_t hash = 2166136261u;  // FNV-1a
    for (const char c : id) hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
    const int value = static_cast<int>(hash & 0x7fffffffu);
    return value > 1 ? value : value + 2;
}

std::optional<TitleMeta> parse_param_json(std::string_view json) {
    JsonReader reader(trimmed(json));
    long category = -1;
    std::string language, name, concept_id;
    std::map<std::string, std::string> names;
    const bool ok = reader.object([&](const std::string &key) {
        if (key == "applicationCategoryType") return reader.integer(category);
        if (key == "conceptId") return reader.string(concept_id);
        if (key != "localizedParameters") return reader.skip();
        return reader.object([&](const std::string &local) {
            if (local == "defaultLanguage") return reader.string(language);
            if (reader.peek() != '{') return reader.skip();
            return reader.object([&](const std::string &field) {
                if (field != "titleName") return reader.skip();
                return reader.string(names[local]);
            });
        });
    });
    if (!ok) return std::nullopt;
    for (const auto &candidate : {language, std::string("en-US")})
        if (const auto it = names.find(candidate); it != names.end() && !it->second.empty()) {
            name = it->second;
            break;
        }
    if (name.empty())
        for (const auto &[local, value] : names)
            if (!value.empty()) {
                name = value;
                break;
            }
    if (name.empty()) return std::nullopt;
    uint32_t concept_number = 0;
    std::from_chars(concept_id.data(), concept_id.data() + concept_id.size(), concept_number);
    const auto english = names.find("en-US");
    return TitleMeta{name, category == 0, concept_number,
                     english != names.end() && !english->second.empty() ? english->second : name};
}

std::optional<TitleMeta> parse_param_sfo(std::span<const uint8_t> sfo) {
    constexpr size_t header = 20, entry = 16;
    if (sfo.size() < header || std::memcmp(sfo.data(), "\0PSF", 4) != 0) return std::nullopt;
    const size_t keys = le(sfo, 8, 4), values = le(sfo, 12, 4), count = le(sfo, 16, 4);
    if (count > (sfo.size() - header) / entry || keys > sfo.size() || values > sfo.size()) return std::nullopt;
    std::string name, category;
    for (size_t i = 0; i < count; i++) {
        const size_t at = header + i * entry;
        const size_t key = keys + le(sfo, at, 2), length = le(sfo, at + 4, 4), value = values + le(sfo, at + 12, 4);
        if (key >= sfo.size() || value > sfo.size() || length > sfo.size() - value) return std::nullopt;
        const std::string_view all(reinterpret_cast<const char *>(sfo.data()), sfo.size());
        const auto key_end = all.find('\0', key);
        if (key_end == std::string_view::npos) return std::nullopt;
        const auto key_name = all.substr(key, key_end - key);
        const auto text = trimmed(all.substr(value, length));
        if (key_name == "TITLE") name = text;
        if (key_name == "CATEGORY") category = text;
    }
    if (name.empty()) return std::nullopt;
    return TitleMeta{name, category == "gd", 0, name};
}

std::optional<TitleMeta> read_title_meta(const std::filesystem::path &user_dir, const std::string &id) {
    return read_meta(user_dir, id);
}

std::vector<Title> list_titles(const std::filesystem::path &user_dir) {
    std::vector<Title> titles;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(user_dir / "app", ec)) {
        const auto id = entry.path().filename().string();
        if (!valid_title_id(id)) continue;
        const auto meta = read_meta(user_dir, id);
        if (!meta || !meta->game) continue;
        std::filesystem::path cover;
        for (const auto &path : {user_dir / "appmeta" / id / "icon0.png", user_dir / "app" / id / "sce_sys" / "icon0.png"})
            if (cover.empty() && std::filesystem::is_regular_file(path, ec)) cover = path;
        titles.push_back({id, meta->name, cover, title_app_id(id)});
    }
    std::sort(titles.begin(), titles.end(), [](const Title &a, const Title &b) {
        const auto left = lowered(a.name), right = lowered(b.name);
        return left != right ? left < right : a.id < b.id;
    });
    return titles;
}

}  // namespace prospero
