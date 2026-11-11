// SPDX-License-Identifier: GPL-3.0-only
// A title's own files, which the console reads for the app list in Moonlight: sce_sys/param.json of a PS5 title and
// param.sfo of a PS4 title (src/apps/titles.cpp). A package can ship any bytes there.
#include "fuzz_common.hpp"
#include "apps/titles.hpp"

#include <span>

using namespace prospero;

namespace {

// A JSON string literal of the text: quotes, backslashes and control characters escaped, other bytes as they are.
std::string quoted(const std::string &text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') (out += '\\') += c;
        else if (static_cast<unsigned char>(c) < 0x20) {
            char buffer[8];
            std::snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
            out += buffer;
        } else out += c;
    }
    return out + '"';
}

// UTF-8 of one scalar value, spelled in JSON as \uXXXX (a pair for those above the Basic Multilingual Plane).
void scalar(FuzzedDataProvider &in, std::string &utf8, std::string &json) {
    uint32_t code = in.ConsumeIntegralInRange<uint32_t>(0x20, 0x10ffff);
    if (code >= 0xd800 && code <= 0xdfff) code -= 0x800;
    if (code == '"' || code == '\\') code = 'x';
    char buffer[16];
    if (code >= 0x10000) {
        const uint32_t v = code - 0x10000;
        std::snprintf(buffer, sizeof buffer, "\\u%04x\\u%04x", 0xd800 + (v >> 10), 0xdc00 + (v & 0x3ff));
    } else {
        std::snprintf(buffer, sizeof buffer, "\\u%04x", code);
    }
    json += buffer;
    if (code < 0x80) utf8 += static_cast<char>(code);
    else if (code < 0x800) { utf8 += static_cast<char>(0xc0 | code >> 6); utf8 += static_cast<char>(0x80 | (code & 0x3f)); }
    else if (code < 0x10000) { utf8 += static_cast<char>(0xe0 | code >> 12); utf8 += static_cast<char>(0x80 | (code >> 6 & 0x3f)); utf8 += static_cast<char>(0x80 | (code & 0x3f)); }
    else { utf8 += static_cast<char>(0xf0 | code >> 18); utf8 += static_cast<char>(0x80 | (code >> 12 & 0x3f)); utf8 += static_cast<char>(0x80 | (code >> 6 & 0x3f)); utf8 += static_cast<char>(0x80 | (code & 0x3f)); }
}

bool valid_utf8(std::string_view text) {
    for (size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        const size_t length = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (length == 0 || i + length > text.size()) return false;
        uint32_t code = length == 1 ? c : c & (0xff >> (length + 1));
        for (size_t k = 1; k < length; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xc0) != 0x80) return false;
            code = code << 6 | (next & 0x3f);
        }
        if ((length == 2 && code < 0x80) || (length == 3 && code < 0x800) || (length == 4 && code < 0x10000) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff))
            return false;
        i += length;
    }
    return true;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider in(data, size);

    // A param.json of a game with the name spelled in escapes: the name comes out as the UTF-8 of those characters.
    {
        std::string utf8, json;
        for (size_t i = 0, n = in.ConsumeIntegralInRange<size_t>(1, 12); i < n; ++i) scalar(in, utf8, json);
        const long category = in.ConsumeBool() ? 0 : 65536;
        const std::string text = "{\"applicationCategoryType\":" + std::to_string(category) + ",\"conceptId\":\"" +
                                 std::to_string(in.ConsumeIntegral<uint32_t>()) + "\",\"localizedParameters\":{\"defaultLanguage\":\"en-US\",\"en-US\":{\"titleName\":\"" +
                                 json + "\"}}}";
        const auto meta = parse_param_json(text);
        FUZZ_CHECK(meta && meta->name == utf8 && meta->english == utf8 && meta->game == (category == 0));
        FUZZ_CHECK(valid_utf8(meta->name));
    }
    // The same with the bytes as they are, escaped only where JSON needs it.
    {
        const std::string name = in.ConsumeRandomLengthString(40);
        const std::string text = "{\"localizedParameters\":{\"defaultLanguage\":\"en-US\",\"en-US\":{\"titleName\":" + quoted(name) + "}}}";
        const auto meta = parse_param_json(text);
        FUZZ_CHECK(meta.has_value() == !name.empty());
        if (meta) FUZZ_CHECK(meta->name == name);
    }

    // Any bytes as param.json.
    const std::string raw = in.ConsumeRandomLengthString(500);
    if (const auto meta = parse_param_json(raw)) {
        FUZZ_CHECK(!meta->name.empty() && !meta->english.empty());
        // A name goes into the app list's XML for Moonlight: it has to be text, whatever the file's escapes spelled.
        FUZZ_CHECK(valid_utf8(meta->name) || !valid_utf8(raw));
    }
    // Any bytes as param.sfo.
    const auto rest = in.ConsumeRemainingBytes<uint8_t>();
    if (const auto meta = parse_param_sfo(rest)) FUZZ_CHECK(!meta->name.empty());
    // And with a good header in front, so that the tables are reached.
    {
        std::vector<uint8_t> sfo{0, 'P', 'S', 'F', 1, 1, 0, 0};
        const uint32_t keys = 20 + 16, values = keys + 16;
        for (const uint32_t v : {keys, values, 1u}) for (int i = 0; i < 4; ++i) sfo.push_back(static_cast<uint8_t>(v >> (8 * i)));
        sfo.insert(sfo.end(), rest.begin(), rest.end());
        if (const auto meta = parse_param_sfo(sfo)) FUZZ_CHECK(!meta->name.empty());
    }
    (void)valid_title_id(raw);
    FUZZ_CHECK(title_app_id(raw) > 1);
    return 0;
}
