// SPDX-License-Identifier: GPL-3.0-only
// What the PS5 input files (input_ps5.cpp, input_seats_ps5.cpp, input_pads_ps5.cpp, input_kbm_ps5.cpp) share: the
// input object, and the functions one of them defines and another calls. Nothing else includes it.
#pragma once

#include "control/device_profiles.hpp"
#include "control/menu_access.hpp"
#include "input/gamepad_map.hpp"
#include "input/keyboard_map.hpp"
#include "input/pad_roles.hpp"
#include "input/pad_settings.hpp"
#include "input/pad_touch.hpp"
#include "input/profile_rules.hpp"
#include "input/text_typist.hpp"
#include "platform/ps5/haptic_envelope.hpp"
#include "platform/ps5/kbm.h"
#include "platform/ps5/pad.h"
#include "src/config.h"
#include "src/platform/common.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

using namespace std::literals;

namespace platf {

// Input: one native virtual DualSense per Moonlight gamepad slot, and one keyboard and one mouse shared by all clients.
// A keyboard and a mouse on the console, with what they hold. The shared ones serve the devices that can choose any
// profile, as the console's own. A device with its own profiles gets its own, seated in the profile it plays as
// (kbm_for_input). Added on first use, removed when their device's last controller leaves or no session is left.
struct kbm_t {
    std::string owner;  // the device's certificate; empty for the shared ones
    uint32_t user = 0;  // the profile they are seated in; 0 for the shared ones
    ps_keyboard *keyboard = nullptr;
    ps_mouse *mouse = nullptr;
    uint64_t keyboard_bus = 0, mouse_bus = 0;  // the bus ids they were seated by; 0 for the shared ones
    bool keyboard_failed = false, mouse_failed = false;
    prospero::KeyboardState keys;
    prospero::TextTypist text;  // a text from its client's on-screen keyboard, typed by type_texts
    uint8_t mouse_buttons = 0;
    prospero::WheelSteps wheel, tilt;
    std::chrono::steady_clock::time_point key_down_at, button_down_at;
    std::optional<std::pair<float, float>> pointer;  // the last absolute position, for abs_mouse
};

// Whether sunshine.conf changed since the last look, looked at no more often than every 2 s: the pads' settings
// (input/pad_settings.hpp) take effect within seconds of saving, in a running stream too.
// The size counts as well as the write time. Once in 17 saves over FTP (which truncates the file, then writes it) the host
// kept the settings of a half-written file until the next save: the look came while the upload was still writing, and the
// finished file showed the same write time, most likely because the console keeps it in whole seconds.
class settings_watch_t {
public:
    bool changed() {
        const auto now = std::chrono::steady_clock::now();
        if (now - checked_ < 2s) return false;
        checked_ = now;
        const auto seen = stamp();
        if (!seen || *seen == seen_) return false;
        seen_ = *seen;
        return true;
    }

private:
    using stamp_t = std::pair<std::filesystem::file_time_type, std::uintmax_t>;
    static std::optional<stamp_t> stamp() {
        std::error_code ec;
        const auto written = std::filesystem::last_write_time(config::sunshine.config_file, ec);
        if (ec) return std::nullopt;
        const auto size = std::filesystem::file_size(config::sunshine.config_file, ec);
        if (ec) return std::nullopt;
        return stamp_t{written, size};
    }
    stamp_t seen_ = stamp().value_or(stamp_t{});
    std::chrono::steady_clock::time_point checked_ = std::chrono::steady_clock::now();
};

struct input_raw_t {
    std::mutex mutex;
    std::array<ps_pad *, MAX_GAMEPADS> pads{};
    // A guest's slot whose pad isn't open yet: it opens on the controller's first use (wakes_pad, GitHub issue 26).
    std::array<bool, MAX_GAMEPADS> waiting{};
    // A waiting pad of a device that may only watch: it has no profile to play as (none, or all taken, or the one it
    // played as was taken away). It holds no role; a press tries again, at most once a second, and the console says why
    // it doesn't join at most once every switch_cooldown.
    std::array<bool, MAX_GAMEPADS> spectating{};
    std::array<std::chrono::steady_clock::time_point, MAX_GAMEPADS> watch_tried{}, watch_told{};
    std::array<bool, MAX_GAMEPADS> active{};  // last state had something pressed or deflected
    std::array<bool, MAX_GAMEPADS> back{};    // Select is down
    std::array<bool, MAX_GAMEPADS> ps_held{}; // a guest's PS or Create press that was held back, logged once per press
    std::array<std::chrono::steady_clock::time_point, MAX_GAMEPADS> back_since{};
    // A controller without a touchpad clicks it with Select and Start together (gamepad_map.hpp, TouchpadCombo).
    std::array<bool, MAX_GAMEPADS> touch_combo{};
    std::array<prospero::TouchpadCombo, MAX_GAMEPADS> combo{};
    std::array<feedback_queue_t, MAX_GAMEPADS> feedback{};
    std::array<uint8_t, MAX_GAMEPADS> client_index{};
    std::array<std::array<uint8_t, 2>, MAX_GAMEPADS> motors{};
    std::array<std::array<uint8_t, 3>, MAX_GAMEPADS> colors{};
    std::array<prospero::TriggerBlock, MAX_GAMEPADS> triggers{};
    std::array<prospero::PadTouch, MAX_GAMEPADS> touch{};
    kbm_t shared;                             // removed when no session is left (client_session)
    std::vector<std::pair<uint64_t, int>> owner_fixes;  // keep_owners: times each bus id was seated back
    // Profiles the host signed in whose last controller left: signed out once their time comes (sign_out_due).
    struct pending_sign_out_t {
        uint32_t user;
        std::chrono::steady_clock::time_point at;
        bool told;  // the log said it waits
    };
    std::vector<pending_sign_out_t> sign_out_at;
    // The console's welcome screen over a game the host started for a profile it signed in (close_welcome): for each pad,
    // the profile it sits in, since when, and whether that seating was looked at already; the pad pressing X until when.
    struct pad_welcome_t {
        uint32_t seat = 0;
        std::chrono::steady_clock::time_point since{};
        bool done = false;
    };
    std::array<pad_welcome_t, MAX_GAMEPADS> welcome{};
    int welcome_pressed = -1;
    std::chrono::steady_clock::time_point welcome_release{}, welcome_check{};
    // The title the host last started for each profile it signed in (sign_in_for_launch, note_launch): while it is open,
    // the profile isn't signed out (prospero::sign_out).
    std::vector<std::pair<uint32_t, std::string>> launched;
    std::vector<std::unique_ptr<kbm_t>> own;  // one per device with its own profiles that used them
    int sessions = 0;
    uint32_t user = 0;
    // Profiles (device_profiles.hpp): the paired device each pad belongs to, by its certificate, and the profile the host
    // seated it in (0 when the console asked).
    std::array<std::string, MAX_GAMEPADS> owner{};
    std::array<uint32_t, MAX_GAMEPADS> seat{};
    // Since when a pad holds the switch (L1+L2+R1+R2, or the keyboard pad's Ctrl+Backspace), and whether it switched already.
    std::array<std::chrono::steady_clock::time_point, MAX_GAMEPADS> switch_since{};
    std::array<bool, MAX_GAMEPADS> switch_done{};
    std::array<prospero::SwitchBudget, MAX_GAMEPADS> switch_budget{};  // a round of switches, then switch_cooldown
    // The profiles this host signed in: it signs each out again once none of its pads sits in it (release_profile), so
    // the console's four sessions don't fill up with players who left, except those in front then (prospero::HostProfiles).
    prospero::HostProfiles host_profiles;
    std::chrono::steady_clock::time_point kept_checked;  // forget_signed_out
    // The console's menus (control/menu_access.hpp): what decides each pad's PS and Create, looked up at most once a second
    // and anew when it opens or switches profile; whether its input waits, as no game is in front and its profile doesn't
    // reach the menus; and when its player was last told so.
    std::array<prospero::MenuAccess, MAX_GAMEPADS> menu_access{};
    std::array<std::chrono::steady_clock::time_point, MAX_GAMEPADS> menu_checked{}, held_told{};
    std::array<bool, MAX_GAMEPADS> held_back{};
    // Windows+Esc on the keys of a device that has a controller is that controller's PS (keyboard_map.hpp, PsChord): each
    // pad's last state from its client, so that PS goes down and up on top of what it holds, and whether a key holds PS on
    // it; per device (by its certificate) the chord and the pad its PS went down on, -1 for none.
    std::array<gamepad_state_t, MAX_GAMEPADS> last_state{};
    std::array<bool, MAX_GAMEPADS> keyboard_ps{};
    // Windows+Shift+Esc: its PS stays down until then at least, and whether Esc already came up (poll_pad_output lets go).
    std::array<std::chrono::steady_clock::time_point, MAX_GAMEPADS> keyboard_ps_until{};
    std::array<bool, MAX_GAMEPADS> keyboard_ps_up{};
    std::map<std::string, std::pair<prospero::PsChord, int>> ps_chords;
    // The devices told how to press PS from the keyboard (hint_ps_keys), once while streams go on.
    std::set<std::string> ps_hint_told;
    bool game_in_front = true;  // refreshed every half second by poll_pad_output
    bool ps_all_players = false;  // ps_button_all_players: every pad may press PS and Create, not only the host's
    std::atomic_int knock_percent{100};  // trigger_knock_strength
    prospero::PadSettings pad_settings;  // as last read, by input() and then poll_pad_output only
    settings_watch_t settings_watch;     // poll_pad_output's, made before the first read so no change is missed
    prospero::PadRoles<MAX_GAMEPADS> roles;  // who is the host: a paired device, not a slot (GitHub issue 15)
    std::jthread output_thread;
    std::jthread haptic_thread;
    prospero::HapticEnvelope haptics;
    std::atomic_bool haptics_enabled{false};  // haptic_mode = rumble
    std::array<prospero::MotorChoice, MAX_GAMEPADS> motor_choice{};
};

// A paired device by its certificate: its uuid (what device_profiles.txt goes by) and its name, for notifications.
struct paired_device_t {
    std::string uuid, name;
};

// What open_pad did: the pad is open, its device may only watch for now (no profile for it), or it failed.
enum class pad_open { opened, watching, failed };

// input_ps5.cpp
void set_motors(input_raw_t &input, int nr, uint8_t large, uint8_t small);
void set_triggers(input_raw_t &input, int nr, const prospero::TriggerBlock &block);
void removed_device();
void settle_bus();
extern std::mutex live_input_mutex;
extern input_raw_t *live_input;

// input_seats_ps5.cpp
void note_credential_restore();
std::string gamepad_owner(const feedback_queue_t &queue);
paired_device_t paired_device(const std::string &cert);
std::string profile_name(uint32_t user);
std::optional<prospero::ProfileSet> profiles_of(const std::string &uuid);
void release_profile(input_raw_t &input, uint32_t user);
prospero::Answer launched_open(input_raw_t &input, uint32_t user, bool forget = true);
void sign_out_due(input_raw_t &input);
void keep_owners(input_raw_t &input);
void close_welcome(input_raw_t &input);
pad_open add_pad(input_raw_t &input, int nr, bool host);
void check_seat_switches(input_raw_t &input);

// input_pads_ps5.cpp
void request_motion(const input_raw_t &input, int nr);
std::optional<prospero::MenuProfiles> menu_profiles();
void apply_gamepad(input_raw_t *input, int nr, const gamepad_state_t &client_state);
void forget_pad_state(input_raw_t &input, int nr);

// input_kbm_ps5.cpp
void close_own_kbm(input_raw_t &input, const std::string &owner);
void close_keyboard_mouse(input_raw_t &input);
void release_keyboard_ps(input_raw_t &input);
void type_texts(input_raw_t &input);

}  // namespace platf
