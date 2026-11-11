// SPDX-License-Identifier: GPL-3.0-only
#include "input/keyboard_map.hpp"
#include <cassert>
#include <chrono>
#include <climits>
#include <iostream>
using namespace prospero;

int main() {
    assert(vk_usage('A') == 0x04 && vk_usage('Z') == 0x1D);
    assert(vk_usage('1') == 0x1E && vk_usage('9') == 0x26 && vk_usage('0') == 0x27);
    assert(vk_usage(0x0D) == 0x28 && vk_usage(0x1B) == 0x29 && vk_usage(0x20) == 0x2C);
    assert(vk_usage(0x70) == 0x3A && vk_usage(0x7B) == 0x45 && vk_usage(0x87) == 0x73);
    assert(vk_usage(0x27) == 0x4F && vk_usage(0x25) == 0x50 && vk_usage(0x28) == 0x51 && vk_usage(0x26) == 0x52);
    // Print Screen and Pause open the console's menus; the rest of the keyboard doesn't (10.20).
    static_assert(opens_console_menu(vk_usage(0x2C)) && opens_console_menu(vk_usage(0x13)));
    for (const uint16_t vk : {uint16_t('A'), uint16_t('0'), uint16_t(0x0D), uint16_t(0x1B), uint16_t(0x5D), uint16_t(0x70),
                              uint16_t(0x71), uint16_t(0x87), uint16_t(0x26), uint16_t(0x24), uint16_t(0x91), uint16_t(0x03)})
        assert(!opens_console_menu(vk_usage(vk)));
    assert(!opens_console_menu(0));
    assert(vk_usage(0x60) == 0x62 && vk_usage(0x61) == 0x59 && vk_usage(0x69) == 0x61);
    assert(vk_usage(0xBF) == 0x38 && vk_usage(0xE2) == 0x64);
    assert(vk_usage(0x10) == 0 && vk_usage(0xA0) == 0 && vk_usage(0x5B) == 0);  // modifiers have no usage here
    assert(vk_usage(0xAD) == 0);  // volume mute: not forwarded
    assert(vk_modifier(0xA0) == 0x02 && vk_modifier(0x10) == 0x02 && vk_modifier(0xA1) == 0x20);
    assert(vk_modifier(0x11) == 0x01 && vk_modifier(0xA5) == 0x40 && vk_modifier(0x5C) == 0x80);
    assert(vk_modifier('A') == 0);

    assert(ascii_key('a').usage == 0x04 && !ascii_key('a').shift);
    assert(ascii_key('Q').usage == 0x14 && ascii_key('Q').shift);
    assert(ascii_key('0').usage == 0x27 && !ascii_key('0').shift);
    assert(ascii_key(')').usage == 0x27 && ascii_key(')').shift);
    assert(ascii_key('@').usage == 0x1F && ascii_key('@').shift);
    assert(ascii_key(' ').usage == 0x2C && ascii_key('\n').usage == 0x28);
    assert(ascii_key('?').usage == 0x38 && ascii_key('?').shift);
    assert(ascii_key('/').usage == 0x38 && !ascii_key('/').shift);
    assert(ascii_key('"').usage == 0x34 && ascii_key('"').shift);
    assert(ascii_key('\x7f').usage == 0 && ascii_key('\0').usage == 0);

    KeyboardState keys;
    assert(keys.empty());
    assert(keys.update('A', false));
    assert(!keys.update('A', false));  // a repeat while held
    assert(keys.update(0x27, false));
    assert(keys.count() == 2 && keys.usages()[0] == 0x04 && keys.usages()[1] == 0x4F);
    assert(keys.update('A', true));
    assert(keys.count() == 1 && keys.usages()[0] == 0x4F);
    assert(!keys.update('A', true));
    assert(!keys.update(0xAD, false));  // no usage: ignored
    assert(keys.update(0x27, true) && keys.empty());

    // Sunshine presses the generic Shift around a key that needs it while the client may hold left Shift.
    assert(keys.update(0xA0, false) && keys.modifiers() == 0x02);
    assert(!keys.update(0x10, false));
    assert(!keys.update(0x10, true) && keys.modifiers() == 0x02);
    assert(keys.update(0xA0, true) && keys.modifiers() == 0);
    assert(keys.update(0xA3, false) && keys.update(0xA4, false) && keys.modifiers() == 0x14);
    keys.clear();
    assert(keys.empty());

    for (uint16_t vk = 'A'; vk < 'A' + KeyboardState::max_keys; vk++) assert(keys.update(vk, false));
    assert(!keys.update('Z', false) && keys.count() == KeyboardState::max_keys);
    assert(keys.update('B', true) && keys.update('Z', false) && keys.usages()[KeyboardState::max_keys - 1] == 0x1D);

    assert(mouse_button_bit(1) == 0x1 && mouse_button_bit(2) == 0x4 && mouse_button_bit(3) == 0x2);
    assert(mouse_button_bit(4) == 0x8 && mouse_button_bit(5) == 0x10 && mouse_button_bit(0) == 0 && mouse_button_bit(6) == 0);

    WheelSteps wheel;
    assert(wheel.add(120) == 1);
    assert(wheel.add(-240) == -2);
    assert(wheel.add(60) == 0 && wheel.add(60) == 1);  // high-resolution wheels add up
    assert(wheel.add(-30) == 0 && wheel.add(-100) == -1);
    wheel.clear();
    assert(wheel.add(120 * 200) == 127 && wheel.add(0) == 73);
    // A client that sends the largest distance in every report scrolls at full speed, and what waits stays bounded (the
    // count would overflow after about 122 000 such reports otherwise).
    wheel.clear();
    for (int i = 0; i < 200'000; i++) assert(wheel.add(32767) == 127);
    int waiting = 0;
    for (int steps; (steps = wheel.add(0)) != 0;) waiting += steps;
    assert(waiting == (32767 - 127 * 120) / 120);
    wheel.clear();
    assert(wheel.add(INT_MAX) == 127 && wheel.add(INT_MIN) == -127 && wheel.add(INT_MIN) == -127);

    // Windows+Esc is PS: on the keyboard pad (ps_chord) and, for a client with a controller, on that controller (PsChord).
    static_assert(ps_chord(0x1B, 0x08) && ps_chord(0x1B, 0x80) && ps_chord(0x1B, 0x8B));
    static_assert(!ps_chord(0x1B, 0) && !ps_chord(0x1B, 0x77) && !ps_chord(0x70, 0x08));
    {
        using A = PsChord::Action;
        PsChord chord;
        assert(chord.key(0x1B, false) == A::pass && chord.key(0x1B, true) == A::pass);  // Esc alone
        for (const uint16_t windows : {uint16_t(0x5B), uint16_t(0x5C)}) {
            assert(chord.key(windows, false) == A::pass);  // Windows still reaches the keyboard
            assert(chord.key(0x1B, false) == A::press && chord.held());
            assert(chord.key(0x1B, false) == A::swallow);  // the key repeat
            assert(chord.key('A', false) == A::pass);
            assert(chord.key(0x1B, true) == A::release && !chord.held());
            assert(chord.key(0x1B, false) == A::press);  // again while Windows stays down (the iPhone's toggle)
            assert(chord.key(windows, true) == A::pass);
            assert(chord.key(0x1B, true) == A::release);  // PS comes up with Esc even after Windows did
            assert(chord.key(0x1B, false) == A::pass && chord.key(0x1B, true) == A::pass);
        }
        // Left and right Windows both down: one up keeps the chord.
        chord.key(0x5B, false);
        chord.key(0x5C, false);
        chord.key(0x5B, true);
        assert(chord.key(0x1B, false) == A::press && chord.key(0x1B, true) == A::release);
        chord.key(0x5C, true);
        // Esc held before Windows stays Esc, its repeats too.
        assert(chord.key(0x1B, false) == A::pass);
        chord.key(0x5B, false);
        assert(chord.key(0x1B, false) == A::pass);
        assert(chord.key(0x1B, true) == A::pass);
        // Windows+Shift+Esc holds PS (either Shift, generic or sided); Shift alone with Esc is Esc.
        static_assert(ps_long_chord(0x1B, 0x0A) && ps_long_chord(0x1B, 0xA0) && !ps_long_chord(0x1B, 0x08));
        static_assert(!ps_long_chord(0x1B, 0x02) && !ps_long_chord(0x70, 0x0A));
        static_assert(ps_long_hold >= std::chrono::milliseconds(650));
        for (const uint16_t shift : {uint16_t(0x10), uint16_t(0xA0), uint16_t(0xA1)}) {
            chord.key(0x5B, false);
            assert(chord.key(shift, false) == A::pass);
            assert(chord.key(0x1B, false) == A::press_long && chord.key(0x1B, false) == A::swallow);
            assert(chord.key(0x1B, true) == A::release);
            assert(chord.key(shift, true) == A::pass);
            assert(chord.key(0x1B, false) == A::press && chord.key(0x1B, true) == A::release);  // Shift up: a press again
            chord.key(0x5B, true);
        }
        chord.key(0xA0, false);
        assert(chord.key(0x1B, false) == A::pass && chord.key(0x1B, true) == A::pass);
        chord.key(0xA0, true);
        // Nothing to press PS on: cancel, and Esc and its release go on as Esc.
        chord.key(0x5B, false);
        assert(chord.key(0x1B, false) == A::press);
        chord.cancel();
        assert(chord.key(0x1B, true) == A::pass);
    }

    std::cout << "PASS: keyboard and mouse map (keys, modifiers, ASCII, held keys, buttons, wheel steps, Windows+Esc)\n";
}
