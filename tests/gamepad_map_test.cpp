// SPDX-License-Identifier: GPL-3.0-only
#include "input/gamepad_map.hpp"
#include <cassert>
#include <iostream>
using namespace prospero;

int main() {
    const auto idle = map_gamepad(0, 0, 0, 0, 0, 0, 0);
    assert(idle.buttons == 0);
    assert(idle.lx == 128 && idle.ly == 127 && idle.rx == 128 && idle.ry == 127);  // centre within one step
    const auto face = map_gamepad(moonlight::a | moonlight::b | moonlight::x | moonlight::y, 0, 0, 0, 0, 0, 0);
    assert(face.buttons == (dualsense::cross | dualsense::circle | dualsense::square | dualsense::triangle));
    const auto system = map_gamepad(moonlight::home | moonlight::start | moonlight::back | moonlight::touchpad, 0, 0, 0,
                                    0, 0, 0);
    assert(system.buttons == (dualsense::ps | dualsense::options | dualsense::create | dualsense::touchpad));
    // An Xbox controller's Share button is the touchpad click; with both held it is still one press.
    assert(map_gamepad(moonlight::misc, 0, 0, 0, 0, 0, 0).buttons == dualsense::touchpad);
    assert(map_gamepad(moonlight::misc | moonlight::touchpad | moonlight::a, 0, 0, 0, 0, 0, 0).buttons ==
           (dualsense::touchpad | dualsense::cross));
    // A controller without a touchpad: Select and Start together are the touchpad click.
    static_assert(has_touchpad(capability_touchpad, 0) && has_touchpad(0, moonlight::touchpad));
    static_assert(!has_touchpad(0x43, 0xF7FF));  // an Xbox controller on Moonlight for PC
    {
        constexpr uint32_t sel = moonlight::back, start = moonlight::start;
        TouchpadCombo c;
        assert(c.apply(sel, 0, 500) == sel && !c.busy(0));                       // Select alone stays Select
        assert(c.apply(sel | start | moonlight::a, 100, 500) == (moonlight::touchpad | moonlight::a));
        assert(c.busy(100));
        assert(c.apply(start, 500, 500) == moonlight::touchpad);                 // Sunshine forced Select up: still held
        assert(c.apply(start | moonlight::home, 501, 500) == moonlight::touchpad); // its PS from the held Select is dropped
        assert(c.apply(0, 700, 500) == 0 && !c.active && c.busy(700));
        assert(c.apply(moonlight::home, 950, 500) == moonlight::home && !c.busy(950));  // a real PS later goes through
        // Start first, then Select: Options went out already, then it becomes the touchpad.
        TouchpadCombo d;
        assert(d.apply(start, 0, 500) == start);
        assert(d.apply(start | sel, 50, 500) == moonlight::touchpad);
        assert(d.apply(sel, 80, 500) == 0);                                       // Start up: click over, Select swallowed
        assert(d.apply(0, 120, 500) == 0 && !d.active);
        assert(d.apply(start, 2000, 500) == start);                               // later, Start is Options again
        // Without Sunshine's held-Select PS the quiet time is short.
        TouchpadCombo e;
        e.apply(sel | start, 0, -1);
        e.apply(0, 10, -1);
        assert(!e.busy(300));
    }
    const auto corners = map_gamepad(0, 0, 0, -32768, 32767, 32767, -32768);
    assert(corners.lx == 0 && corners.ly == 0);      // full left, full up
    assert(corners.rx == 255 && corners.ry == 255);  // full right, full down
    const auto triggers = map_gamepad(0, trigger_threshold - 1, 255, 0, 0, 0, 0);
    assert(!(triggers.buttons & dualsense::l2) && (triggers.buttons & dualsense::r2));
    assert(triggers.l2 == trigger_threshold - 1 && triggers.r2 == 255);
    // As Teardown sends it: resistance on the second block only.
    const TriggerBlock feedback = {0x03, 0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x21, 0xfe, 0x03, 0x48, 0x92, 0x24, 0x09};
    const auto on = map_trigger_effects(feedback, triggers_off);
    assert(on.flags == trigger_flag_right);
    assert(on.type_left == 0x05 && on.type_right == 0x21);
    assert(on.right[0] == 0xfe && on.right[5] == 0x09 && on.right[9] == 0 && on.left[0] == 0);
    assert(map_trigger_effects(triggers_off, feedback).flags == trigger_flag_right);
    assert(map_trigger_effects(triggers_off, TriggerBlock{}).flags == (trigger_flag_left | trigger_flag_right));
    TriggerBlock left_only = triggers_off;
    left_only[11] = 1;  // last parameter of the first block
    assert(map_trigger_effects(left_only, triggers_off).flags == trigger_flag_left);
    TriggerBlock mask_only = triggers_off;
    mask_only[0] = 0x01;
    mask_only[23] = 0xff;
    assert(map_trigger_effects(mask_only, triggers_off).flags == 0);
    // Only the host has PS and Create unless every player may.
    constexpr uint32_t with_home = moonlight::home | moonlight::a | moonlight::back | moonlight::start;
    static_assert(allowed_buttons(with_home, PadRole::host, false) == with_home);
    static_assert(allowed_buttons(with_home, PadRole::guest, false) == (moonlight::a | moonlight::start));
    static_assert(allowed_buttons(with_home, PadRole::guest, true) == with_home);
    static_assert(allowed_buttons(0, PadRole::guest, false) == 0);
    // A guest's pad opens on its first use: a button, a trigger or a stick pushed far enough, not drift (issue 26).
    static_assert(!wakes_pad(0, 0, 0, 0, 0, 0, 0));
    static_assert(!wakes_pad(0, trigger_threshold - 1, 0, 3000, -3000, stick_wake - 1, -(stick_wake - 1)));
    static_assert(wakes_pad(moonlight::a, 0, 0, 0, 0, 0, 0));
    static_assert(wakes_pad(0, 0, trigger_threshold, 0, 0, 0, 0));
    static_assert(wakes_pad(0, 0, 0, 0, -stick_wake, 0, 0) && wakes_pad(0, 0, 0, 0, 0, 0, -32768));
    std::cout << "PASS: gamepad mapping (face, system, Share as touchpad, Select and Start as touchpad without one, sticks, triggers, trigger effects, PS and Create for the host only, "
                 "a guest's pad opens on its first use)\n";
}
