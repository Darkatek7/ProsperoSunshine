// SPDX-License-Identifier: GPL-3.0-only
// Moonlight gamepad state (Xbox layout, platf::gamepad_state_t) → PS5 virtual DualSense state.
// Portable so it can be unit-tested on the build host; docs/input-mapping.md lists the table.
#pragma once
#include <array>
#include <cstdint>

namespace prospero {

// Moonlight button bits (Sunshine's platform/common.h).
namespace moonlight {
inline constexpr uint32_t dpad_up = 0x0001, dpad_down = 0x0002, dpad_left = 0x0004, dpad_right = 0x0008;
inline constexpr uint32_t start = 0x0010, back = 0x0020, left_stick = 0x0040, right_stick = 0x0080;
inline constexpr uint32_t left_button = 0x0100, right_button = 0x0200, home = 0x0400;
inline constexpr uint32_t a = 0x1000, b = 0x2000, x = 0x4000, y = 0x8000;
inline constexpr uint32_t touchpad = 0x100000;
// The button below the Xbox button on an Xbox Series controller (Share), sent as Sunshine's MISC_BUTTON.
inline constexpr uint32_t misc = 0x200000;
}

// ScePadButton bits (src/platform/ps5/pad.h).
namespace dualsense {
inline constexpr uint32_t create = 0x1, l3 = 0x2, r3 = 0x4, options = 0x8;
inline constexpr uint32_t up = 0x10, right = 0x20, down = 0x40, left = 0x80;
inline constexpr uint32_t l2 = 0x100, r2 = 0x200, l1 = 0x400, r1 = 0x800;
inline constexpr uint32_t triangle = 0x1000, circle = 0x2000, cross = 0x4000, square = 0x8000;
inline constexpr uint32_t ps = 0x10000, touchpad = 0x100000;
}

struct PadState {
    uint32_t buttons;
    uint8_t lx, ly, rx, ry, l2, r2;
};

// The host's pad plays as the console's user; every other pad is a guest the console asked about (pad_roles.hpp). Unless
// the owner allows it for all (ps_button_all_players), a guest's PS and Create don't go through, so a guest can't leave
// the game, open the control center, reach the settings or open the Create menu, which captures and broadcasts what the
// console shows. Sunshine has already turned a held Select into home here, so that way is closed too.
enum class PadRole { host, guest };
constexpr uint32_t allowed_buttons(uint32_t flags, PadRole role, bool all_players) {
    return role == PadRole::host || all_players ? flags : flags & ~(moonlight::home | moonlight::back);
}

// Moonlight's controller capabilities in the arrival message (LI_CCAP_TOUCHPAD).
inline constexpr uint16_t capability_touchpad = 0x08;
constexpr bool has_touchpad(uint16_t capabilities, uint32_t supported_buttons) {
    return (capabilities & capability_touchpad) || (supported_buttons & moonlight::touchpad);
}

// A controller without a touchpad (an Xbox one, mostly; its Share button seldom reaches Moonlight, as Windows keeps it)
// clicks the touchpad with Select and Start together, the buttons in the places of Create and Options. Pressed together,
// Start is the touchpad until it comes up, and neither Create nor Options goes out. Sunshine turns Select held for
// back_button_timeout into PS (and forces Select up first): that PS is dropped for a while after the combination starts,
// as the timer ran from the Select press, which came before. Times are in milliseconds of any steady clock.
struct TouchpadCombo {
    bool active = false;
    int64_t quiet_until = 0;  // Sunshine's PS from the held Select is dropped until then

    // ps_hold_ms: back_button_timeout, negative when Sunshine makes no PS from Select.
    constexpr uint32_t apply(uint32_t flags, int64_t now, int64_t ps_hold_ms) {
        constexpr uint32_t pair = moonlight::back | moonlight::start;
        if (!active && (flags & pair) == pair) {
            active = true;
            quiet_until = now + (ps_hold_ms > 0 ? ps_hold_ms : 0) + 300;
        }
        if (active && !(flags & pair)) active = false;
        if (active) {
            const bool click = flags & moonlight::start;
            flags &= ~pair;
            if (click) flags |= moonlight::touchpad;
        }
        if (now < quiet_until) flags &= ~moonlight::home;
        return flags;
    }
    // While true, a Select coming up is not a tap of Create.
    constexpr bool busy(int64_t now) const { return active || now < quiet_until; }
};

// A trigger counts as pressed (the digital L2/R2 bit) above this analog value.
inline constexpr uint8_t trigger_threshold = 8;

// A guest's pad opens on the console only once its controller is used (GitHub issue 26): some clients announce a
// controller that never sends anything (Steam Input's virtual one next to the real one, on-screen controls), and the
// console's "Who's using this controller?" for it can only be answered by that controller, so it blocked the screen.
// A button, a trigger past the threshold or a stick pushed a quarter of the way counts; stick drift doesn't.
inline constexpr int stick_wake = 8192;
constexpr bool wakes_pad(uint32_t flags, uint8_t lt, uint8_t rt, int16_t ls_x, int16_t ls_y, int16_t rs_x, int16_t rs_y) {
    const auto far = [](int16_t v) { return v >= stick_wake || v <= -stick_wake; };
    return flags || lt >= trigger_threshold || rt >= trigger_threshold || far(ls_x) || far(ls_y) || far(rs_x) || far(rs_y);
}

// Moonlight sticks are signed 16-bit with +Y up; DualSense sticks are 0..255 with 0 at the top.
constexpr uint8_t stick_axis(int16_t value, bool invert) {
    int v = (static_cast<int>(value) + 32768) >> 8;  // 0..255
    if (invert) v = 255 - v;
    return static_cast<uint8_t>(v);
}

constexpr PadState map_gamepad(uint32_t flags, uint8_t lt, uint8_t rt, int16_t ls_x, int16_t ls_y, int16_t rs_x,
                               int16_t rs_y) {
    struct Pair { uint32_t from, to; };
    constexpr Pair table[] = {
        {moonlight::a, dualsense::cross},         {moonlight::b, dualsense::circle},
        {moonlight::x, dualsense::square},        {moonlight::y, dualsense::triangle},
        {moonlight::dpad_up, dualsense::up},      {moonlight::dpad_down, dualsense::down},
        {moonlight::dpad_left, dualsense::left},  {moonlight::dpad_right, dualsense::right},
        {moonlight::left_button, dualsense::l1},  {moonlight::right_button, dualsense::r1},
        {moonlight::left_stick, dualsense::l3},   {moonlight::right_stick, dualsense::r3},
        {moonlight::start, dualsense::options},   {moonlight::back, dualsense::create},
        {moonlight::home, dualsense::ps},         {moonlight::touchpad, dualsense::touchpad},
        // An Xbox controller has no touchpad; its Share button stands in for the touchpad click.
        {moonlight::misc, dualsense::touchpad},
    };
    uint32_t buttons = 0;
    for (const auto &p : table)
        if (flags & p.from) buttons |= p.to;
    if (lt >= trigger_threshold) buttons |= dualsense::l2;
    if (rt >= trigger_threshold) buttons |= dualsense::r2;
    return {buttons, stick_axis(ls_x, false), stick_axis(ls_y, true), stick_axis(rs_x, false), stick_axis(rs_y, true),
            lt, rt};
}

// Moonlight's event flags in its trigger effect message.
inline constexpr uint8_t trigger_flag_right = 0x04, trigger_flag_left = 0x08;

// The block the console keeps for a pad: a mask, then mode and ten parameters for L2 and for R2, one spare byte
// (docs/re/adaptive-triggers.md).
using TriggerBlock = std::array<uint8_t, 24>;
inline constexpr TriggerBlock triggers_off = {0x03, 0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x05};

struct TriggerEffects {
    uint8_t flags;  // the triggers that differ from the last block
    uint8_t type_left, type_right;
    std::array<uint8_t, 10> left, right;
};

constexpr TriggerEffects map_trigger_effects(const TriggerBlock &block, const TriggerBlock &last) {
    TriggerEffects effects{};
    effects.type_left = block[1];
    effects.type_right = block[12];
    for (int i = 0; i < 10; ++i) {
        effects.left[i] = block[2 + i];
        effects.right[i] = block[13 + i];
    }
    for (int i = 1; i <= 11; ++i) {
        if (block[i] != last[i]) effects.flags |= trigger_flag_left;
        if (block[11 + i] != last[11 + i]) effects.flags |= trigger_flag_right;
    }
    return effects;
}

}
