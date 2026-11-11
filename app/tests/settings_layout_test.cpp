// SPDX-License-Identifier: GPL-3.0-only
// The settings page's layout and D-pad paths (app/src/psapp/settings_layout.hpp), checked on the PC because nobody may
// look at the console's screen while it changes; and the same layout drawn as an SVG for a look (psapp_test --svg).
#include "app/psapp/conf.hpp"
#include "app/psapp/experimental_list.h"
#include "app/psapp/settings_layout.hpp"

#include "check.hpp"
#include "inter_metrics.hpp"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace psapp;
using namespace psapp::settings_layout;
using psapp_test::kInterRegular;
using psapp_test::kInterSemiBold;
using psapp_test::text_width;

namespace
{

const char *const kActionNames[kActions] = {"save", "save_restart", "revert", "web"};
const char *const kActionLabels[kActions] = {"Save", "Save and restart host", "Revert changes", "Web page: off"};

// An item's name: the option's key, or the action's.
std::string name_of(const Layout &layout, int item)
{
    const std::size_t i = static_cast<std::size_t>(item);
    return i < layout.tiles.size() ? option_specs()[i].key : kActionNames[i - layout.tiles.size()];
}

const char *move_name(Move move)
{
    return move == Move::up ? "up" : move == Move::down ? "down" : move == Move::left ? "left" : "right";
}

Move opposite(Move move)
{
    return move == Move::up ? Move::down : move == Move::down ? Move::up : move == Move::left ? Move::right : Move::left;
}

// Rows of different counts can't all go back the way they came: the nearer centre one way is not always the
// nearer one back. These are the round trips that don't, for today's list (option key or action, the first move).
// A change of the option list may change them: look at the SVG, and update this list when the new paths make sense.
const std::set<std::pair<std::string, std::string>> kOneWay = {
    // Host and Playing's five tiles over Network's three.
    {"min_log_level", "down"},         // to port, whose up is the host name
    {"ps_button_all_players", "down"}, // to pair_over_internet, whose up is error correction
    // Network's three over the Experimental six.
    {"experimental_hdr", "up"},          // to lan_encryption_mode, whose down is HEVC
    {"experimental_high_bitrate", "up"}, // to wan_encryption_mode, whose down is 1440p and 4K
    {"surround_sound", "up"},            // to ping_timeout, whose down is below 1080p
    // The Experimental six over the four actions.
    {"experimental_high_bitrate", "down"}, // to revert, whose up is 1440p and 4K
    {"experimental_small_modes", "down"},  // to web, whose up is surround
};

bool in_rows(const Box &box)
{
    return box.x >= kLeft - 0.01f && box.right() <= kRight + 0.01f && box.y >= kTop - 0.01f && box.x >= 0.0f &&
           box.y >= 0.0f && box.right() <= 1920.0f && box.bottom() <= 1080.0f;
}

void geometry_tests(const Layout &layout)
{
    const std::vector<OptionSpec> &specs = option_specs();
    CHECK(layout.error.empty());
    REQUIRE(layout.tiles.size() == specs.size());
    REQUIRE(layout.actions.size() == static_cast<std::size_t>(kActions));

    // Every option has one tile of its own, inside the rows' area and above the actions.
    float actions_top = 1080.0f;
    for (const Box &action : layout.actions)
        actions_top = std::min(actions_top, action.y);
    for (std::size_t i = 0; i < specs.size(); ++i)
    {
        const Box &tile = layout.tiles[i];
        if (!(tile.w > 0.0f && tile.h > 0.0f && in_rows(tile) && tile.bottom() <= actions_top))
            std::printf("  tile of %s: %.1f %.1f %.1f %.1f\n", specs[i].key, tile.x, tile.y, tile.w, tile.h);
        CHECK(tile.w > 0.0f && tile.h > 0.0f);
        CHECK(in_rows(tile));
        CHECK(tile.bottom() <= actions_top);
    }
    for (const Box &action : layout.actions)
    {
        CHECK(in_rows(action));
        CHECK(action.bottom() <= kHelpBar.y);
    }
    CHECK(in_rows(kHelpBar));

    // Nothing overlaps: tiles, captions, actions, the help bar.
    std::vector<std::pair<std::string, Box>> parts;
    for (int i = 0; i < layout.items(); ++i)
        parts.push_back({name_of(layout, i), layout.item(i)});
    for (const Caption &caption : layout.captions)
    {
        parts.push_back({std::string("caption ") + caption.text, caption.box});
        CHECK(in_rows(caption.box));
    }
    parts.push_back({"help bar", kHelpBar});
    for (std::size_t a = 0; a < parts.size(); ++a)
    {
        for (std::size_t b = a + 1; b < parts.size(); ++b)
        {
            if (parts[a].second.overlaps(parts[b].second))
            {
                std::printf("  %s overlaps %s\n", parts[a].first.c_str(), parts[b].first.c_str());
                CHECK(!parts[a].second.overlaps(parts[b].second));
            }
        }
    }

    // One caption for each group with options, above its first tile, and the caption's text fits its line.
    std::set<OptionGroup> groups;
    for (const OptionSpec &spec : specs)
        groups.insert(spec.group);
    CHECK(layout.captions.size() == groups.size());
    for (const Caption &caption : layout.captions)
    {
        CHECK(text_width(kInterSemiBold, caption.text, 22.0f, 4.0f) + 56.0f <= caption.box.w);
        for (std::size_t i = 0; i < specs.size(); ++i)
        {
            if (specs[i].group == caption.group)
                CHECK(layout.tiles[i].y >= caption.box.bottom());
        }
    }
    // The Experimental caption's note fits beside it on the widest firmware text the app writes.
    for (const Caption &caption : layout.captions)
    {
        if (caption.group != OptionGroup::experimental)
            continue;
        const std::string note = "For what is untested on Firmware 13.00. Off by default; what fails turns itself off.";
        CHECK(text_width(kInterSemiBold, caption.text, 22.0f, 4.0f) + 80.0f + text_width(kInterRegular, note, 22.0f) <=
              caption.box.w);
    }
}

// What the screen draws stays whole: labels on their tiles, helps in the help bar and in the editors' sheets.
void text_tests(const Layout &layout)
{
    const std::vector<OptionSpec> &specs = option_specs();
    for (std::size_t i = 0; i < specs.size(); ++i)
    {
        const OptionSpec &spec = specs[i];
        const float room = layout.tiles[i].w - kTextX - kTextRight;
        const float label = text_width(kInterSemiBold, spec.label, kLabelSize);
        if (label > room * 0.98f)
            std::printf("  label of %s: %.0f px for %.0f\n", spec.key, label, room);
        CHECK(label <= room * 0.98f);
        // The value line: the default and its tag, or a switch's track (screen.cpp draws both the same way).
        const bool toggle = spec.kind == OptionKind::toggle;
        const float value_room = room - (toggle ? 84.0f - kTextRight + 12.0f : 0.0f);
        CHECK(text_width(kInterSemiBold, spec.default_label, kValueSize) <= value_room);

        // The help bar: the help at 22 at least, beside "Default: ...".
        const std::string extra = std::string("Default: ") + spec.default_label;
        const float bar_room = kHelpBar.w - 56.0f - 30.0f - (text_width(kInterRegular, extra, 22.0f) + 30.0f);
        const float help = text_width(kInterRegular, spec.help, kHelpSmallSize);
        if (help > bar_room)
            std::printf("  help of %s: %.0f px for %.0f\n", spec.key, help, bar_room);
        CHECK(help <= bar_room);
        // The editors (screen.cpp, draw_sheet): one line of help under the title, 880 wide for numbers and choices,
        // 1120 for text with " Leave it empty for the default." after it.
        if (spec.kind == OptionKind::number || spec.kind == OptionKind::choice)
            CHECK(text_width(kInterRegular, spec.help, 24.0f) <= 880.0f - 60.0f);
        if (spec.kind == OptionKind::text)
            CHECK(text_width(kInterRegular, std::string(spec.help) + " Leave it empty for the default.", 24.0f) <= 1120.0f - 60.0f);
    }
}

void navigation_tests(const Layout &layout)
{
    const int items = layout.items();
    const Move moves[] = {Move::up, Move::down, Move::left, Move::right};

    // From every tile and action, every other one is reachable with the D-pad.
    for (int start = 0; start < items; ++start)
    {
        std::vector<bool> seen(static_cast<std::size_t>(items), false);
        std::vector<int> queue = {start};
        seen[static_cast<std::size_t>(start)] = true;
        for (std::size_t q = 0; q < queue.size(); ++q)
        {
            for (Move move : moves)
            {
                const int to = next(layout, queue[q], move);
                if (to >= 0 && !seen[static_cast<std::size_t>(to)])
                {
                    seen[static_cast<std::size_t>(to)] = true;
                    queue.push_back(to);
                }
            }
        }
        if (static_cast<int>(queue.size()) != items)
            std::printf("  from %s only %zu of %d reachable\n", name_of(layout, start).c_str(), queue.size(), items);
        CHECK(static_cast<int>(queue.size()) == items);
    }

    // Moves go where they say, and back again, except the listed one-way trips.
    std::set<std::pair<std::string, std::string>> one_way;
    for (int from = 0; from < items; ++from)
    {
        const Box &a = layout.item(from);
        for (Move move : moves)
        {
            const int to = next(layout, from, move);
            if (to < 0)
                continue;
            const Box &b = layout.item(to);
            if (move == Move::left)
                CHECK(b.cx() < a.cx() && b.y == a.y);
            if (move == Move::right)
                CHECK(b.cx() > a.cx() && b.y == a.y);
            if (move == Move::up)
                CHECK(b.y < a.y);
            if (move == Move::down)
                CHECK(b.y > a.y);
            const int back = next(layout, to, opposite(move));
            // Left and right always come back; up and down at least to the same row.
            CHECK(back >= 0);
            if (back < 0)
                continue;
            CHECK(layout.item(back).y == a.y);
            if (move == Move::left || move == Move::right)
                CHECK(back == from);
            if (back != from)
                one_way.insert({name_of(layout, from), move_name(move)});
        }
    }
    // An edge: nothing above the first row, nothing under the actions, nothing beyond a row's ends.
    for (int i = 0; i < items; ++i)
    {
        const Box &box = layout.item(i);
        if (box.y == kTop + kCaptionHeight)
            CHECK(next(layout, i, Move::up) < 0);
        if (box.y == kActionsTop)
            CHECK(next(layout, i, Move::down) < 0);
    }
    const std::set<std::pair<std::string, std::string>> &expected = kOneWay;
    for (const auto &[item, move] : one_way)
    {
        if (!expected.count({item, move}))
            std::printf("  one-way trip not in kOneWay: %s %s\n", item.c_str(), move.c_str());
    }
    for (const auto &[item, move] : expected)
    {
        if (!one_way.count({item, move}))
            std::printf("  kOneWay lists %s %s, which comes back\n", item.c_str(), move.c_str());
    }
    CHECK(one_way == expected);

    // Some paths a player takes: down from the last Experimental switch reaches the web page, and up from Save the
    // first switch; right from Log level goes on to Playing's first tile.
    const auto index_of = [&](const char *key) {
        const OptionSpec *spec = find_option(key);
        return spec != nullptr ? static_cast<int>(spec - option_specs().data()) : -1;
    };
    const int tiles = static_cast<int>(layout.tiles.size());
    CHECK(next(layout, index_of("surround_sound"), Move::down) == tiles + 3);
    CHECK(next(layout, tiles, Move::up) == index_of("experimental_hevc"));
    CHECK(next(layout, tiles + 3, Move::up) == index_of("surround_sound"));
    CHECK(next(layout, index_of("min_log_level"), Move::right) == index_of("close_game_on_quit"));
    CHECK(next(layout, index_of("close_game_on_quit"), Move::left) == index_of("min_log_level"));
    CHECK(next(layout, tiles + 2, Move::right) == tiles + 3);
    CHECK(next(layout, -1, Move::up) < 0 && next(layout, items, Move::up) < 0);
}

// The Experimental tiles: each switch of experimental_list.h once, in that row, and each tile's switch is its own.
void experimental_tests(const Layout &layout)
{
    const std::vector<OptionSpec> &specs = option_specs();
    std::vector<int> found(PS_EXP_SWITCH_COUNT, 0);
    float row = -1.0f;
    for (std::size_t i = 0; i < specs.size(); ++i)
    {
        const int index = experimental_switch(specs[i]);
        if (index < 0)
        {
            // Only switches are locked by firmware: everything else, Surround included, is no switch of the list.
            CHECK(specs[i].key != std::string("ps_button_all_players") || specs[i].group == OptionGroup::playing);
            continue;
        }
        REQUIRE(index < PS_EXP_SWITCH_COUNT);
        CHECK(std::string(ps_exp_switch_at(index)->key) == specs[i].key);
        CHECK(std::string(ps_exp_switch_at(index)->label) == specs[i].label);
        CHECK(specs[i].group == OptionGroup::experimental);
        ++found[static_cast<std::size_t>(index)];
        if (row < 0.0f)
            row = layout.tiles[i].y;
        CHECK(layout.tiles[i].y == row);
    }
    for (int count : found)
        CHECK(count == 1);
    // Surround sits in the Experimental row, unlocked.
    const OptionSpec *surround = find_option("surround_sound");
    REQUIRE(surround != nullptr);
    CHECK(surround->group == OptionGroup::experimental && experimental_switch(*surround) < 0);
    CHECK(layout.tiles[static_cast<std::size_t>(surround - specs.data())].y == row);
    // The options that were in the Experimental row by mistake in 0.6.0 are no switches.
    for (const char *key : {"ps_button_all_players", "pair_over_internet", "upnp"})
        CHECK(experimental_switch(*find_option(key)) < 0);
}

// An option without a group of the page has no place: build() says which.
void group_tests()
{
    std::vector<OptionSpec> specs = option_specs();
    CHECK(build(specs).error.empty());
    specs.push_back({static_cast<OptionGroup>(99), icons::Icon::tag, "orphan", "Orphan", "None.", OptionKind::toggle, "Off", {}});
    const Layout layout = build(specs);
    CHECK(layout.error.find("orphan") != std::string::npos);
    CHECK(layout.tiles.back().w == 0.0f);
    for (OptionGroup group : {OptionGroup::host, OptionGroup::network, OptionGroup::playing, OptionGroup::experimental})
        CHECK(std::string(caption(group)).size() > 0);
    // Another network option takes a third row of Network, and the page no longer fits above the actions.
    std::vector<OptionSpec> more = option_specs();
    for (int i = 0; i < 3; ++i)
        more.push_back({OptionGroup::network, icons::Icon::plug, "extra", "Extra", "", OptionKind::toggle, "Off", {}});
    const Layout crowded = build(more);
    float bottom = 0.0f;
    for (const Box &tile : crowded.tiles)
        bottom = std::max(bottom, tile.bottom());
    CHECK(bottom > crowded.actions.front().y);
}

std::string escape(const std::string &text)
{
    std::string out;
    for (char c : text)
    {
        if (c == '&')
            out += "&amp;";
        else if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else if (c == '"')
            out += "&quot;";
        else
            out += c;
    }
    return out;
}

} // namespace

void settings_layout_tests()
{
    const Layout &layout = page();
    geometry_tests(layout);
    text_tests(layout);
    navigation_tests(layout);
    experimental_tests(layout);
    group_tests();
}

// The settings page as the layout places it, with the options' default values; colours as in screen.cpp.
bool write_settings_svg(const char *path)
{
    const Layout &layout = page();
    const std::vector<OptionSpec> &specs = option_specs();
    std::string svg;
    const auto add = [&svg](const std::string &line) { svg += line + "\n"; };
    const auto num = [](float v) {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%.1f", static_cast<double>(v));
        return std::string(buffer);
    };
    const auto rect = [&](const Box &b, float radius, const char *fill, const char *extra) {
        add("<rect x=\"" + num(b.x) + "\" y=\"" + num(b.y) + "\" width=\"" + num(b.w) + "\" height=\"" + num(b.h) + "\" rx=\"" +
            num(radius) + "\" fill=\"" + fill + "\" " + extra + "/>");
    };
    const auto text = [&](float x, float y, float size, const char *weight, const char *fill, const std::string &value,
                          const char *extra = "") {
        add("<text x=\"" + num(x) + "\" y=\"" + num(y) + "\" font-size=\"" + num(size) + "\" font-weight=\"" + weight +
            "\" fill=\"" + fill + "\" " + extra + ">" + escape(value) + "</text>");
    };
    add("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1920\" height=\"1080\" viewBox=\"0 0 1920 1080\" "
        "font-family=\"Inter, 'Segoe UI', Arial, sans-serif\">");
    rect({0, 0, 1920, 1080}, 0, "#0b1029", "");
    text(150, 87, 24, "600", "#f6f3ee", "PROSPEROSUNSHINE", "letter-spacing=\"6\"");
    text(750, 87, 26, "600", "#aab1c8", "Home      Settings      Diagnostics");
    text(90, 250, 72, "700", "#f6f3ee", "Settings");
    text(420, 248, 24, "400", "#aab1c8", "sunshine.conf on this console. The host reads it when it starts.");
    for (const Caption &caption : layout.captions)
    {
        rect({caption.box.x, caption.box.y + 6.0f, 40.0f, 3.0f}, 1.5f, "#ffa62b", "");
        text(caption.box.x + 56.0f, caption.box.y + 16.0f, 22, "600", "#ffa62b", caption.text, "letter-spacing=\"4\"");
        if (caption.group == OptionGroup::experimental)
            text(caption.box.x + 80.0f + text_width(kInterSemiBold, caption.text, 22.0f, 4.0f), caption.box.y + 16.0f, 22, "400",
                 "#aab1c8", "For what is untested on this firmware. Off by default; what fails turns itself off.");
    }
    for (std::size_t i = 0; i < specs.size(); ++i)
    {
        const Box &b = layout.tiles[i];
        const OptionSpec &spec = specs[i];
        const bool toggle = spec.kind == OptionKind::toggle;
        rect(b, 24, "#18203f", i == 0 ? "stroke=\"#ffa62b\" stroke-width=\"3\"" : "stroke=\"#ffffff\" stroke-opacity=\"0.10\"");
        add("<circle cx=\"" + num(b.x + kIconX) + "\" cy=\"" + num(b.cy()) + "\" r=\"12\" fill=\"none\" stroke=\"#ffa62b\" "
            "stroke-width=\"2.5\"/>");
        text(b.x + kTextX, b.y + 37.0f, kLabelSize, "600", "#f6f3ee", spec.label);
        text(b.x + kTextX, b.y + 70.0f, kValueSize, "600", "#aab1c8", spec.default_label);
        if (toggle)
        {
            const Box track{b.right() - 84.0f, b.y + 46.0f, 64.0f, 32.0f};
            rect(track, 16, "#3a4363", "");
            add("<circle cx=\"" + num(track.x + 16.0f) + "\" cy=\"" + num(track.cy()) + "\" r=\"13\" fill=\"#ffffff\"/>");
        }
        else
        {
            const float w = text_width(kInterSemiBold, "DEFAULT", 15.0f, 2.0f) + 30.0f;
            const float end = b.right() - kTextRight;
            if (b.x + kTextX + text_width(kInterSemiBold, spec.default_label, kValueSize) <= end - w - 12.0f)
            {
                rect({end - w, b.y + 48.0f, w, 28.0f}, 14, "#ffffff", "fill-opacity=\"0.07\"");
                text(end - w / 2.0f, b.y + 67.5f, 15, "600", "#aab1c8", "DEFAULT", "text-anchor=\"middle\" letter-spacing=\"2\"");
            }
        }
    }
    for (int i = 0; i < kActions; ++i)
    {
        const Box &b = layout.actions[static_cast<std::size_t>(i)];
        rect(b, b.h / 2.0f, i == 0 ? "#3a3020" : "#18203f", "stroke=\"#ffffff\" stroke-opacity=\"0.10\"");
        text(b.x + 82.0f, b.cy() + 10.0f, 29, "600", i == 0 ? "#6f7694" : "#f6f3ee", i == 0 ? "Saved" : kActionLabels[i]);
    }
    rect(kHelpBar, 18, "#18203f", "stroke=\"#ffffff\" stroke-opacity=\"0.10\"");
    add("<circle cx=\"" + num(kHelpBar.x + 32.0f) + "\" cy=\"" + num(kHelpBar.cy()) + "\" r=\"6\" fill=\"#ffa62b\"/>");
    text(kHelpBar.x + 56.0f, kHelpBar.cy() + 8.0f, kHelpSize, "400", "#f6f3ee", specs[0].help);
    text(kHelpBar.right() - 30.0f, kHelpBar.cy() + 8.0f, 22, "400", "#aab1c8", std::string("Default: ") + specs[0].default_label,
         "text-anchor=\"end\"");
    text(1824, 1044, 22, "400", "#aab1c8", "Cross: Change    Square: Default    Triangle: Reload", "text-anchor=\"end\"");
    add("</svg>");
    std::FILE *file = std::fopen(path, "w");
    if (file == nullptr)
        return false;
    const bool ok = std::fwrite(svg.data(), 1, svg.size(), file) == svg.size();
    return std::fclose(file) == 0 && ok;
}
