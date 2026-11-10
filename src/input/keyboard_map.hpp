// SPDX-License-Identifier: GPL-3.0-only
// Moonlight keyboard and mouse (Windows virtual-key codes, Sunshine's button numbers, 120-step wheel) → the PS5's
// virtual keyboard and mouse (HID usages and modifier bits, SceMouseData buttons). Portable so it can be unit-tested
// on the build host; docs/re/keyboard-mouse.md has the device side.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace prospero {

// HID modifier bits for a modifier key, else 0. The generic Shift, Ctrl and Alt count as the left ones.
constexpr uint8_t vk_modifier(uint16_t vk) {
    switch (vk) {
    case 0x11: case 0xA2: return 0x01;  // Ctrl, left Ctrl
    case 0x10: case 0xA0: return 0x02;  // Shift, left Shift
    case 0x12: case 0xA4: return 0x04;  // Alt, left Alt
    case 0x5B: return 0x08;             // left Windows
    case 0xA3: return 0x10;             // right Ctrl
    case 0xA1: return 0x20;             // right Shift
    case 0xA5: return 0x40;             // right Alt (AltGr)
    case 0x5C: return 0x80;             // right Windows
    default: return 0;
    }
}

// The HID usage (keyboard page) for a key that is not a modifier, else 0. Positions are those of a US keyboard;
// the console's own layout setting decides which character a position types.
constexpr uint8_t vk_usage(uint16_t vk) {
    if (vk >= 'A' && vk <= 'Z') return static_cast<uint8_t>(0x04 + vk - 'A');
    if (vk >= '1' && vk <= '9') return static_cast<uint8_t>(0x1E + vk - '1');
    if (vk >= 0x70 && vk <= 0x7B) return static_cast<uint8_t>(0x3A + vk - 0x70);  // F1..F12
    if (vk >= 0x7C && vk <= 0x87) return static_cast<uint8_t>(0x68 + vk - 0x7C);  // F13..F24
    if (vk >= 0x61 && vk <= 0x69) return static_cast<uint8_t>(0x59 + vk - 0x61);  // numpad 1..9
    switch (vk) {
    case '0': return 0x27;
    case 0x0D: return 0x28;  // Enter
    case 0x1B: return 0x29;  // Escape
    case 0x08: return 0x2A;  // Backspace
    case 0x09: return 0x2B;  // Tab
    case 0x20: return 0x2C;  // Space
    case 0xBD: return 0x2D;  // - _
    case 0xBB: return 0x2E;  // = +
    case 0xDB: return 0x2F;  // [ {
    case 0xDD: return 0x30;  // ] }
    case 0xDC: return 0x31;  // \ |
    case 0xBA: return 0x33;  // ; :
    case 0xDE: return 0x34;  // ' "
    case 0xC0: return 0x35;  // ` ~
    case 0xBC: return 0x36;  // , <
    case 0xBE: return 0x37;  // . >
    case 0xBF: return 0x38;  // / ?
    case 0x14: return 0x39;  // Caps Lock
    case 0x2C: return 0x46;  // Print Screen
    case 0x91: return 0x47;  // Scroll Lock
    case 0x13: return 0x48;  // Pause
    case 0x2D: return 0x49;  // Insert
    case 0x24: return 0x4A;  // Home
    case 0x21: return 0x4B;  // Page Up
    case 0x2E: return 0x4C;  // Delete
    case 0x23: return 0x4D;  // End
    case 0x22: return 0x4E;  // Page Down
    case 0x27: return 0x4F;  // Right
    case 0x25: return 0x50;  // Left
    case 0x28: return 0x51;  // Down
    case 0x26: return 0x52;  // Up
    case 0x90: return 0x53;  // Num Lock
    case 0x6F: return 0x54;  // numpad /
    case 0x6A: return 0x55;  // numpad *
    case 0x6D: return 0x56;  // numpad -
    case 0x6B: return 0x57;  // numpad +
    case 0x60: return 0x62;  // numpad 0
    case 0x6E: return 0x63;  // numpad .
    case 0xE2: return 0x64;  // the key next to left Shift on ISO keyboards
    case 0x5D: return 0x65;  // Menu
    default: return 0;
    }
}

// The keys that open the console's own menus from a keyboard seated in a profile, as PS and Create do on a controller
// (control/menu_access.hpp): Print Screen opens the Create menu, Pause the control center. Measured on 10.20 by pressing
// every key of a full keyboard on the home screen: no other key did, nor Ctrl+Esc, Alt+Tab, Ctrl+Alt+Del, Alt+F4,
// Ctrl+Shift+Esc or Windows+G.
constexpr bool opens_console_menu(uint8_t usage) { return usage == 0x46 || usage == 0x48; }

// Windows+Esc (either Windows key, ⌘ on a Mac or an iPad keyboard) is PS, as F1 is on the keyboard pad. Moonlight for
// iPhone and iPad has no PS button: neither its on-screen controller nor the bar above its on-screen keyboard (three
// fingers on the screen) has one, nor F1, but the bar has Windows, which stays down until it is tapped again, and Esc.
// modifiers: the HID modifier bits held (vk_modifier).
constexpr bool ps_chord(uint16_t vk, uint8_t modifiers) { return vk == 0x1B && (modifiers & 0x88); }

// The same for a client whose keys go to the console as a keyboard while it has a controller (the on-screen one, say):
// its Esc with Windows held is that controller's PS, held while Esc is, and Esc doesn't reach the keyboard. One per client.
class PsChord {
public:
    // pass: the key goes on as it is. press: PS goes down. release: PS comes up. swallow: Esc's repeat while PS is down.
    enum class Action { pass, press, release, swallow };

    Action key(uint16_t vk, bool release) {
        if (const uint8_t bit = vk_modifier(vk); bit & 0x88) {
            windows_ = release ? windows_ & ~bit : windows_ | bit;
            return Action::pass;
        }
        if (vk != 0x1B) return Action::pass;
        if (release) {
            escape_ = false;
            return held_ ? (held_ = false, Action::release) : Action::pass;
        }
        if (held_) return Action::swallow;
        // Esc that went down as Esc stays Esc while it repeats, even once Windows is down too.
        if (escape_ || !ps_chord(vk, windows_)) {
            escape_ = true;
            return Action::pass;
        }
        held_ = true;
        return Action::press;
    }

    // PS couldn't go down (no controller to press it on): Esc goes on as Esc, its release too.
    void cancel() {
        held_ = false;
        escape_ = true;
    }
    bool held() const { return held_; }

private:
    uint8_t windows_ = 0;
    bool held_ = false;    // Esc is down as PS
    bool escape_ = false;  // Esc is down as Esc
};

// What a printable ASCII character is on a US keyboard: its key and whether it needs Shift. Usage 0 if none.
struct AsciiKey {
    uint8_t usage;
    bool shift;
};

constexpr AsciiKey ascii_key(char c) {
    if (c >= 'a' && c <= 'z') return {static_cast<uint8_t>(0x04 + c - 'a'), false};
    if (c >= 'A' && c <= 'Z') return {static_cast<uint8_t>(0x04 + c - 'A'), true};
    if (c >= '1' && c <= '9') return {static_cast<uint8_t>(0x1E + c - '1'), false};
    constexpr char plain[] = "0\n\t -=[]\\;'`,./";
    constexpr uint8_t plain_usage[] = {0x27, 0x28, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38};
    constexpr char shifted[] = ")!@#$%^&*(_+{}|:\"~<>?";
    constexpr uint8_t shifted_usage[] = {0x27, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x2D,
                                         0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38};
    for (unsigned i = 0; i < sizeof plain_usage; i++) {
        if (plain[i] == c) return {plain_usage[i], false};
    }
    for (unsigned i = 0; i < sizeof shifted_usage; i++) {
        if (shifted[i] == c) return {shifted_usage[i], true};
    }
    return {0, false};
}

// The held keys of one Moonlight keyboard, turned into the device's report: modifier bits and up to 16 usages in
// the order they went down. A key Moonlight repeats while held changes nothing.
class KeyboardState {
public:
    static constexpr int max_keys = 16;

    // Returns whether the report changed. Keys without a usage are ignored.
    bool update(uint16_t vk, bool release) {
        if (const uint8_t bit = vk_modifier(vk)) {
            // The generic and the sided code of one modifier can both be down; the bit stays until both are up.
            const int slot = generic_slot(vk);
            const uint8_t before = modifiers();
            if (release) held_modifiers_[slot] &= static_cast<uint8_t>(~bit);
            else held_modifiers_[slot] |= bit;
            return modifiers() != before;
        }
        const uint8_t usage = vk_usage(vk);
        if (!usage) return false;
        uint8_t *const end = usages_.data() + count_;
        uint8_t *const found = std::find(usages_.data(), end, usage);
        if (release) {
            if (found == end) return false;
            std::copy(found + 1, end, found);
            --count_;
            return true;
        }
        if (found != end || count_ == max_keys) return false;
        usages_[count_++] = usage;
        return true;
    }

    void clear() {
        held_modifiers_ = {};
        count_ = 0;
    }

    uint8_t modifiers() const { return held_modifiers_[0] | held_modifiers_[1]; }
    const uint8_t *usages() const { return usages_.data(); }
    uint8_t count() const { return count_; }
    bool empty() const { return !modifiers() && !count_; }

private:
    static constexpr int generic_slot(uint16_t vk) { return vk == 0x10 || vk == 0x11 || vk == 0x12 ? 1 : 0; }

    std::array<uint8_t, 2> held_modifiers_{};  // sided codes, generic codes
    std::array<uint8_t, max_keys> usages_{};
    uint8_t count_ = 0;
};

// Sunshine's mouse buttons (BUTTON_LEFT 1 ... BUTTON_X2 5) → SceMouseData bits.
constexpr uint8_t mouse_button_bit(int button) {
    switch (button) {
    case 1: return 0x1;   // left: primary
    case 2: return 0x4;   // middle
    case 3: return 0x2;   // right: secondary
    case 4: return 0x8;   // back
    case 5: return 0x10;  // forward
    default: return 0;
    }
}

// Moonlight scrolls in 1/120 steps (WHEEL_DELTA); the mouse reports whole steps. Takes a distance, returns the
// whole steps to send and keeps the rest for the next one. Positive is away from the user.
class WheelSteps {
public:
    int8_t add(int distance) {
        rest_ += distance;
        const int steps = std::clamp(rest_ / 120, -127, 127);
        rest_ -= steps * 120;
        return static_cast<int8_t>(steps);
    }

    void clear() { rest_ = 0; }

private:
    int rest_ = 0;
};

}  // namespace prospero
