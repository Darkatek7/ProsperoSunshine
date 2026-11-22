// SPDX-License-Identifier: GPL-3.0-only
// What a paired device sends while it streams: touch points, motion, the buttons and sticks of a controller, keys, the mouse
// and its wheel (src/input/*.hpp), as sequences of events the way the control channel delivers them. These are the state
// machines behind the DualSense and the virtual keyboard and mouse; no order of events may leave them outside their ranges.
#include "fuzz_common.hpp"
#include "input/gamepad_map.hpp"
#include "input/keyboard_map.hpp"
#include "input/keyboard_pad.hpp"
#include "input/pad_settings.hpp"
#include "input/pad_touch.hpp"
#include "input/profile_rules.hpp"
#include "input/title_profile.hpp"

#include <chrono>
#include <set>

using namespace prospero;
using clock_type = std::chrono::steady_clock;

namespace {

// Finite and not, small and large: what a client could put in a float field.
float some_float(FuzzedDataProvider &in) {
    switch (in.ConsumeIntegralInRange<int>(0, 3)) {
    case 0: return in.ConsumeFloatingPointInRange<float>(-0.5f, 1.5f);
    case 1: return in.ConsumeFloatingPointInRange<float>(-4000.f, 4000.f);
    default: return in.ConsumeFloatingPoint<float>();
    }
}

void check_pad(const PadInput &pad) {
    for (const int16_t axis : {pad.lx, pad.ly, pad.rx, pad.ry}) FUZZ_CHECK(axis >= -32768 && axis <= 32767);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider in(data, size);

    // The profile file comes first, so that the pad can play with a profile of the player's own.
    const ProfileList list = parse_profile_list(in.ConsumeRandomLengthString(200));

    // Controller state: any buttons, triggers and sticks map into the DualSense's ranges.
    {
        const uint32_t flags = in.ConsumeIntegral<uint32_t>();
        const auto lt = in.ConsumeIntegral<uint8_t>(), rt = in.ConsumeIntegral<uint8_t>();
        const auto lx = in.ConsumeIntegral<int16_t>(), ly = in.ConsumeIntegral<int16_t>(), rx = in.ConsumeIntegral<int16_t>(), ry = in.ConsumeIntegral<int16_t>();
        const PadState state = map_gamepad(flags, lt, rt, lx, ly, rx, ry);
        FUZZ_CHECK(state.l2 == lt && state.r2 == rt);
        FUZZ_CHECK((state.buttons & dualsense::l2) == ((lt >= trigger_threshold) ? dualsense::l2 : 0u));
        FUZZ_CHECK(stick_axis(lx, false) + stick_axis(lx, true) == 255);
        // A guest's PS and Create never pass unless allowed, whatever else is set.
        for (const MenuAccess access : {MenuAccess::by_role, MenuAccess::allowed, MenuAccess::denied}) {
            const uint32_t passed = allowed_buttons(flags, in.ConsumeBool() ? PadRole::host : PadRole::guest, in.ConsumeBool(), access);
            if (access == MenuAccess::denied) FUZZ_CHECK((passed & (moonlight::home | moonlight::back)) == 0);
            FUZZ_CHECK((passed & ~flags) == 0);
        }
        (void)wakes_pad(flags, lt, rt, lx, ly, rx, ry);
        (void)holds_switch(flags, lt, rt);
        (void)has_touchpad(in.ConsumeIntegral<uint16_t>(), flags);

        TouchpadCombo combo;
        int64_t now = 0;
        for (int i = 0; i < 20 && in.remaining_bytes() > 0; ++i) {
            now += in.ConsumeIntegralInRange<int64_t>(0, 400);
            const uint32_t out = combo.apply(in.ConsumeIntegral<uint32_t>(), now, in.ConsumeIntegralInRange<int64_t>(-1, 5000));
            FUZZ_CHECK(!combo.active || (out & (moonlight::back | moonlight::start)) == 0);
            (void)combo.busy(now);
        }
        SwitchBudget budget;
        auto at = clock_type::now();
        for (int i = 0; i < 20 && in.remaining_bytes() > 0; ++i) {
            at += std::chrono::seconds(in.ConsumeIntegralInRange<int>(0, 30));
            if (in.ConsumeBool()) budget.switched(at, in.ConsumeIntegralInRange<size_t>(0, 4));
            else budget.refused(at);
            (void)budget.allowed(at);
            (void)budget.tell_once();
            FUZZ_CHECK(budget.seconds_left(at) >= 0 && budget.seconds_left(at) <= switch_cooldown.count());
        }
        TriggerBlock a{}, b{};
        for (auto &byte : a) byte = in.ConsumeIntegral<uint8_t>();
        for (auto &byte : b) byte = in.ConsumeIntegral<uint8_t>();
        (void)map_trigger_effects(a, b);
        // trigger_knock_strength on whatever block a game sends: only the zones of effect 0x26 change, a zone never gets
        // stronger, and the mask only loses zones.
        if (in.ConsumeBool()) a[1] = trigger_effect_knock;
        const int knock = in.ConsumeIntegralInRange<int>(-10, 120);
        const auto soft = map_trigger_effects(a, b, knock);
        const auto full = map_trigger_effects(a, b);
        FUZZ_CHECK(soft.type_left == full.type_left && soft.type_right == full.type_right && soft.flags == full.flags);
        if (soft.type_left != trigger_effect_knock || knock >= 100) {
            FUZZ_CHECK(soft.left == full.left);
        } else {
            const auto zones = [](const std::array<uint8_t, 10> &d) {
                return d[2] | uint32_t(d[3]) << 8 | uint32_t(d[4]) << 16 | uint32_t(d[5]) << 24;
            };
            const uint32_t mask_soft = soft.left[0] | uint32_t(soft.left[1]) << 8, mask_full = full.left[0] | uint32_t(full.left[1]) << 8;
            FUZZ_CHECK((mask_soft & ~mask_full) == 0 && (mask_soft >> 10) == (mask_full >> 10));
            FUZZ_CHECK((zones(soft.left) & 0xc0000000u) == (zones(full.left) & 0xc0000000u));
            for (int zone = 0; zone < 10; ++zone) FUZZ_CHECK((zones(soft.left) >> (3 * zone) & 7u) <= (zones(full.left) >> (3 * zone) & 7u));
            for (int i = 6; i < 10; ++i) FUZZ_CHECK(soft.left[i] == full.left[i]);
            if (knock <= 0) FUZZ_CHECK((mask_soft & 0x3ffu) == 0);
        }
        // The pads' percentages in sunshine.conf: a value in range, or 100 for anything else.
        const std::string text = in.ConsumeRandomLengthString(16);
        const int max = in.ConsumeBool() ? knock_strength_max : haptic_strength_max;
        const Percent parsed = parse_percent(text, max);
        FUZZ_CHECK(parsed.value >= 0 && parsed.value <= max && (!parsed.fell_back || parsed.value == 100));
    }

    // Fingers: at most two, ids of seven bits, positions on the pad.
    {
        PadTouch touch;
        for (int i = 0; i < 300 && in.remaining_bytes() > 0; ++i) {
            const auto event = static_cast<uint8_t>(in.ConsumeIntegralInRange<int>(0, 8));
            (void)touch.update(event, in.ConsumeIntegralInRange<uint32_t>(0, 4), some_float(in), some_float(in));
            FUZZ_CHECK(touch.count() <= 2);
            for (uint8_t k = 0; k < touch.count(); ++k) {
                const TouchPoint &p = touch.points()[k];
                FUZZ_CHECK(p.id < 128 && p.x <= touch_max_x && p.y <= touch_max_y);
            }
            if (touch.count() == 2) FUZZ_CHECK(touch.points()[0].id != touch.points()[1].id);
        }
        (void)accel_to_g(some_float(in));
        (void)gyro_to_rad(some_float(in));
    }

    // The virtual keyboard: no more than 16 keys and each once; modifiers only the bits that exist.
    {
        KeyboardState keyboard;
        for (int i = 0; i < 300 && in.remaining_bytes() > 0; ++i) {
            const auto vk = static_cast<uint16_t>(in.ConsumeBool() ? in.ConsumeIntegral<uint16_t>() : in.ConsumeIntegralInRange<int>(0x08, 0xDE));
            (void)keyboard.update(vk, in.ConsumeBool());
            FUZZ_CHECK(keyboard.count() <= KeyboardState::max_keys);
            std::set<uint8_t> unique(keyboard.usages(), keyboard.usages() + keyboard.count());
            FUZZ_CHECK(unique.size() == keyboard.count());
            for (uint8_t k = 0; k < keyboard.count(); ++k) FUZZ_CHECK(keyboard.usages()[k] != 0);
        }
        keyboard.clear();
        FUZZ_CHECK(keyboard.empty());
        WheelSteps wheel;
        for (int i = 0; i < 200 && in.remaining_bytes() > 0; ++i) {
            const int distance = in.ConsumeIntegral<int16_t>();  // a wheel report is 16 bits
            const int8_t steps = wheel.add(distance);
            FUZZ_CHECK(steps >= -127 && steps <= 127);
        }
        for (char c = 0; c < 127; ++c) (void)ascii_key(c);
        for (int button = -1; button < 8; ++button) (void)mouse_button_bit(button);
    }

    // Keys, mouse and wheel as a controller, under any profile, the player's own included.
    {
        KeyboardPad pad;
        auto now = clock_type::now();
        auto choose = [&](uint8_t pick) {
            if (!list.own.empty() && pick % 3 == 0) {
                auto it = list.own.begin();
                std::advance(it, pick % list.own.size());
                pad.set_profile(it->second->base, it->second);
            } else {
                pad.set_profile(static_cast<Profile>(pick % profile_names.size()));
            }
        };
        choose(0);
        for (int i = 0; i < 400 && in.remaining_bytes() > 0; ++i) {
            now += std::chrono::microseconds(in.ConsumeIntegralInRange<int>(0, 60000));
            switch (in.ConsumeIntegralInRange<int>(0, 13)) {
            case 0: case 1: case 2:
                (void)pad.key(static_cast<uint16_t>(in.ConsumeBool() ? in.ConsumeIntegral<uint16_t>() : in.ConsumeIntegralInRange<int>(0x08, 0xDE)),
                              in.ConsumeBool(), now, in.ConsumeBool());
                break;
            case 3: (void)pad.mouse_button(in.ConsumeIntegralInRange<int>(-1, 7), in.ConsumeBool()); break;
            case 4: pad.mouse_move(in.ConsumeIntegral<int16_t>(), in.ConsumeIntegral<int16_t>(), now); break;
            case 5: pad.mouse_position(some_float(in), some_float(in), now); break;
            case 6: (void)pad.scroll(in.ConsumeIntegral<int16_t>(), now); break;
            case 7: pad.hscroll(in.ConsumeIntegral<int16_t>(), now); break;
            case 8: pad.set_sensitivity(in.ConsumeIntegral<int>()); FUZZ_CHECK(pad.sensitivity() > 0); break;
            case 9: choose(in.ConsumeIntegral<uint8_t>()); break;
            case 10: pad.set_outside_game(in.ConsumeBool()); break;
            case 11: pad.clear(); break;
            case 12: pad.drop_movement(); break;
            default: break;
            }
            check_pad(pad.state(now));
            check_pad(pad.state(now, false));
            (void)pad.pressed(now);
            if (const auto next = pad.next_change(now)) FUZZ_CHECK(*next >= now);
        }
        // Long after the last event the mouse and the wheel are still.
        now += std::chrono::seconds(10);
        check_pad(pad.state(now));
        (void)pad.next_change(now);
    }

    // The sign-out decisions and the routes.
    {
        SignOutFacts facts;
        facts.host_signed_in = in.ConsumeBool();
        facts.seated = in.ConsumeBool();
        facts.in_front = static_cast<Answer>(in.ConsumeIntegralInRange<int>(0, 2));
        facts.owns_unseated_pad = static_cast<Answer>(in.ConsumeIntegralInRange<int>(0, 2));
        const SignOut out = sign_out(facts);
        if (!facts.host_signed_in || facts.seated) FUZZ_CHECK(out == SignOut::never);
        if (out == SignOut::now) FUZZ_CHECK(facts.in_front == Answer::no && facts.owns_unseated_pad == Answer::no);
        (void)join_block({in.ConsumeBool(), static_cast<Answer>(in.ConsumeIntegralInRange<int>(0, 2)), in.ConsumeBool()});
        ProfileSet set;
        for (int i = 0; i < 6; ++i) set.users.push_back(in.ConsumeIntegral<uint32_t>());
        const uint32_t last = in.ConsumeBool() ? set.users[2] : in.ConsumeIntegral<uint32_t>();
        const auto order = join_order(set, last);
        FUZZ_CHECK(order.size() <= set.users.size());
        HostProfiles host;
        for (int i = 0; i < 30 && in.remaining_bytes() > 0; ++i) {
            const uint32_t user = set.users[in.ConsumeIntegralInRange<size_t>(0, 5)];
            switch (in.ConsumeIntegralInRange<int>(0, 3)) {
            case 0: host.add(user); FUZZ_CHECK(host.signed_in_by_host(user)); break;
            case 1: host.keep(user); break;
            case 2: host.seat(user); break;
            default: host.forget(user); FUZZ_CHECK(!host.knows(user)); break;
            }
            for (const uint32_t u : host.kept) FUZZ_CHECK(!host.signed_in_by_host(u));
        }
        (void)kbm_route(in.ConsumeBool(), in.ConsumeBool(), in.ConsumeBool(), in.ConsumeBool());
    }
    return 0;
}
