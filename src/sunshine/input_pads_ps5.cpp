// SPDX-License-Identifier: GPL-3.0-only
// Sunshine's gamepads on the PS5, one native virtual DualSense each: coming (opened in the profile the seats give
// them), going, and what their clients send (buttons, sticks, touchpad, motion), with the console's menus deciding
// PS, Create and whether a pad's input waits.
#include "sunshine/input_ps5_internal.hpp"
#include "control/client_app.hpp"
#include "control/menu_access.hpp"
#include "input/gamepad_map.hpp"
#include "input/pad_roles.hpp"
#include "input/pad_touch.hpp"
#include "input/profile_rules.hpp"
#include "platform/ps5/pad.h"
#include "platform/ps5/profile_seat.h"
#include "sunshine/platform_ps5.hpp"
#include "src/config.h"
#include "src/logging.h"
#include "src/nvhttp.h"
#include "src/platform/common.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std::literals;

namespace platf {

// A client only sends the sensors it is asked for, and a new client has to be asked anew. One without sensors ignores this.
void request_motion(const input_raw_t &input, int nr) {
    if (!input.feedback[nr]) return;
    for (const uint8_t type : {prospero::moonlight::motion_accel, prospero::moonlight::motion_gyro}) {
        input.feedback[nr]->raise(gamepad_feedback_msg_t::make_motion_event_state(input.client_index[nr], type, 100));
    }
}

static prospero::MenuAccess pad_menu_access(input_raw_t &input, int nr);

// Moonlight for iPhone and iPad has no PS button: neither its on-screen controller nor the bar above its on-screen keyboard
// has one, nor F1, but the bar has Windows and Esc (keyboard_map.hpp, ps_chord). Once a controller of such a device joins
// that may press PS, the console says how, once per device while streams go on, whatever buttons the controller reports:
// with a Bluetooth controller connected to an iPhone, the one tried gave PS only by a held Select, which can't hold PS.
// Caller holds input.mutex.
static void hint_ps_keys(input_raw_t &input, int nr) {
    const std::string &cert = input.owner[nr];
    if (cert.empty() || input.ps_hint_told.contains(cert)) return;
    if (!prospero::touch_apple_client(nvhttp::get_client_user_agent(cert))) return;
    const auto role = input.roles.is_host(nr) ? prospero::PadRole::host : prospero::PadRole::guest;
    if (!prospero::has_menu_buttons(role, input.ps_all_players, pad_menu_access(input, nr))) return;
    input.ps_hint_told.insert(cert);
    BOOST_LOG(info) << "PS5 virtual pad "sv << nr << ": its device is Moonlight for iPhone or iPad, the console says how to press PS"sv;
    // Win and Shift of that bar stay down until tapped again.
    ps_notify("No PS button? Three fingers for the keyboard. PS: Win, Esc, Win. Hold PS: Win, Shift, Esc, Shift, Win");
}

// add_pad, then the hint for a device without a PS button.
static pad_open open_pad(input_raw_t &input, int nr, bool host) {
    const pad_open opened = add_pad(input, nr, host);
    if (opened == pad_open::opened) hint_ps_keys(input, nr);
    return opened;
}


int alloc_gamepad(input_t &input, const gamepad_id_t &id, const gamepad_arrival_t &arrival, feedback_queue_t feedback) {
    if (id.globalIndex < 0 || id.globalIndex >= MAX_GAMEPADS) return -1;
    const std::string owner = gamepad_owner(feedback);
    std::lock_guard lock(input->mutex);
    input->feedback[id.globalIndex] = std::move(feedback);
    input->client_index[id.globalIndex] = id.clientRelativeIndex;
    input->touch_combo[id.globalIndex] = !prospero::has_touchpad(arrival.capabilities, arrival.supportedButtons);
    input->combo[id.globalIndex] = {};
    if (input->touch_combo[id.globalIndex])
        BOOST_LOG(info) << "PS5 virtual pad "sv << id.globalIndex << ": no touchpad, Select and Start together click it"sv;
    auto *&pad = input->pads[id.globalIndex];
    if (!pad && !input->waiting[id.globalIndex]) {
        input->owner[id.globalIndex] = owner;
        input->watch_told[id.globalIndex] = {};  // a new controller here hears why it doesn't join, whoever was told before
        // The host's pad plays as the console's user. Every other one, from another device or a second controller on the
        // host's, is a guest: the system asks who uses it, and they pick a user or a guest (pad_roles.hpp). The role is
        // the device's from the start; a guest's pad only opens on its first use, as the question can only be answered
        // from that controller and a client may announce one that never sends anything (gamepad_map.hpp, wakes_pad).
        const bool host = input->roles.attach(id.globalIndex, owner);
        // No device ids in the log: it goes into the report people post.
        if (!host) {
            input->waiting[id.globalIndex] = true;
            BOOST_LOG(info) << "PS5 virtual pad "sv << id.globalIndex << " announced, opens when it is used"sv
                            << (owner.empty() ? " (device unknown)"sv : ""sv);
        } else if (const auto opened = open_pad(*input, id.globalIndex, true); opened != pad_open::opened) {
            input->roles.detach(id.globalIndex);
            if (opened == pad_open::failed) return -1;
            // Its device may only watch: the slot stays, so a press joins it once the device has a profile for it.
            input->waiting[id.globalIndex] = input->spectating[id.globalIndex] = true;
            input->watch_tried[id.globalIndex] = std::chrono::steady_clock::now();
        }
    }
    request_motion(*input, id.globalIndex);
    return 0;
}

int rebind_gamepad(input_t &input, const gamepad_id_t &id, feedback_queue_t feedback) {
    if (id.globalIndex < 0 || id.globalIndex >= MAX_GAMEPADS) return -1;
    std::lock_guard lock(input->mutex);
    input->feedback[id.globalIndex] = std::move(feedback);
    input->client_index[id.globalIndex] = id.clientRelativeIndex;
    request_motion(*input, id.globalIndex);
    return 0;
}

void free_gamepad(input_t &input, int nr) {
    if (nr < 0 || nr >= MAX_GAMEPADS) return;
    std::lock_guard lock(input->mutex);
    const uint32_t seated = input->seat[nr];
    input->seat[nr] = 0;
    input->switch_since[nr] = {};
    input->switch_done[nr] = false;
    if (input->waiting[nr]) {
        input->waiting[nr] = false;
        input->roles.detach(nr);
        input->owner[nr].clear();
        BOOST_LOG(info) << "PS5 virtual pad "sv << nr << (input->spectating[nr] ? " gone, it only watched"sv : " gone, never used"sv);
        input->spectating[nr] = false;
        return;
    }
    const std::string owner = std::exchange(input->owner[nr], std::string());
    if (!input->pads[nr]) return;
    set_motors(*input, nr, 0, 0);
    set_triggers(*input, nr, prospero::triggers_off);
    const int rc = ps_pad_close(input->pads[nr]);  // sends a neutral state before removal
    removed_device();
    input->pads[nr] = nullptr;
    // Its device's own keyboard and mouse go with its last controller: from then on they play as a controller of their
    // own (keyboard_pad), as for any device without one.
    if (std::none_of(input->owner.begin(), input->owner.end(), [&](const std::string &o) { return o == owner; }))
        close_own_kbm(*input, owner);
    const bool was_host = input->roles.is_host(nr);
    input->roles.detach(nr);
    input->colors[nr] = {};  // a later pad in this slot reports its colour anew
    input->motor_choice[nr] = {};
    input->triggers[nr] = {};
    input->touch[nr].clear();
    input->back[nr] = false;
    input->ps_held[nr] = false;
    input->combo[nr] = {};
    input->held_back[nr] = false;
    input->menu_checked[nr] = {};
    input->last_state[nr] = {};
    input->keyboard_ps[nr] = input->keyboard_ps_up[nr] = false;
    input->keyboard_ps_until[nr] = {};
    BOOST_LOG(info) << "PS5 virtual pad "sv << nr << " detached ("sv << rc << ')'
                    << (was_host ? (input->roles.host_away() ? ", the host's device keeps the role"sv : ", the host role is free"sv) : ""sv);
    release_profile(*input, seated);
}

std::optional<prospero::MenuProfiles> menu_profiles() { return prospero::read_menu_profiles(platf::appdata() / "menu_profiles.txt"); }

// Whether a paired device gets "PS5 Screen" (control/menu_access.hpp).
bool screen_offered_to(const std::string &cert) {
    const auto menus = menu_profiles();
    return !menus || prospero::screen_offered(menus, profiles_of(paired_device(cert).uuid));
}

// What decides a pad's PS and Create (control/menu_access.hpp): the profiles the owner gave the console's menus and the
// profile the host seated it in. Looked up at most once a second, so a change on the web page counts within a second.
// Caller holds input.mutex.
static prospero::MenuAccess pad_menu_access(input_raw_t &input, int nr) {
    const auto now = std::chrono::steady_clock::now();
    if (input.menu_checked[nr].time_since_epoch().count() != 0 && now - input.menu_checked[nr] < 1s) return input.menu_access[nr];
    input.menu_checked[nr] = now;
    const auto menus = menu_profiles();
    // While the menus aren't chosen, the device's profiles don't matter.
    input.menu_access[nr] = menus ? prospero::menu_access(menus, profiles_of(paired_device(input.owner[nr]).uuid), input.seat[nr])
                                  : prospero::MenuAccess::by_role;
    return input.menu_access[nr];
}

// A pad's virtual controller for its buttons, sticks, touchpad and motion: every use goes through here. Null while the pad
// has none, or while its input waits: no game is in front and its profile doesn't reach the console's menus (the one rule,
// prospero::passes_input). Caller holds input.mutex.
static ps_pad *pad_for_input(input_raw_t &input, int nr) {
    if (!input.pads[nr] || !prospero::passes_input(pad_menu_access(input, nr), input.game_in_front)) return nullptr;
    return input.pads[nr];
}

// A pad whose input waits (pad_for_input). Once it starts waiting it lets go of everything, as a button held while the game
// ended would stay down on the home screen. A press tells its player why, at most once every switch_cooldown. Caller holds
// input.mutex.
static void hold_back(input_raw_t &input, int nr, const gamepad_state_t &state) {
    if (!input.held_back[nr]) {
        input.held_back[nr] = true;
        input.back[nr] = input.ps_held[nr] = false;
        input.touch[nr].clear();
        ps_pad_neutral(input.pads[nr]);
        BOOST_LOG(info) << "PS5 virtual pad "sv << nr << ": no game in front, and its profile doesn't reach the console's menus: its input waits"sv;
    }
    if (!prospero::wakes_pad(state.buttonFlags, state.lt, state.rt, state.lsX, state.lsY, state.rsX, state.rsY)) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - input.held_told[nr] < prospero::switch_cooldown) return;
    input.held_told[nr] = now;
    // Not while a game the host started for its profile is open: that game is about to come to the front (close_welcome).
    // Looked at once per switch_cooldown, as it reads the console's process list.
    if (launched_open(input, input.seat[nr], false) == prospero::Answer::yes) return;
    ps_notify((profile_name(input.seat[nr]) + " can't use the console's menus. Start a game from Moonlight").c_str());
}

// A client's controller state, with PS held on top while its device's keys hold it (keyboard_ps). Caller holds input.mutex.
void apply_gamepad(input_raw_t *input, int nr, const gamepad_state_t &client_state) {
    gamepad_state_t state = client_state;
    if (input->keyboard_ps[nr]) state.buttonFlags |= HOME;
    if (input->waiting[nr]) {
        if (!prospero::wakes_pad(state.buttonFlags, state.lt, state.rt, state.lsX, state.lsY, state.rsX, state.rsY)) return;
        // A device that may only watch tries again at most once a second while it presses.
        const auto now = std::chrono::steady_clock::now();
        if (input->spectating[nr] && now - input->watch_tried[nr] < 1s) return;
        input->watch_tried[nr] = now;
        // This first use only brings up the console's question for the new pad (or, for a device with its own
        // profiles, seats it in the next one); the next press answers it. A watching pad takes a role anew.
        const bool host = input->spectating[nr] && input->roles.attach(nr, input->owner[nr]);
        switch (open_pad(*input, nr, host)) {
            case pad_open::opened:
                input->waiting[nr] = input->spectating[nr] = false;
                break;
            case pad_open::watching:
                input->spectating[nr] = true;
                input->roles.detach(nr);
                return;
            case pad_open::failed:
                input->waiting[nr] = input->spectating[nr] = false;
                input->roles.detach(nr);
                return;
        }
        request_motion(*input, nr);
    }
    if (!input->pads[nr]) return;
    // L1+L2+R1+R2 held for switch_hold, or the keyboard pad's Ctrl+Backspace at once: poll_pad_output switches the profile.
    if (!prospero::holds_switch(state.buttonFlags, state.lt, state.rt)) {
        input->switch_since[nr] = {};
        input->switch_done[nr] = false;
    } else if (input->switch_since[nr].time_since_epoch().count() == 0) {
        const auto now = std::chrono::steady_clock::now();
        input->switch_since[nr] = prospero::switch_at_once(state.buttonFlags) ? now - prospero::switch_hold : now;
    }
    ps_pad *pad = pad_for_input(*input, nr);
    if (!pad) return hold_back(*input, nr, state);
    if (std::exchange(input->held_back[nr], false))
        BOOST_LOG(info) << "PS5 virtual pad "sv << nr << ": a game is in front, its input goes through again"sv;
    const auto access = pad_menu_access(*input, nr);
    const auto send = [&](uint32_t flags) {
        const auto mapped = prospero::map_gamepad(flags, state.lt, state.rt, state.lsX, state.lsY, state.rsX, state.rsY);
        const ps_pad_state out{mapped.buttons, mapped.lx, mapped.ly, mapped.rx, mapped.ry, mapped.l2, mapped.r2};
        ps_pad_send(pad, &out);
    };
    // Sunshine turns a held Select into PS (back_button_timeout), but Select is Create here and pressing it
    // first would open the Create menu. So Create only goes out, as a tap, when Select comes up early. The
    // margin keeps Sunshine's own forced release at the timeout from counting as early.
    const auto now = std::chrono::steady_clock::now();
    const auto timeout = config::input.back_button_timeout;
    const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    uint32_t pressed = state.buttonFlags;
    if (input->touch_combo[nr]) pressed = input->combo[nr].apply(pressed, now_ms, timeout.count());
    const auto role = input->roles.is_host(nr) ? prospero::PadRole::host : prospero::PadRole::guest;
    uint32_t flags = prospero::allowed_buttons(pressed, role, input->ps_all_players, access);
    if ((pressed & ~flags) && !(input->ps_held[nr]))
        BOOST_LOG(info) << "PS5 virtual pad "sv << nr << ": PS or Create ignored, "sv
                        << (access == prospero::MenuAccess::denied ? "its profile doesn't reach the console's menus"sv
                                                                   : "only the host has them (ps_button_all_players)"sv);
    input->ps_held[nr] = pressed & ~flags;
    bool create_tap = false;
    if (timeout >= 0ms) {
        const bool back = flags & BACK;
        if (back && !input->back[nr]) input->back_since[nr] = now;
        // Select that went into the touchpad combination is no tap of Create when it comes up.
        create_tap = !back && input->back[nr] && now - input->back_since[nr] < timeout - 20ms &&
                     !(input->touch_combo[nr] && input->combo[nr].busy(now_ms));
        input->back[nr] = back;
        flags &= ~BACK;
    }
    send(flags);
    if (create_tap) {
        send(flags | BACK);
        std::this_thread::sleep_for(50ms);
        send(flags);
    }
    // Sunshine neutralises held input with an all-zero state when a session ends (P4.3).
    const bool active = state.buttonFlags || state.lt || state.rt || state.lsX || state.lsY || state.rsX || state.rsY;
    if (input->active[nr] && !active) BOOST_LOG(debug) << "PS5 virtual pad "sv << nr << " released to neutral"sv;
    input->active[nr] = active;
}

void gamepad_update(input_t &input, int nr, const gamepad_state_t &state) {
    if (nr < 0 || nr >= MAX_GAMEPADS) return;
    std::lock_guard lock(input->mutex);
    input->last_state[nr] = state;
    apply_gamepad(&*input, nr, state);
}

void gamepad_touch(input_t &input, const gamepad_touch_t &touch) {
    const int nr = touch.id.globalIndex;
    if (nr < 0 || nr >= MAX_GAMEPADS) return;
    std::lock_guard lock(input->mutex);
    if (!input->pads[nr]) return;
    auto &fingers = input->touch[nr];
    // Sunshine sends this when a session ends and the pad stays. A last motion sample left in place would keep turning a
    // game's camera; a client sending it mid-stream only loses the 10 ms to its next sample.
    if (touch.eventType == prospero::moonlight::touch_cancel_all) {
        fingers.clear();
        ps_pad_rest(input->pads[nr]);
        return;
    }
    // The touchpad moves the pointer of some of the console's screens.
    ps_pad *pad = pad_for_input(*input, nr);
    if (!pad) return;
    if (!fingers.update(touch.eventType, touch.pointerId, touch.x, touch.y)) return;
    std::array<ps_pad_touch, 2> points{};
    for (int i = 0; i < fingers.count(); i++) {
        const auto &point = fingers.points()[i];
        points[i] = {point.id, point.x, point.y};
    }
    ps_pad_set_touch(pad, fingers.count(), points.data());
}

void gamepad_motion(input_t &input, const gamepad_motion_t &motion) {
    const int nr = motion.id.globalIndex;
    if (nr < 0 || nr >= MAX_GAMEPADS) return;
    std::lock_guard lock(input->mutex);
    ps_pad *pad = pad_for_input(*input, nr);
    if (!pad) return;
    if (motion.motionType == prospero::moonlight::motion_accel) {
        const float xyz[3] = {prospero::accel_to_g(motion.x), prospero::accel_to_g(motion.y), prospero::accel_to_g(motion.z)};
        ps_pad_set_acceleration(pad, xyz);
    } else if (motion.motionType == prospero::moonlight::motion_gyro) {
        const float xyz[3] = {prospero::gyro_to_rad(motion.x), prospero::gyro_to_rad(motion.y), prospero::gyro_to_rad(motion.z)};
        ps_pad_set_angular_velocity(pad, xyz);
    }
}

std::vector<supported_gamepad_t> &supported_gamepads(input_t *) {
    static std::vector<supported_gamepad_t> pads{{"ds5", true, ""}};
    return pads;
}

platform_caps::caps_t get_capabilities() { return platform_caps::controller_touch; }


void touch_update(client_input_t *, const touch_port_t &, const touch_input_t &) {}
void pen_update(client_input_t *, const touch_port_t &, const pen_input_t &) {}
void gamepad_battery(input_t &, const gamepad_battery_t &) {}
}
