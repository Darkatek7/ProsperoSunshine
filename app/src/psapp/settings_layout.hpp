// SPDX-License-Identifier: GPL-3.0-only
// Where the settings page puts its tiles and buttons, and where the D-pad goes from each, in the 1920x1080 canvas.
// Computed from the option list (each OptionSpec names its group), so a new option in conf.cpp needs no number changed
// here or in screen.cpp; the app's tests check that it still fits. Pure code, tested on the PC (app/tests), which also
// draws it as an SVG for a look without the console.
//
// The page, top to bottom: a band with Host and Playing side by side, Network in rows of three, the Experimental switches
// in one row, then Save, Save and restart, Revert and (at the right end) the web page, and the help bar. Each group has
// a caption line over its tiles, as the Experimental row had alone before.
#pragma once

#include "app/psapp/conf.hpp"

#include <string>
#include <vector>

namespace psapp::settings_layout
{

struct Box
{
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    float right() const
    {
        return x + w;
    }
    float bottom() const
    {
        return y + h;
    }
    float cx() const
    {
        return x + w / 2.0f;
    }
    float cy() const
    {
        return y + h / 2.0f;
    }
    bool overlaps(const Box &o) const
    {
        return x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom();
    }
};

// The area the tiles may use: the width of the page's other parts, under its title.
inline constexpr float kLeft = 96.0f;
inline constexpr float kRight = 1824.0f;
inline constexpr float kTop = 274.0f;
inline constexpr float kTileHeight = 88.0f;
inline constexpr float kCaptionHeight = 34.0f; // the caption's line over a group, with the gap to its tiles
inline constexpr float kBandGap = 12.0f;       // between one band's tiles and the next band's caption
inline constexpr float kRowGap = 12.0f;        // between the rows of one group
inline constexpr float kTileGap = 20.0f;       // between the tiles of one row
inline constexpr float kGroupGap = 40.0f;      // between two groups side by side
// Inside a tile: the icon's centre, where its text starts and the room kept at its right, the text sizes.
inline constexpr float kIconX = 36.0f;
inline constexpr float kTextX = 66.0f;
inline constexpr float kTextRight = 16.0f;
inline constexpr float kLabelSize = 26.0f;
inline constexpr float kValueSize = 24.0f;
// Under the tiles: the four actions, then the bar with the focused part's help.
inline constexpr int kActions = 4;
inline constexpr float kActionsTop = 780.0f;
inline constexpr Box kHelpBar{96.0f, 874.0f, 1728.0f, 58.0f};
inline constexpr float kHelpSize = 24.0f;      // the help's text, 22 when it and the default don't fit side by side
inline constexpr float kHelpSmallSize = 22.0f;

struct Caption
{
    OptionGroup group;
    const char *text;
    Box box; // the caption's line: its text starts at the left, an accent bar in front of it
};

struct Layout
{
    std::vector<Box> tiles;   // one per option, in the order of the list it was built from
    std::vector<Box> actions; // Save, Save and restart, Revert, the web page
    std::vector<Caption> captions;
    std::string error; // which option has no place; empty when all have one

    int items() const
    {
        return static_cast<int>(tiles.size() + actions.size());
    }
    // The tiles, then the actions: the screen's focus index.
    const Box &item(int index) const
    {
        const std::size_t i = static_cast<std::size_t>(index);
        return i < tiles.size() ? tiles[i] : actions[i - tiles.size()];
    }
};

Layout build(const std::vector<OptionSpec> &specs);
// build(option_specs()), once.
const Layout &page();

const char *caption(OptionGroup group);

enum class Move
{
    up,
    down,
    left,
    right,
};
// Where the D-pad goes from `item` (Layout::item's index), or -1 at an edge. Left and right stay in the row; up and
// down go to the nearest row above or below and there to the part whose centre is nearest.
int next(const Layout &layout, int item, Move move);

} // namespace psapp::settings_layout
