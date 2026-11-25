// SPDX-License-Identifier: GPL-3.0-only
// Sunshine's keyboard and mouse on the PS5: the shared ones and a device's own, seated in its profile, added on first
// use and closed again; Windows+Esc as a controller's PS; text from a client's on-screen keyboard.
#include "sunshine/input_ps5_internal.hpp"
#include "control/device_profiles.hpp"
#include "control/menu_access.hpp"
#include "input/keyboard_map.hpp"
#include "input/profile_rules.hpp"
#include "platform/ps5/kbm.h"
#include "platform/ps5/profile_seat.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

using namespace std::literals;

namespace platf {

// Removes a keyboard and mouse (they release what they hold) and forgets their state. Caller holds input.mutex.
static void close_kbm(kbm_t &kbm) {
    const char *whose = kbm.owner.empty() ? "" : " of a device's profile";
    if (kbm.keyboard) {
        const int rc = ps_keyboard_close(kbm.keyboard);
        removed_device();
        BOOST_LOG(info) << "PS5 virtual keyboard"sv << whose << " detached ("sv << rc << ')';
    }
    if (kbm.mouse) {
        const int rc = ps_mouse_close(kbm.mouse);
        removed_device();
        BOOST_LOG(info) << "PS5 virtual mouse"sv << whose << " detached ("sv << rc << ')';
    }
    kbm.keyboard = nullptr;
    kbm.mouse = nullptr;
    kbm.keyboard_bus = kbm.mouse_bus = 0;
    kbm.keyboard_failed = kbm.mouse_failed = false;
    kbm.keys.clear();
    kbm.mouse_buttons = 0;
    kbm.wheel.clear();
    kbm.tilt.clear();
    kbm.pointer.reset();
}

// A device's own keyboard and mouse go once its last controller leaves. Caller holds input.mutex.
void close_own_kbm(input_raw_t &input, const std::string &owner) {
    for (auto it = input.own.begin(); it != input.own.end();) {
        if ((*it)->owner == owner) {
            close_kbm(**it);
            it = input.own.erase(it);
        } else {
            ++it;
        }
    }
}

void close_keyboard_mouse(input_raw_t &input) {
    close_kbm(input.shared);
    for (auto &kbm : input.own) close_kbm(*kbm);
    input.own.clear();
}

// Each device is added on first use; one that fails is not tried again until the sessions end. The system drops what
// a device reports right after it was added (a key at 0 ms was lost, at 50 ms taken, kbm_probe on 13.00), so the
// first report waits a little, once per session.
constexpr auto device_settle = 150ms;

// A key or button let go within a few ms of going down is not seen: the first key after the settle wait above went
// down and up 1 ms apart and was lost. So a release waits until its press has been in effect this long.
constexpr auto min_hold = 30ms;

static void hold_from(std::chrono::steady_clock::time_point down) { std::this_thread::sleep_until(down + min_hold); }

namespace {
// Whose input this thread passes on (set_input_owner, patch 0022): the session's feedback queue, or null.
thread_local const feedback_queue_t *input_owner = nullptr;
}  // namespace

void set_input_owner(const feedback_queue_t *feedback_queue) { input_owner = feedback_queue; }

// Which keyboard and mouse on the console the input this thread passes on gets: the one rule, asked only by the seven
// ways in below (they get null and drop the input).
// - A device that can choose any profile (or not set yet): the shared ones, as the console's own, as before profiles.
// - A device with its own profiles: its own, seated in the profile its first controller plays as. The console doesn't ask
//   "Who's using this keyboard?" (or mouse), so nobody picks another profile with them, and the player types into text
//   fields as that profile. None while it has no controller seated: a spectator, or one whose keyboard and mouse play as a
//   controller of their own (keyboard_pad, which takes them before they get here).
// - Input without a stream behind it (Sunshine releasing what a stream held; its nudge of the pointer at a stream's start)
//   may only release, on the shared ones: a device's own ones release everything when they go. Sunshine's key repeat and
//   its delayed left button run on its task pool but carry their stream (patch 0022).
// - The console's menus (control/menu_access.hpp), for a device's own ones whose profile doesn't reach them: a key that
//   opens them (`vk`, opens_console_menu: Print Screen the Create menu, Pause the control center) gets none, and while no
//   game is in front nothing does. They go then, which lets go of everything, and come back seated with the next use once
//   a game is in front; a press meanwhile tells the player why, as for a pad (hold_back).
// prospero::kbm_route and prospero::kbm_menus decide. Its profiles and the menus are read at most once a second. Caller
// holds input.mutex.
static kbm_t *kbm_for_input(input_raw_t &input, bool release, uint16_t vk = 0) {
    const std::string cert = input_owner && *input_owner ? gamepad_owner(*input_owner) : std::string();
    thread_local std::string cached_cert;
    thread_local std::optional<prospero::ProfileSet> cached_set;
    thread_local std::optional<prospero::MenuProfiles> cached_menus;
    thread_local std::chrono::steady_clock::time_point cached_at{};
    const auto now = std::chrono::steady_clock::now();
    if (!cert.empty() && (cert != cached_cert || now - cached_at > 1s)) {
        cached_set = profiles_of(paired_device(cert).uuid);
        cached_menus = menu_profiles();
        cached_cert = cert;
        cached_at = now;
    }
    const bool limited = !cert.empty() && cached_set && !cached_set->free;
    uint32_t user = 0;
    int first = -1;  // the device's first pad
    for (int nr = 0; limited && nr < MAX_GAMEPADS && !user; ++nr)
        if (input.pads[nr] && input.owner[nr] == cert) {
            user = input.seat[nr];
            first = nr;
        }
    const auto route = prospero::kbm_route(!cert.empty(), release, limited, user != 0);
    if (route == prospero::KbmRoute::shared) return &input.shared;
    if (!limited) return nullptr;
    const auto menus = route == prospero::KbmRoute::own
                           ? prospero::kbm_menus(prospero::menu_access(cached_menus, cached_set, user), input.game_in_front,
                                                 release, prospero::opens_console_menu(prospero::vk_usage(vk)))
                           : prospero::KbmMenus::pass;
    const bool waits = menus == prospero::KbmMenus::wait;
    auto it = std::find_if(input.own.begin(), input.own.end(), [&](const auto &kbm) { return kbm->owner == cert; });
    // Its player switched profile, or it lost the one they were in, or it waits for a game: the next use adds them anew
    // where it plays then.
    if (it != input.own.end() && ((*it)->user != user || waits)) {
        if (waits) BOOST_LOG(info) << "PS5 input: no game in front, and the profile of a device's own keyboard and mouse doesn't reach the console's menus: they wait"sv;
        close_kbm(**it);
        input.own.erase(it);
        it = input.own.end();
    }
    if (route == prospero::KbmRoute::none) return nullptr;
    if (waits) {
        if (!release && first >= 0 && now - input.held_told[first] >= prospero::switch_cooldown) {
            input.held_told[first] = now;
            if (launched_open(input, user, false) != prospero::Answer::yes)
                ps_notify((profile_name(user) + " can't use the console's menus. Start a game from Moonlight").c_str());
        }
        return nullptr;
    }
    if (menus == prospero::KbmMenus::drop) {
        BOOST_LOG(info) << "PS5 input: Print Screen or Pause ignored, the keyboard's profile doesn't reach the console's menus"sv;
        return nullptr;
    }
    if (it == input.own.end()) {
        input.own.push_back(std::make_unique<kbm_t>());
        input.own.back()->owner = cert;
        input.own.back()->user = user;
        it = std::prev(input.own.end());
    }
    return it->get();
}

// Seats a keyboard or mouse of a device's own in its profile right after adding it, as open_pad does a pad. The bus
// knows them by the add handle with the device type above it, type << 48: 3 for a keyboard (0x3000000660100 in the login
// manager's log), 4 for a mouse (0x4000000670000). The bare handle is the fallback. False leaves it unseated: the
// caller removes it. Returns the bus id it was seated by, 0 if none.
static uint64_t seat_kbm_device(uint32_t handle, uint32_t user, const char *what, uint64_t type) {
    const uint64_t forms[] = {handle | (type << 48), handle};
    int rc = 0;
    for (const uint64_t device : forms) {
        rc = ps_profile_bind(device, user);
        if (rc == 0) {
            char text[64];
            std::snprintf(text, sizeof text, "handle %#x, bound as %#llx", handle, static_cast<unsigned long long>(device));
            BOOST_LOG(info) << "PS5 virtual "sv << what << " of a device's profile seated ("sv << text << ')';
            return device;
        }
    }
    BOOST_LOG(warning) << "PS5 virtual "sv << what << " of a device's profile not seated ("sv << rc << "), so it was removed"sv;
    return 0;
}

// The keyboard and the mouse of a kbm_t, added on first use. A release gets the one there is, never a new one.
static ps_keyboard *keyboard_of(input_raw_t &input, kbm_t &kbm, bool release) {
    if (release) return kbm.keyboard;
    if (!kbm.keyboard && !kbm.keyboard_failed) {
        if (kbm.user) settle_bus();
        if (const int rc = ps_keyboard_open(kbm.user ? kbm.user : input.user, &kbm.keyboard); rc) {
            BOOST_LOG(error) << "PS5 virtual keyboard failed: "sv << rc;
            kbm.keyboard_failed = true;
        } else if (kbm.user && !(kbm.keyboard_bus = seat_kbm_device(ps_keyboard_device_id(kbm.keyboard), kbm.user, "keyboard", 3))) {
            ps_keyboard_close(kbm.keyboard);
            removed_device();
            kbm.keyboard = nullptr;
            kbm.keyboard_failed = true;
        } else {
            BOOST_LOG(info) << "PS5 virtual keyboard attached"sv << (kbm.user ? " in its device's profile"sv : ""sv);
            std::this_thread::sleep_for(device_settle);
        }
    }
    return kbm.keyboard;
}

static ps_mouse *mouse_of(input_raw_t &input, kbm_t &kbm, bool release) {
    if (release) return kbm.mouse;
    if (!kbm.mouse && !kbm.mouse_failed) {
        if (kbm.user) settle_bus();
        if (const int rc = ps_mouse_open(kbm.user ? kbm.user : input.user, &kbm.mouse); rc) {
            BOOST_LOG(error) << "PS5 virtual mouse failed: "sv << rc;
            kbm.mouse_failed = true;
        } else if (kbm.user && !(kbm.mouse_bus = seat_kbm_device(ps_mouse_device_id(kbm.mouse), kbm.user, "mouse", 4))) {
            ps_mouse_close(kbm.mouse);
            removed_device();
            kbm.mouse = nullptr;
            kbm.mouse_failed = true;
        } else {
            BOOST_LOG(info) << "PS5 virtual mouse attached"sv << (kbm.user ? " in its device's profile"sv : ""sv);
            std::this_thread::sleep_for(device_settle);
        }
    }
    return kbm.mouse;
}

static void send_mouse(kbm_t &kbm, ps_mouse *mouse, int dx, int dy, int8_t wheel = 0, int8_t tilt = 0) {
    // A large jump goes out in several reports rather than wrapping around.
    do {
        const auto x = static_cast<int16_t>(std::clamp(dx, -32767, 32767));
        const auto y = static_cast<int16_t>(std::clamp(dy, -32767, 32767));
        ps_mouse_send(mouse, x, y, kbm.mouse_buttons, wheel, tilt);
        dx -= x;
        dy -= y;
        wheel = tilt = 0;
    } while (dx || dy);
}

void move_mouse(input_t &input, int deltaX, int deltaY) {
    std::lock_guard lock(input->mutex);
    kbm_t *kbm = kbm_for_input(*input, false);
    if (!kbm) return;
    // Sunshine nudges the pointer by a pixel and back at every stream start; that alone does not add a mouse.
    if (!kbm->mouse && std::abs(deltaX) <= 1 && std::abs(deltaY) <= 1) return;
    ps_mouse *mouse = mouse_of(*input, *kbm, false);
    if (!mouse) return;
    kbm->pointer.reset();
    send_mouse(*kbm, mouse, deltaX, deltaY);
}

void move_mouse(input_t &input, const touch_port_t &, int deltaX, int deltaY) { move_mouse(input, deltaX, deltaY); }

// The console's pointer is relative only, so a client in absolute mode moves it by the difference to its last position.
void abs_mouse(input_t &input, const touch_port_t &, float x, float y) {
    std::lock_guard lock(input->mutex);
    kbm_t *kbm = kbm_for_input(*input, false);
    if (!kbm) return;
    ps_mouse *mouse = mouse_of(*input, *kbm, false);
    if (!mouse) return;
    const auto last = std::exchange(kbm->pointer, std::pair{x, y});
    if (!last) return;
    send_mouse(*kbm, mouse, static_cast<int>(std::lround(x - last->first)), static_cast<int>(std::lround(y - last->second)));
}

void button_mouse(input_t &input, int button, bool release) {
    const uint8_t bit = prospero::mouse_button_bit(button);
    if (!bit) return;
    std::lock_guard lock(input->mutex);
    kbm_t *kbm = kbm_for_input(*input, release);
    if (!kbm) return;
    const uint8_t buttons = release ? kbm->mouse_buttons & ~bit : kbm->mouse_buttons | bit;
    if (buttons == kbm->mouse_buttons) return;
    ps_mouse *mouse = mouse_of(*input, *kbm, release);
    if (!mouse) return;
    kbm->mouse_buttons = buttons;
    if (release) hold_from(kbm->button_down_at);
    send_mouse(*kbm, mouse, 0, 0);
    if (!release) kbm->button_down_at = std::chrono::steady_clock::now();
}

void scroll(input_t &input, int distance) {
    std::lock_guard lock(input->mutex);
    kbm_t *kbm = kbm_for_input(*input, false);
    if (!kbm) return;
    ps_mouse *mouse = mouse_of(*input, *kbm, false);
    if (!mouse) return;
    if (const int8_t steps = kbm->wheel.add(distance)) send_mouse(*kbm, mouse, 0, 0, steps, 0);
}

void hscroll(input_t &input, int distance) {
    std::lock_guard lock(input->mutex);
    kbm_t *kbm = kbm_for_input(*input, false);
    if (!kbm) return;
    ps_mouse *mouse = mouse_of(*input, *kbm, false);
    if (!mouse) return;
    if (const int8_t steps = kbm->tilt.add(distance)) send_mouse(*kbm, mouse, 0, 0, 0, steps);
}

// Lets go of a keyboard's PS once its Esc came up and, for Windows+Shift+Esc, ps_long_hold is over. Every 10 ms from
// poll_pad_output, and at once when Esc comes up. Caller holds input.mutex.
void release_keyboard_ps(input_raw_t &input) {
    const auto now = std::chrono::steady_clock::now();
    for (int nr = 0; nr < MAX_GAMEPADS; ++nr) {
        if (!input.keyboard_ps[nr] || !input.keyboard_ps_up[nr] || now < input.keyboard_ps_until[nr]) continue;
        input.keyboard_ps[nr] = input.keyboard_ps_up[nr] = false;
        input.keyboard_ps_until[nr] = {};
        if (input.pads[nr]) apply_gamepad(&input, nr, input.last_state[nr]);
    }
}

// Windows+Esc on the keys of a device that has a controller (the on-screen one of Moonlight for iPhone and iPad, say):
// PS on its first controller, held while Esc is (Windows+Shift+Esc: ps_long_hold at least), with that controller's rules
// for PS (its role, the console's menus), and Esc doesn't reach the keyboard. Without a controller of the device Esc goes
// on as Esc. Returns whether the key was the chord's. Caller holds input.mutex.
static bool keyboard_ps(input_t &input, uint16_t vk, bool release) {
    const std::string cert = input_owner && *input_owner ? gamepad_owner(*input_owner) : std::string();
    if (cert.empty()) return false;
    auto &[chord, nr] = input->ps_chords.try_emplace(cert, prospero::PsChord{}, -1).first->second;
    const auto action = chord.key(vk, release);
    switch (action) {
        case prospero::PsChord::Action::pass:
            return false;
        case prospero::PsChord::Action::swallow:
            return true;
        case prospero::PsChord::Action::press:
        case prospero::PsChord::Action::press_long: {
            const bool held = action == prospero::PsChord::Action::press_long;
            for (int m = 0; m < MAX_GAMEPADS && nr < 0; ++m)
                if (input->pads[m] && input->owner[m] == cert) nr = m;
            if (nr < 0) {
                chord.cancel();
                return false;
            }
            BOOST_LOG(info) << "PS5 virtual pad "sv << nr << ": Windows+"sv << (held ? "Shift+"sv : ""sv)
                            << "Esc on its device's keyboard, PS"sv << (held ? " held"sv : ""sv);
            input->keyboard_ps[nr] = true;
            input->keyboard_ps_up[nr] = false;
            input->keyboard_ps_until[nr] = held ? std::chrono::steady_clock::now() + prospero::ps_long_hold
                                                : std::chrono::steady_clock::time_point{};
            apply_gamepad(&*input, nr, input->last_state[nr]);
            return true;
        }
        case prospero::PsChord::Action::release:
            // The pad went meanwhile (free_gamepad let go of its PS), or another took its slot: nothing to let go. A held
            // PS whose time isn't over yet goes with release_keyboard_ps.
            if (nr >= 0 && input->keyboard_ps[nr] && input->owner[nr] == cert) {
                input->keyboard_ps_up[nr] = true;
                release_keyboard_ps(*input);
            }
            nr = -1;
            return true;
    }
    return false;
}

void keyboard_update(input_t &input, uint16_t modcode, bool release, uint8_t, bool) {
    std::lock_guard lock(input->mutex);
    if (keyboard_ps(input, modcode, release)) return;
    kbm_t *kbm = kbm_for_input(*input, release, modcode);
    if (!kbm) return;
    // Only a key that changes what is held asks for the keyboard (an unmapped key or a repeat adds none).
    auto keys = kbm->keys;
    if (!keys.update(modcode, release)) return;
    ps_keyboard *keyboard = keyboard_of(*input, *kbm, release);
    if (!keyboard) return;
    kbm->keys = keys;
    if (release) hold_from(kbm->key_down_at);
    ps_keyboard_send(keyboard, kbm->keys.modifiers(), kbm->keys.usages(), kbm->keys.count());
    if (!release) kbm->key_down_at = std::chrono::steady_clock::now();
}

// Text from a client's on-screen keyboard: printable ASCII is typed key by key as on a US keyboard, and what the
// client holds stays held. Other characters have no key and are dropped. Each key takes two min_hold, so a pasted text
// runs for seconds: the lock is taken for each report and let go while the key is held, or every device's input and
// the pads' feedback would wait for the text. The keyboard is looked up anew each time, as it may go meanwhile (its
// device's last controller left, its profile changed); the rest of the text is dropped then.
void unicode(input_t &input, const char *utf8, int size) {
    int dropped = 0, typed = 0;
    bool gone = false;
    for (int i = 0; i < size && !gone; i++) {
        const auto key = prospero::ascii_key(utf8[i]);
        if (!key.usage) {
            dropped += (static_cast<uint8_t>(utf8[i]) & 0xC0) != 0x80;  // a UTF-8 sequence counts once
            continue;
        }
        for (const bool down : {true, false}) {
            {
                std::lock_guard lock(input->mutex);
                // Letting go never adds a keyboard: one that went let go of the key itself.
                kbm_t *kbm = kbm_for_input(*input, !down);
                ps_keyboard *keyboard = kbm ? keyboard_of(*input, *kbm, !down) : nullptr;
                if (!keyboard) {
                    gone = true;
                    break;
                }
                const auto &held = kbm->keys;
                if (down) {
                    std::array<uint8_t, prospero::KeyboardState::max_keys> usages{};
                    const auto count = std::min<uint8_t>(held.count(), prospero::KeyboardState::max_keys - 1);
                    std::copy_n(held.usages(), count, usages.begin());
                    usages[count] = key.usage;
                    const uint8_t modifiers = held.modifiers() | (key.shift ? 0x02 : 0);
                    ps_keyboard_send(keyboard, modifiers, usages.data(), count + 1);
                    ++typed;
                } else {
                    ps_keyboard_send(keyboard, held.modifiers(), held.usages(), held.count());
                }
            }
            std::this_thread::sleep_for(min_hold);
        }
    }
    if (gone && typed) BOOST_LOG(info) << "PS5 virtual keyboard: it went while a text was typed, the rest is dropped"sv;
    if (dropped) BOOST_LOG(debug) << "PS5 virtual keyboard: "sv << dropped << " characters without a key"sv;
}
}
