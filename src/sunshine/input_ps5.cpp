// SPDX-License-Identifier: GPL-3.0-only
// Sunshine's input object on the PS5 (platf::input) and its life: made with its settings, the threads that pass on
// what the system wants from each pad (motors, light bar, trigger effects, the haptic channel as rumble), freed with
// the profiles the host signed in, and the client sessions that keep the keyboard and mouse. Seats in
// input_seats_ps5.cpp, pads in input_pads_ps5.cpp, keyboard and mouse in input_kbm_ps5.cpp.
#include "sunshine/input_ps5_internal.hpp"
#include "apps/titles.hpp"
#include "control/menu_access.hpp"
#include "input/gamepad_map.hpp"
#include "input/pad_settings.hpp"
#include "input/profile_rules.hpp"
#include "platform/ps5/capture.h"
#include "platform/ps5/haptic_envelope.hpp"
#include "platform/ps5/haptic_guard.h"
#include "platform/ps5/pad.h"
#include "platform/ps5/profile_seat.h"
#include "platform/ps5/shared_capture.hpp"
#include "platform/ps5/title_launch.h"
#include "src/config.h"
#include "src/file_handler.h"
#include "src/globals.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/utility.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

using namespace std::literals;

namespace platf {

void set_motors(input_raw_t &input, int nr, uint8_t large, uint8_t small) {
    if (input.motors[nr] == std::array<uint8_t, 2>{large, small}) return;
    input.motors[nr] = {large, small};
    BOOST_LOG(debug) << "PS5 virtual pad "sv << nr << " motors "sv << int(large) << ',' << int(small);
    if (input.feedback[nr]) input.feedback[nr]->raise(gamepad_feedback_msg_t::make_rumble(input.client_index[nr], large * 257, small * 257));
}

static void set_color(input_raw_t &input, int nr, uint8_t r, uint8_t g, uint8_t b) {
    if (input.colors[nr] == std::array<uint8_t, 3>{r, g, b}) return;
    input.colors[nr] = {r, g, b};
    BOOST_LOG(debug) << "PS5 virtual pad "sv << nr << " light bar "sv << int(r) << ',' << int(g) << ',' << int(b);
    if (input.feedback[nr]) input.feedback[nr]->raise(gamepad_feedback_msg_t::make_rgb_led(input.client_index[nr], r, g, b));
}

void set_triggers(input_raw_t &input, int nr, const prospero::TriggerBlock &block) {
    if (input.triggers[nr] == block) return;
    const auto effects = prospero::map_trigger_effects(block, input.triggers[nr], input.knock_percent);
    input.triggers[nr] = block;
    static constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    for (const uint8_t byte : block) {
        hex += digits[byte >> 4];
        hex += digits[byte & 15];
    }
    BOOST_LOG(debug) << "PS5 virtual pad "sv << nr << " triggers "sv << hex;
    if (effects.flags && input.feedback[nr]) {
        input.feedback[nr]->raise(gamepad_feedback_msg_t::make_adaptive_triggers(input.client_index[nr], effects.flags, effects.type_left, effects.type_right, effects.left, effects.right));
    }
}

// Whether a game is in front, for pads whose profile doesn't reach the console's menus (control/menu_access.hpp): the
// console's running big app is a title the app list shows as a game (titles.cpp), and it has the screen. The home screen,
// an app of the console's, a media app or homebrew such as the settings app or a store count as menus, and so does the
// home screen over a game left running (PS held on another controller). The control center that a short PS opens over a
// game leaves the game the screen (measured on 10.20), so it counts as in front. The running big app is
// sceLncUtilGetAppIdOfRunningBigApp's, the one the keyboard pad's profile goes by (patch 0018); who has the screen,
// ps_title_focused's. A title's kind is read once, when it comes to the front. Should the system not tell who has the
// screen, the running game counts, as before, and the log says so once.
static bool game_in_front() {
    static std::mutex mutex;
    static std::string title;
    static bool game = false;
    char running[16];
    if (ps_title_running(running) <= 0) return false;
    std::lock_guard lock(mutex);
    if (title != running) {
        title = running;
        const auto meta = prospero::read_title_meta("/user", title);
        game = meta && meta->game;
    }
    if (!game) return false;
    const int focused = ps_title_focused();
    static std::once_flag unknown;
    if (focused < 0) std::call_once(unknown, [] { BOOST_LOG(warning) << "PS5 menus: the system doesn't say which app has the screen; a game left open behind the home screen counts as in front"sv; });
    return focused != 0;
}

// Sunshine doesn't know these keys (input/pad_settings.hpp). Read at the start and again whenever the file changes, so a
// change takes effect within seconds, in a running stream too: the knock from the next effect the game sends, the vibration
// at once. Logs what changed.
static void load_pad_settings(input_raw_t &input, bool first) {
    auto read = prospero::read_pad_settings(config::parse_config(file_handler::read_file(config::sunshine.config_file.c_str())));
    for (const auto &note : read.notes) BOOST_LOG(warning) << "PS5 input: "sv << note;
    const auto before = std::exchange(input.pad_settings, std::move(read));
    const auto &now = input.pad_settings;
    input.knock_percent = now.knock;
    input.haptics.set_strength(now.haptic_strength);
    input.haptics_enabled = now.rumble;
    if (now.knock != before.knock) BOOST_LOG(info) << "PS5 input: trigger knock "sv << now.knock << '%';
    if (first || now.rumble != before.rumble || now.haptic_strength != before.haptic_strength)
        BOOST_LOG(info) << "PS5 input: vibration "sv << (now.rumble ? "rumble"sv : "off"sv) << " at "sv << now.haptic_strength << '%';
}

// Reads what the system wants from each pad at the cadence of Remote Play's own reader and passes on
// changes of the motors, the light bar and the trigger effects. Only titles using the classic two-motor rumble set the motors;
// see docs/re/pad-output.md. Every half second, while pads are open, it also looks whether a game is in front.
// How often the host checks that the console left its seated devices in their profiles (keep_owners).
constexpr auto owner_check = 100ms;

static void poll_pad_output(std::stop_token stop, input_raw_t *input) {
    std::array<bool, MAX_GAMEPADS> failed{};
    auto next_owner_check = std::chrono::steady_clock::now();
    bool any_pad = false;
    for (unsigned tick = 0; !stop.stop_requested(); ++tick) {
        std::optional<bool> front;
        if (any_pad && tick % 50 == 0) front = game_in_front();
        const auto rumble = input->haptics.tick();
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (input->settings_watch.changed()) load_pad_settings(*input, false);
        {
            std::lock_guard lock(input->mutex);
            if (front) input->game_in_front = *front;
            any_pad = std::any_of(input->pads.begin(), input->pads.end(), [](const ps_pad *pad) { return pad != nullptr; });
            check_seat_switches(*input);
            release_keyboard_ps(*input);
            if (const auto now = std::chrono::steady_clock::now(); now >= next_owner_check) {
                next_owner_check = now + owner_check;
                keep_owners(*input);
                sign_out_due(*input);
            }
            close_welcome(*input);
            for (int nr = 0; nr < MAX_GAMEPADS; ++nr) {
                if (!input->pads[nr]) {
                    failed[nr] = false;
                    continue;
                }
                ps_pad_output output{};
                if (const int rc = ps_pad_read_output(input->pads[nr], &output); rc) {
                    if (!failed[nr]) BOOST_LOG(warning) << "PS5 virtual pad "sv << nr << " output read failed: "sv << rc;
                    failed[nr] = true;
                    continue;
                }
                // The channel is the console's, one for all players: only the host's pad feels it, so a guest doesn't feel the host's
                // shots. A game's classic rumble is each pad's own, as before.
                const auto from_channel = input->roles.is_host(nr) ? rumble : std::array<uint8_t, 2>{0, 0};
                const auto motors = input->motor_choice[nr].choose({output.motors[0], output.motors[1]}, from_channel, now_ms);
                set_motors(*input, nr, motors[0], motors[1]);
                set_color(*input, nr, output.color[0], output.color[1], output.color[2]);
                prospero::TriggerBlock triggers;
                std::memcpy(triggers.data(), output.triggers, sizeof output.triggers);
                set_triggers(*input, nr, triggers);
            }
        }
        std::this_thread::sleep_for(10ms);
    }
}

// While this file exists the channel stays closed, whatever sunshine.conf says: the host died with it open, or a call into it
// faulted. Delete it to try again.
static std::filesystem::path haptic_blocked() { return appdata() / "haptic_blocked"; }
// Exists from before the channel opens until it has run for a while. Found at the start, it means the host died with the channel
// open: then the channel gets blocked, so that one bad try can't break every stream that follows.
static std::filesystem::path haptic_attempt() { return appdata() / "haptic_attempt"; }

namespace {
struct haptic_open_call {
    ps_capture *session;
    const uint32_t *words;
};
int haptic_open_step(void *argument) {
    auto *call = static_cast<haptic_open_call *>(argument);
    return ps_capture_haptic_open(call->session, call->words, 0);
}
struct haptic_read_call {
    ps_capture *session;
    void *destination;
    std::size_t capacity;
    uint8_t *header;
};
int haptic_read_step(void *argument) {
    auto *call = static_cast<haptic_read_call *>(argument);
    return ps_capture_haptic_read(call->session, call->destination, call->capacity, call->header);
}
}

// Reads the haptic channel during a stream and hands its level to poll_pad_output as rumble. Games made for the PS5 send their
// vibration only this way; Moonlight has no channel for it, but it has the two motors. See docs/re/haptics.md.
static void poll_haptics(std::stop_token stop, input_raw_t *input) {
    const auto shutdown_event = mail::man->event<bool>(mail::shutdown);
    std::shared_ptr<prospero::SharedCapture> shared;
    unsigned generation = 0;
    bool open = false, gave_up = false, faulted = false, attempt_open = false, told_blocked = false;
    std::vector<uint8_t> packet(0x10000);
    alignas(16) uint8_t header[0x400];  // only 0x18 are known; room in case the call writes more
    prospero::HapticSplitter splitter;
    auto opened_at = std::chrono::steady_clock::now();
    std::error_code attempt_ec;
    if (std::filesystem::exists(haptic_attempt(), attempt_ec)) {
        BOOST_LOG(warning) << "PS5 haptics: the host stopped during the last try, the channel stays closed until "sv
                           << haptic_blocked().string() << " is deleted"sv;
        std::ofstream(haptic_blocked()).flush();
        std::filesystem::remove(haptic_attempt(), attempt_ec);
        told_blocked = true;
    }
    const auto attempt_over = [&] {
        if (!attempt_open) return;
        std::error_code ec;
        std::filesystem::remove(haptic_attempt(), ec);
        attempt_open = false;
    };
    ps_haptic_fault fault{};
    const auto try_open = [&](ps_capture *s) -> int {
        if (generation != shared->generation()) return PS_CAPTURE_ERROR;
        // Audio's first block (size, kind, purpose, encoder, resource id) with encoder 2, the one that opens on 9.60.
        constexpr uint32_t words[4] = {0, 2, 2, PS_CAPTURE_HAPTIC_RESOURCE};
        haptic_open_call call{s, words};
        const int rc = ps_haptic_guarded(haptic_open_step, &call, &fault);
        // After a fault the channel is forgotten without calling into it, a lock in there may still be held.
        if (rc == PS_HAPTIC_FAULT)
            ps_capture_haptic_abandon(s);
        else if (rc)
            BOOST_LOG(warning) << "PS5 haptics: open failed: "sv << util::hex(rc).to_string_view();
        return rc;
    };
    // A call into the channel faulted: closed for good, here and, through haptic_blocked, for the hosts that follow.
    const auto block = [&](std::string_view call) {
        BOOST_LOG(warning) << "PS5 haptics: "sv << call << " faulted, signal "sv << fault.signal << " at "sv
                           << util::hex(fault.instruction).to_string_view() << ", address "sv
                           << util::hex(fault.address).to_string_view() << "; the channel stays closed until "sv
                           << haptic_blocked().string() << " is deleted"sv;
        std::ofstream(haptic_blocked()).flush();
        told_blocked = true;
        open = false;
        shared.reset();
        faulted = gave_up = true;
        attempt_over();
    };
    const auto release = [&] {
        if (open && shared) shared->exclusive([&](ps_capture *s) { return generation == shared->generation() ? ps_capture_haptic_close(s) : 0; });
        open = false;
        shared.reset();
    };

    while (!stop.stop_requested()) {
        bool pad = false;  // the host's: only its pad gets the rumble (poll_pad_output)
        {
            std::lock_guard lock(input->mutex);
            for (int nr = 0; nr < MAX_GAMEPADS; ++nr) pad = pad || (input->pads[nr] && input->roles.is_host(nr));
        }
        std::error_code ec;
        // A stopping host lets go of the channel first, while its session is whole, and doesn't keep that session open for later.
        const bool stopping = shutdown_event->peek();
        if (stopping && open) BOOST_LOG(info) << "PS5 haptics: closing the channel, the host is stopping"sv;
        const bool blocked = !open && std::filesystem::exists(haptic_blocked(), ec);
        if (!blocked) told_blocked = false;
        if (blocked && pad && input->haptics_enabled && !told_blocked) {
            BOOST_LOG(warning) << "PS5 haptics: "sv << haptic_blocked().string() << " is there, the channel stays closed; delete it to try again"sv;
            told_blocked = true;
        }
        if (stopping || !pad || !input->haptics_enabled || blocked) {
            release();
            attempt_over();
            gave_up = faulted;
            std::this_thread::sleep_for(500ms);
            continue;
        }
        if (!open) {
            if (gave_up) {
                std::this_thread::sleep_for(500ms);
                continue;
            }
            shared = prospero::SharedCapture::attach();
            if (!shared) {
                std::this_thread::sleep_for(200ms);
                continue;
            }
            generation = shared->generation();
            std::ofstream(haptic_attempt()).flush();
            attempt_open = true;
            const int opened = shared->exclusive(try_open);
            if (opened == PS_HAPTIC_FAULT) {
                block("open");
                continue;
            }
            open = opened == 0;
            if (!open) {
                BOOST_LOG(warning) << "PS5 haptics: the channel didn't open, no more tries in this stream"sv;
                shared.reset();
                gave_up = true;
                attempt_over();
                continue;
            }
            BOOST_LOG(info) << "PS5 haptics: channel open"sv;
            opened_at = std::chrono::steady_clock::now();
        }
        if (generation != shared->generation()) {
            open = false;
            shared.reset();
            continue;
        }
        const int rc = shared->with_session([&](ps_capture *s) {
            haptic_read_call call{s, packet.data(), packet.size(), header};
            return ps_haptic_guarded(haptic_read_step, &call, &fault);
        });
        if (rc == PS_HAPTIC_FAULT) {
            shared->exclusive([](ps_capture *s) {
                ps_capture_haptic_abandon(s);
                return 0;
            });
            block("read");
            continue;
        }
        if (attempt_open && std::chrono::steady_clock::now() - opened_at >= 5s) attempt_over();
        if (rc < 0) {
            BOOST_LOG(warning) << "PS5 haptics: read failed: "sv << rc;
            release();
            std::this_thread::sleep_for(1s);
            continue;
        }
        if (rc > 0) {
            const auto frames = std::min(prospero::kHapticFrames, static_cast<std::size_t>(rc) / (2 * sizeof(float)));
            input->haptics.feed(splitter.process(reinterpret_cast<const float *>(packet.data()), frames));
        } else {
            std::this_thread::sleep_for(2ms);
        }
    }
    release();
    attempt_over();
}

// The system bus stays busy for a while after the host removes devices. The login manager still decides about a new
// device a few ms after it arrives, but this process learns of it later, and a bind that comes after that decision leaves
// the console's question up. Seen once on 10.20 (2026-11-07): a keyboard pad added 50 ms after its client's pad, keyboard
// and mouse went lost by a millisecond. That was 1 of 48 adds so soon after a removal, and none of about 210 others lost.
// So a device to be seated in a profile is added only bus_settle after the host's last removal; keep_owners covers the
// rest.
constexpr auto bus_settle = 250ms;
std::atomic<std::chrono::steady_clock::rep> last_removal{0};  // steady_clock ticks; 0 while nothing was removed

void removed_device() {
    last_removal = std::chrono::steady_clock::now().time_since_epoch().count();
}

void settle_bus() {
    const auto last = last_removal.load();
    if (!last) return;
    const auto since = std::chrono::steady_clock::now() -
                       std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(last));
    if (since < bus_settle) std::this_thread::sleep_for(bus_settle - since);
}

// The platform's input, for the control socket's thread (profiles_changed). input() sets it and freeInput clears it
// before anything goes; taken before input.mutex, never after.
std::mutex live_input_mutex;
input_raw_t *live_input = nullptr;


void freeInput(input_raw_t *input) {
    if (!input) return;
    {
        std::lock_guard live(live_input_mutex);
        if (live_input == input) live_input = nullptr;
    }
    input->haptic_thread.request_stop();
    if (input->haptic_thread.joinable()) input->haptic_thread.join();
    input->output_thread.request_stop();
    if (input->output_thread.joinable()) input->output_thread.join();
    for (auto *&pad : input->pads) {
        ps_pad_close(pad);
        pad = nullptr;
    }
    // The profiles it signed in go with it, also those still in their grace (prospero::sign_out decides; no pad is left).
    // If the console doesn't say which one is in front, they all stay: signing out the one someone plays as is worse than
    // leaving one signed in.
    // Those kept as they were in front stay signed in, as they would without the host.
    if (!input->host_profiles.signed_in.empty()) {
        uint32_t front = 0;
        const bool known = ps_profile_foreground(&front) == 0;
        if (!known) BOOST_LOG(warning) << "PS5 input: the console didn't say which profile is in front; the host's profiles stay signed in"sv;
        for (const uint32_t user : input->host_profiles.signed_in) {
            const prospero::SignOutFacts facts{true, false,
                                               !known ? prospero::Answer::unknown : front == user ? prospero::Answer::yes : prospero::Answer::no,
                                               prospero::Answer::no, launched_open(*input, user)};
            if (prospero::sign_out(facts) == prospero::SignOut::now) ps_profile_sign_out(user);
        }
        note_credential_restore();
    }
    close_keyboard_mouse(*input);
    delete input;
}

input_t input() {
    auto *raw = new input_raw_t;
    raw->user = ps_pad_default_user();
    // Not the user's id: this log goes into the report people post (app/src/psapp/diag.cpp).
    BOOST_LOG(info) << (raw->user ? "PS5 input: virtual pads ready"sv : "PS5 input: no user for virtual pads"sv);
    // The app's "PS for all players" (off by default). Sunshine doesn't know the key and leaves it alone.
    const auto settings = config::parse_config(file_handler::read_file(config::sunshine.config_file.c_str()));
    const auto all = settings.find("ps_button_all_players");
    raw->ps_all_players = all != settings.end() && all->second == "1";
    BOOST_LOG(info) << (raw->ps_all_players ? "PS5 input: every player can press PS and Create"sv
                                            : "PS5 input: only the host can press PS and Create"sv);
    // No profile names or ids: this log goes into the report people post.
    if (const auto menus = prospero::read_menu_profiles(appdata() / "menu_profiles.txt"))
        BOOST_LOG(info) << "PS5 input: the console's menus are for "sv << menus->users.size()
                        << " profile(s); on a device with profiles of its own, that decides PS and Create"sv;
    raw->game_in_front = game_in_front();
    load_pad_settings(*raw, true);
    raw->output_thread = std::jthread(poll_pad_output, raw);
    raw->haptic_thread = std::jthread(poll_haptics, raw);
    {
        std::lock_guard live(live_input_mutex);
        live_input = raw;
    }
    return input_t{raw};
}

// Sunshine keeps one of these per stream session, and keeps it for a reconnect like its pads. The keyboard and mouse
// go when the last one does, so the console doesn't list them (nor games show their prompts) once nobody streams.
class client_session: public client_input_t {
public:
    explicit client_session(input_raw_t *input): input_(input) {
        std::lock_guard lock(input_->mutex);
        ++input_->sessions;
    }

    ~client_session() override {
        std::lock_guard lock(input_->mutex);
        if (--input_->sessions == 0) {
            close_keyboard_mouse(*input_);
            input_->ps_chords.clear();
            input_->ps_hint_told.clear();
        }
    }

private:
    input_raw_t *input_;
};

std::unique_ptr<client_input_t> allocate_client_input_context(input_t &input) {
    return std::make_unique<client_session>(input.get());
}

}
