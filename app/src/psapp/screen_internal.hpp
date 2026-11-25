// SPDX-License-Identifier: GPL-3.0-only
// What the screen's sources share (screen.cpp, screen_sheets.cpp, screen_sheets_draw.cpp, screen_draw.cpp): the
// colours, the sizes more than one of them uses and small helpers. Only those files include it.
#pragma once

#include "app/psapp/icons.hpp"
#include "app/psapp/settings_layout.hpp"
#include "core/input.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/font.hpp"
#include "ui/fonts.hpp"

#include <algorithm>
#include <string>

namespace psapp
{

using hui::Direction;
using hui::gfx::Align;
using hui::gfx::Color;
using hui::gfx::Rect;
using icons::Icon;
using InputAction = hui::Action;

// Night blue with one strong colour: the sun's amber.
inline const Color kInk = Color::rgb(0xf6f3ee);
inline const Color kMuted = Color::rgb(0xaab1c8);
inline const Color kFaint = Color::rgb(0x6f7694);
inline const Color kAccent = Color::rgb(0xffa62b);
inline const Color kAccentDeep = Color::rgb(0xff6a1a);
inline const Color kGood = Color::rgb(0x45d39a);
inline const Color kBad = Color::rgb(0xff6b6b);
inline const Color kClear = Color::rgb(0x000000, 0.0f);

inline constexpr float kArmSeconds = 4.0f;
inline constexpr int kDeviceRows = 6; // the device list's rows in view
inline const Rect kPairedDevices{1220.0f, 740.0f, 604.0f, 172.0f};
inline constexpr int kReportVisible = 19; // lines of the report on screen

// The settings page's tiles, one per option_specs() entry, and its actions under them (settings_layout.hpp): Save, Save
// and restart, Revert, and at the right end the web page. The focus counts the tiles first, then the actions.
namespace layout = settings_layout;
inline int settings_tiles()
{
    return static_cast<int>(layout::page().tiles.size());
}
inline Rect rect_of(const layout::Box &box)
{
    return {box.x, box.y, box.w, box.h};
}
// The web page's sheet: its height, where its rows start in it, and their pitch.
inline constexpr float kWebSheetHeight = 770.0f;
inline constexpr float kWebRowsTop = 474.0f;
inline constexpr float kWebRowStep = 68.0f;
inline constexpr int kWebRows = 4;

inline float ease_out(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

inline std::string shorten(const hui::ui::FontRef &font, const std::string &text, float size, float width)
{
    if (font.measure(text, size) <= width)
        return text;
    return font.font->fit(text + " \xE2\x80\xA6", size, width);
}

} // namespace psapp
