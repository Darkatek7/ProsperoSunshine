// SPDX-License-Identifier: GPL-3.0-only
// Who plays as which console profile on the PS5: the paired device each session's pads belong to, the profiles a
// device's pads are seated in (signed in and out by the host, kept in front, seated back), the profile a game starts
// for, the welcome screen over it, profile switches and a device whose profiles changed.
#include "sunshine/input_ps5_internal.hpp"
#include "control/device_profiles.hpp"
#include "input/gamepad_map.hpp"
#include "input/profile_rules.hpp"
#include "platform/ps5/pad.h"
#include "platform/ps5/profile_seat.h"
#include "platform/ps5/title_launch.h"
#include "sunshine/platform_ps5.hpp"
#include "src/logging.h"
#include "src/nvhttp.h"
#include "src/platform/common.h"
#include "src/rtsp.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace std::literals;

namespace platf {

// Signing in and out borrows SceShellCore's credentials for one call (profile_seat.c). If this process's own couldn't be
// put back afterwards, the log says so, once per new failure.
void note_credential_restore() {
    static std::atomic<int> told{0};
    const int failures = ps_profile_credential_restore_failures();
    if (failures > told.exchange(failures))
        BOOST_LOG(error) << "PS5 input: this process's own credentials couldn't be put back after signing a profile in or out"sv;
}

namespace {
// Which paired device each session's feedback queue belongs to: stream::session::start() tells (patch 0016), and
// alloc_gamepad, which only gets the queue, asks. The weak pointer lets an ended session's entry go.
struct gamepad_owner_t {
    std::weak_ptr<void> queue;
    const void *key;
    std::string owner;
};
std::mutex gamepad_owners_mutex;
std::vector<gamepad_owner_t> gamepad_owners;
}  // namespace

std::string gamepad_owner(const feedback_queue_t &queue) {
    std::lock_guard lock(gamepad_owners_mutex);
    for (const auto &entry : gamepad_owners)
        if (entry.key == queue.get() && !entry.queue.expired()) return entry.owner;
    return {};
}

static void greet_spectator(const std::string &cert);

void set_gamepad_owner(const feedback_queue_t &queue, std::string_view owner) {
    if (!queue) return;
    {
        std::lock_guard lock(gamepad_owners_mutex);
        std::erase_if(gamepad_owners, [&](const gamepad_owner_t &entry) { return entry.queue.expired() || entry.key == queue.get(); });
        gamepad_owners.push_back({queue, queue.get(), std::string(owner)});
    }
    greet_spectator(std::string(owner));
}

paired_device_t paired_device(const std::string &cert) {
    if (cert.empty()) return {};
    for (const auto &client : nvhttp::get_all_clients()) {
        const std::string uuid = client.value("uuid", "");
        if (!uuid.empty() && nvhttp::get_cert_by_uuid(uuid) == cert) return {uuid, client.value("name", "")};
    }
    return {};
}

std::string profile_name(uint32_t user) {
    char name[64];
    ps_profile_name(user, name, sizeof name);
    return name[0] ? std::string(name) : "A player"s;
}

std::optional<prospero::ProfileSet> profiles_of(const std::string &uuid) {
    if (uuid.empty()) return std::nullopt;
    return prospero::DeviceProfiles(platf::appdata() / "device_profiles.txt").get(uuid);
}

namespace {
// What a new pad of this device gets: the console's question (profiles not set, or free choice), one of the device's
// profiles (open_pad picks the first it may join), or nothing (no profile: it only watches).
struct seat_plan_t {
    enum { ask, profile, none } kind = ask;
    std::vector<uint32_t> users;  // profile: the device's profiles in the order to try (prospero::join_order)
};

prospero::DeviceSeats seats() { return prospero::DeviceSeats(platf::appdata() / "device_seats.txt"); }

// Whether another of the host's pads sits in this profile, of any device: binding a second pad to it would take it away
// from that one.
bool seat_taken(const input_raw_t &input, int nr, uint32_t user) {
    for (int m = 0; m < MAX_GAMEPADS; ++m)
        if (m != nr && input.pads[m] && input.seat[m] == user) return true;
    return false;
}

// What the host knows of a profile for pad nr (prospero::join_block decides). Asks the console whether it is signed in.
prospero::ProfileUse profile_use(const input_raw_t &input, int nr, uint32_t user) {
    const int signed_in = ps_profile_signed_in(user);
    return {seat_taken(input, nr, user),
            signed_in < 0 ? prospero::Answer::unknown : signed_in ? prospero::Answer::yes : prospero::Answer::no,
            input.host_profiles.knows(user)};
}

// A limited device never gets the console's question: whoever answers it can pick any profile, and picking one in use
// moves it off the controller using it (the login manager disconnects that one, seen on 10.20). A Quick Play guest
// ("guest" in its profiles) would need the host to add one without the question, which isn't done yet.
seat_plan_t seat_plan(const input_raw_t &input, int nr, const std::string &uuid) {
    const auto set = profiles_of(uuid);
    if (!set || set->free) return {};
    if (set->users.empty()) return {seat_plan_t::none, {}};
    return {seat_plan_t::profile, prospero::join_order(*set, seats().get(uuid, input.client_index[nr]))};
}
}  // namespace

// A device that may only watch hears so as its stream starts, not at its first press: it has no profile to play as (a
// list with none, or only the Quick Play guest, which isn't there yet).
static void greet_spectator(const std::string &cert) {
    const auto device = paired_device(cert);
    const auto set = profiles_of(device.uuid);
    if (!set || set->free) return;
    if (set->users.empty()) {
        BOOST_LOG(info) << "PS5 input: a device that may only watch started streaming"sv;
        ps_notify(((device.name.empty() ? "This device"s : device.name) + " joined as a spectator (no profile to play as)").c_str());
        return;
    }
    // A device with profiles of its own that streams alone while nobody is in front (prospero::welcome_greeting): until its
    // controller is added, the console may show its welcome screen ("Press the PS button"), and a client without a PS
    // button (Moonlight's on-screen controls) only adds the controller at its first press. Any button starts it, and
    // close_welcome takes it from there.
    uint32_t front = 0;
    const int front_rc = ps_profile_foreground(&front);
    const auto someone_in_front = front_rc != 0 ? prospero::Answer::unknown : front ? prospero::Answer::yes : prospero::Answer::no;
    if (!prospero::welcome_greeting(rtsp_stream::session_count_except_cert(cert) == 0, someone_in_front)) return;
    std::lock_guard live(live_input_mutex);
    if (!live_input) return;
    std::lock_guard lock(live_input->mutex);
    const auto order = prospero::join_order(*set, seats().get(device.uuid, 0));
    if (order.empty()) return;
    BOOST_LOG(info) << "PS5 input: a device with profiles of its own started streaming alone; it was welcomed"sv;
    ps_notify(("Welcome, " + profile_name(order.front()) + "! Press any button to start").c_str());
}

// A profile this host signed in is signed out again once none of its pads sits in it, so players who left don't keep the
// console's four sessions: sign_out_grace after its last pad left, so that a Moonlight client that reconnects finds it as
// it was (sign_out_due). Caller holds input.mutex.
void release_profile(input_raw_t &input, uint32_t user) {
    if (!user || !input.host_profiles.signed_in_by_host(user)) return;
    for (int m = 0; m < MAX_GAMEPADS; ++m)
        if (input.pads[m] && input.seat[m] == user) return;
    const auto at = std::chrono::steady_clock::now() + prospero::sign_out_grace;
    auto &pending = input.sign_out_at;
    const auto it = std::find_if(pending.begin(), pending.end(), [&](const auto &p) { return p.user == user; });
    if (it != pending.end())
        *it = {user, at, false};
    else
        pending.push_back({user, at, false});
    BOOST_LOG(info) << "PS5 input: a profile the host signed in lost its last pad; it is signed out in "sv
                    << prospero::sign_out_grace.count() << " s unless a pad joins it again"sv;
}

// Whether one of the host's pads that got its profile from the console's question (no seat of the host's) plays as the
// profile; unknown when the bus doesn't say who one of them plays as. Caller holds input.mutex.
static prospero::Answer unseated_pad_plays_as(const input_raw_t &input, uint32_t user) {
    auto answer = prospero::Answer::no;
    for (int m = 0; m < MAX_GAMEPADS; ++m) {
        if (!input.pads[m] || input.seat[m]) continue;
        uint32_t owner = 0;
        if (ps_profile_owner(ps_pad_device_id(input.pads[m]), &owner) != 0)
            answer = prospero::Answer::unknown;
        else if (owner == user)
            return prospero::Answer::yes;
    }
    return answer;
}

// Whether the title the host started for this profile is still open: the system's running game, or else a process of
// that title, behind the console's screens. Not by the running game alone: one opened before the console's
// ProsperoSunshine app stays open behind it, and once that app is closed it stays open without being the running game
// (process_ps5.cpp, seen on 10.20). Not by /mnt/sandbox either: a title's folder there can keep its contents after the
// title closed (seen on 10.20 after Quit, 2026-11-10), and the profile would then stay signed in for good. Forgets the
// title once it is closed. Caller holds input.mutex.
prospero::Answer launched_open(input_raw_t &input, uint32_t user, bool forget) {
    const auto it = std::find_if(input.launched.begin(), input.launched.end(), [&](const auto &l) { return l.first == user; });
    if (it == input.launched.end()) return prospero::Answer::no;
    char running[16];
    if (ps_title_running(running) > 0 && it->second == running) return prospero::Answer::yes;
    const int open = ps_title_open(it->second.c_str());
    if (open < 0) return prospero::Answer::unknown;
    if (open) return prospero::Answer::yes;
    if (forget) input.launched.erase(it);
    return prospero::Answer::no;
}

// The kept profiles (prospero::HostProfiles) that are no longer signed in, someone signed them out on the console, are
// forgotten: signed in again later, by someone else, they are theirs. Once a second. Caller holds input.mutex.
static void forget_signed_out(input_raw_t &input, std::chrono::steady_clock::time_point now) {
    if (input.host_profiles.kept.empty() || now - input.kept_checked < 1s) return;
    input.kept_checked = now;
    const auto kept = input.host_profiles.kept;
    for (const uint32_t user : kept)
        if (ps_profile_signed_in(user) == 0) {
            input.host_profiles.forget(user);
            BOOST_LOG(info) << "PS5 input: a profile the host kept signed in was signed out on the console"sv;
        }
}

// The profiles whose time came (release_profile) are signed out, if prospero::sign_out says so: not one in front, nor one
// a controller the console asked about plays as, and nothing while the console doesn't say. One in front is kept: it
// stays signed in, and its device may join it again (GitHub issue 52). One that a pad joined again stays the host's.
// Caller holds input.mutex.
void sign_out_due(input_raw_t &input) {
    const auto now = std::chrono::steady_clock::now();
    forget_signed_out(input, now);
    for (auto it = input.sign_out_at.begin(); it != input.sign_out_at.end();) {
        if (now < it->at) {
            ++it;
            continue;
        }
        const uint32_t user = it->user;
        prospero::SignOutFacts facts;
        facts.host_signed_in = input.host_profiles.signed_in_by_host(user);
        for (int m = 0; m < MAX_GAMEPADS && !facts.seated; ++m) facts.seated = input.pads[m] && input.seat[m] == user;
        if (facts.seated) {
            it = input.sign_out_at.erase(it);
            continue;
        }
        uint32_t front = 0;
        facts.in_front = ps_profile_foreground(&front) != 0 ? prospero::Answer::unknown
                         : front == user                 ? prospero::Answer::yes
                                                          : prospero::Answer::no;
        facts.owns_unseated_pad = unseated_pad_plays_as(input, user);
        facts.owns_open_game = launched_open(input, user);
        switch (prospero::sign_out(facts)) {
            case prospero::SignOut::now: {
                const int rc = ps_profile_sign_out(user);
                note_credential_restore();
                BOOST_LOG(info) << "PS5 input: a profile the host signed in was signed out, its last pad left ("sv << rc << ')';
                input.host_profiles.forget(user);
                it = input.sign_out_at.erase(it);
                break;
            }
            case prospero::SignOut::kept:
                BOOST_LOG(info) << "PS5 input: a profile the host signed in stays signed in, it is in front on the console; "
                                   "its device may join it again"sv;
                input.host_profiles.keep(user);
                it = input.sign_out_at.erase(it);
                break;
            case prospero::SignOut::never:
                it = input.sign_out_at.erase(it);
                break;
            case prospero::SignOut::later:
                if (!it->told && facts.owns_open_game == prospero::Answer::yes)
                    BOOST_LOG(info) << "PS5 input: a profile the host signed in waits to be signed out: the game started for it is still open"sv;
                else if (!it->told)
                    BOOST_LOG(info) << "PS5 input: a profile the host signed in waits to be signed out: a controller may play as it, "
                                       "or the console didn't say"sv;
                it->told = true;
                it->at = now + 10s;
                ++it;
                break;
        }
    }
}

// A seated device whose owner the console changed goes back to its profile at once. That is someone answering "Who's
// using this controller?" (or keyboard, or mouse) for it: a bind that comes a millisecond after the login manager decided
// to ask leaves the question up (seen once on 10.20, 2026-11-07), and whoever answers could pick any profile. A device
// seated back more than a few times is left alone, in case the bus reports its owner some other way. Returns whether it
// was seated back. Caller holds input.mutex.
static bool keep_owner(input_raw_t &input, uint64_t device, uint32_t seat) {
    uint32_t owner = 0;
    if (!device || !seat || ps_profile_owner(device, &owner) != 0 || !owner || owner == seat) return false;
    auto it = std::find_if(input.owner_fixes.begin(), input.owner_fixes.end(), [&](const auto &f) { return f.first == device; });
    if (it == input.owner_fixes.end()) it = input.owner_fixes.insert(input.owner_fixes.end(), {device, 0});
    if (it->second >= 5) return false;
    if (++it->second == 5) BOOST_LOG(warning) << "PS5 input: a seated device keeps another owner on the bus; it is left alone"sv;
    const int rc = ps_profile_bind(device, seat);
    BOOST_LOG(warning) << "PS5 input: the console gave a seated device to another profile; it was seated back ("sv << rc << ')';
    return rc == 0;
}

void keep_owners(input_raw_t &input) {
    for (int nr = 0; nr < MAX_GAMEPADS; ++nr) {
        if (!input.pads[nr] || !input.seat[nr] || !keep_owner(input, ps_pad_device_id(input.pads[nr]), input.seat[nr])) continue;
        const auto device = paired_device(input.owner[nr]);
        ps_notify(((device.name.empty() ? "This device"s : device.name) + "'s controller only plays as " +
                   profile_name(input.seat[nr]) + ": it went back to it")
                      .c_str());
    }
    for (const auto &kbm : input.own) {
        keep_owner(input, kbm->keyboard_bus, kbm->user);
        keep_owner(input, kbm->mouse_bus, kbm->user);
    }
    // Bus ids aren't reused soon: forget those of devices gone.
    if (input.owner_fixes.size() > 64) input.owner_fixes.erase(input.owner_fixes.begin(), input.owner_fixes.begin() + 32);
}

// Before a game starts for the paired device with this certificate (process_ps5.cpp): while nobody is in front, the game
// would have no clear owner, or the system refuses to start one (prospero::launch_sign_in). A device with its own profiles
// then gets the first of them its first controller may join (open_pad), signed in now if it isn't, not when that
// controller arrives. The profile is the
// host's like one open_pad signed in: signed out sign_out_grace from now unless a pad sits in it by then, so a start that
// leads to no stream doesn't keep it. Returns the profile it signed in for the game, 0 for none. No profile ids in the log.
uint32_t sign_in_for_launch(const std::string &cert, const std::string &title) {
    const auto device = paired_device(cert);
    const auto set = profiles_of(device.uuid);
    uint32_t front = 0;
    const int front_rc = ps_profile_foreground(&front);
    const auto someone_in_front = front_rc != 0 ? prospero::Answer::unknown : front ? prospero::Answer::yes : prospero::Answer::no;
    switch (prospero::launch_sign_in(set, someone_in_front)) {
        case prospero::LaunchSignIn::no: return 0;
        case prospero::LaunchSignIn::unknown:
            BOOST_LOG(warning) << "PS5 input: the console didn't say who is in front ("sv << front_rc << "); the game starts as before"sv;
            return 0;
        case prospero::LaunchSignIn::yes: break;
    }
    std::lock_guard live(live_input_mutex);
    if (!live_input) return 0;
    auto &input = *live_input;
    std::lock_guard lock(input.mutex);
    for (const uint32_t candidate : prospero::join_order(*set, seats().get(device.uuid, 0))) {
        const auto use = profile_use(input, -1, candidate);
        if (prospero::join_block(use) != prospero::JoinBlock::none) continue;
        if (use.signed_in == prospero::Answer::no) {
            const int rc = ps_profile_sign_in(candidate);
            note_credential_restore();
            if (rc) {
                BOOST_LOG(error) << "PS5 input: signing in a profile of the device starting a game failed: "sv << rc;
                continue;
            }
            input.host_profiles.add(candidate);
            BOOST_LOG(info) << "PS5 input: nobody was in front, so a profile of the device starting a game was signed in first"sv;
        } else {
            // Signed in by the host, or kept: the game makes it the host's again at once, as one signed in for it, so its
            // grace below applies even if no controller of the device ever sits in it.
            input.host_profiles.add(candidate);
            BOOST_LOG(info) << "PS5 input: nobody was in front; a profile of the device starting a game, signed in by the host, gets it"sv;
        }
        // No pad sits in it yet: its grace starts now, and the device's first pad takes it (sign_out_due).
        const auto at = std::chrono::steady_clock::now() + prospero::sign_out_grace;
        std::erase_if(input.launched, [&](const auto &l) { return l.first == candidate; });
        input.launched.emplace_back(candidate, title);
        std::erase_if(input.sign_out_at, [&](const auto &p) { return p.user == candidate; });
        input.sign_out_at.push_back({candidate, at, false});
        BOOST_LOG(info) << "PS5 input: the game starts for that profile; it is signed out in "sv << prospero::sign_out_grace.count()
                        << " s unless a pad of the device joins it"sv;
        return candidate;
    }
    BOOST_LOG(warning) << "PS5 input: nobody is in front and no profile of the device starting a game could be signed in"sv;
    return 0;
}

// A game the host started for a profile it signed in (sign_in_for_launch) may run behind the console's welcome screen
// (prospero::welcome_step): one X on a pad of the device closes it, held for 200 ms. It is looked at each time a pad sits
// in such a profile, from its seating on: a client's controller may come long after the game started (Moonlight on iOS
// adds it at the first touch), or come back to a game left open. Looked at every 250 ms. Caller holds input.mutex.
void close_welcome(input_raw_t &input) {
    const auto now = std::chrono::steady_clock::now();
    if (input.welcome_pressed >= 0) {
        if (now < input.welcome_release) return;
        if (input.pads[input.welcome_pressed]) ps_pad_neutral(input.pads[input.welcome_pressed]);
        input.welcome_pressed = -1;
    }
    if (now < input.welcome_check) return;
    input.welcome_check = now + 250ms;
    for (int nr = 0; nr < MAX_GAMEPADS; ++nr) {
        auto &welcome = input.welcome[nr];
        if (!input.pads[nr] || !input.seat[nr]) {
            welcome = {};
            continue;
        }
        if (welcome.seat != input.seat[nr]) welcome = {input.seat[nr], now, false};
        if (welcome.done) continue;
        // Only a profile the host started a game for; it waits while that game isn't open yet.
        if (std::none_of(input.launched.begin(), input.launched.end(), [&](const auto &l) { return l.first == welcome.seat; })) {
            welcome.done = true;
            continue;
        }
        if (launched_open(input, welcome.seat, false) != prospero::Answer::yes && now - welcome.since < prospero::welcome_window)
            continue;
        if (now - welcome.since >= prospero::welcome_window) {
            BOOST_LOG(info) << "PS5 virtual pad "sv << nr << ": the game started for its profile didn't come to the front; nothing was pressed"sv;
            welcome.done = true;
            continue;
        }
        char running[16];
        const int run = ps_title_running(running);
        const int focused = run > 0 ? ps_title_focused() : -1;
        uint32_t front = 0;
        const bool front_known = ps_profile_foreground(&front) == 0;
        using prospero::Answer;
        const prospero::WelcomeFacts facts{now - welcome.since >= prospero::welcome_settle,
                                           run > 0 ? Answer::yes : run == 0 ? Answer::no : Answer::unknown,
                                           focused < 0 ? Answer::unknown : focused ? Answer::yes : Answer::no,
                                           !front_known ? Answer::unknown : front ? Answer::yes : Answer::no};
        switch (prospero::welcome_step(facts)) {
            case prospero::WelcomeStep::wait: continue;
            case prospero::WelcomeStep::done: welcome.done = true; continue;
            case prospero::WelcomeStep::press: {
                const ps_pad_state down{PS_PAD_CROSS, 128, 128, 128, 128, 0, 0};
                ps_pad_send(input.pads[nr], &down);
                input.welcome_pressed = nr;
                input.welcome_release = now + 200ms;
                welcome.done = true;
                BOOST_LOG(info) << "PS5 virtual pad "sv << nr
                                << ": the console's welcome screen covers the game started for its profile; it pressed X to close it"sv;
                return;
            }
        }
    }
}

// Caller holds input.mutex. No device names or profile ids in the log: it goes into the report people post.
pad_open add_pad(input_raw_t &input, int nr, bool host) {
    auto *&pad = input.pads[nr];
    input.seat[nr] = 0;
    input.menu_checked[nr] = {};  // its menus go by the profile it gets
    input.held_back[nr] = false;
    const auto device = paired_device(input.owner[nr]);
    const auto plan = seat_plan(input, nr, device.uuid);
    const std::string who = device.name.empty() ? "This device"s : device.name;
    // A pad that doesn't join watches: a press tries again at most once a second, and the console says why at most once
    // every switch_cooldown, not at every try. A device without any profile was told so when its stream started
    // (greet_spectator).
    const auto tell = [&](std::string_view reason, const std::string &text) {
        if (const auto now = std::chrono::steady_clock::now(); now - input.watch_told[nr] >= prospero::switch_cooldown) {
            input.watch_told[nr] = now;
            BOOST_LOG(info) << "PS5 virtual pad "sv << nr << " not added: "sv << reason;
            if (!text.empty()) ps_notify(text.c_str());
        }
    };
    if (plan.kind == seat_plan_t::none) {
        tell("its device has no profile, it may only watch"sv, {});
        return pad_open::watching;
    }
    if (plan.kind == seat_plan_t::profile) {
        // The first of the device's profiles the pad may join (prospero::join_block), signed in if it isn't. Signing one in
        // fails when the console already has its four sessions (0x80960101 on 10.20).
        uint32_t user = 0, full = 0, elsewhere = 0;
        bool unknown = false;
        for (const uint32_t candidate : plan.users) {
            const auto use = profile_use(input, nr, candidate);
            const auto block = prospero::join_block(use);
            if (block == prospero::JoinBlock::unknown) unknown = true;
            if (block == prospero::JoinBlock::signed_in_elsewhere && !elsewhere) elsewhere = candidate;
            if (block != prospero::JoinBlock::none) continue;
            if (use.signed_in == prospero::Answer::no) {
                const int rc = ps_profile_sign_in(candidate);
                note_credential_restore();
                if (rc) {
                    BOOST_LOG(error) << "PS5 virtual pad "sv << nr << ": signing a profile of its device in failed: "sv << rc;
                    if (!full) full = candidate;
                    continue;
                }
                BOOST_LOG(info) << "PS5 virtual pad "sv << nr << ": its profile was signed in"sv;
                input.host_profiles.add(candidate);
            }
            user = candidate;
            break;
        }
        if (!user) {
            if (full)
                tell("a profile of its device couldn't be signed in"sv,
                     who + "'s controller can't join: " + profile_name(full) +
                         " couldn't be signed in (the console takes four at most). Press a button to try again");
            else if (elsewhere)
                tell("its device's profile is signed in on the console by someone else"sv,
                     who + " can't play as " + profile_name(elsewhere) +
                         " while it is signed in on the console. Sign it out there, then press a button");
            else if (unknown)
                tell("the console didn't say whether its device's profiles are signed in"sv,
                     who + "'s controller can't join: the console didn't answer. Press a button to try again");
            else
                tell("every profile of its device is taken"sv, who + " has no profile left for another controller");
            return pad_open::watching;
        }
        settle_bus();
        if (const int rc = ps_pad_open(user, &pad); rc) {
            BOOST_LOG(error) << "PS5 virtual pad "sv << nr << " failed: "sv << rc;
            pad = nullptr;
            release_profile(input, user);
            return pad_open::failed;
        }
        // At once: the console decides whether to ask about a new pad a few milliseconds after it is added.
        const auto bind_start = std::chrono::steady_clock::now();
        const int bind_rc = ps_profile_bind(ps_pad_device_id(pad), user);
        const auto bind_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - bind_start).count();
        if (bind_rc) {
            // Left open, the pad would get the console's question, which a limited device must not: it goes again.
            BOOST_LOG(warning) << "PS5 virtual pad "sv << nr << ": seating it in its profile failed ("sv << bind_rc << " after "sv
                               << bind_ms << " ms), so it was not added"sv;
            ps_pad_close(pad);
            removed_device();
            pad = nullptr;
            release_profile(input, user);
            // It watches until a press tries again (at most once a second), and hears why at most once every
            // switch_cooldown: right after the host restarts, every try fails until libSceMbus is up.
            if (const auto now = std::chrono::steady_clock::now(); now - input.watch_told[nr] >= prospero::switch_cooldown) {
                input.watch_told[nr] = now;
                ps_notify((who + "'s controller couldn't join as " + profile_name(user) + ". Press a button to try again").c_str());
            }
            return pad_open::watching;
        }
        input.seat[nr] = user;
        input.host_profiles.seat(user);  // a kept profile is the host's again
        BOOST_LOG(info) << "PS5 virtual pad "sv << nr << " attached in its device's profile"sv << (host ? " as the host's controller"sv : ""sv)
                        << " (bound after "sv << bind_ms << " ms)"sv;
        // With more profiles to switch to, the notification tells how, so nobody has to have read the web page's help.
        const auto set = profiles_of(device.uuid);
        const bool can_switch = set && set->users.size() > 1;
        ps_notify((profile_name(user) + " joined the party" +
                   (can_switch ? ". To switch profile: Ctrl+Backspace, or hold L1+L2+R1+R2" : ""))
                      .c_str());
        return pad_open::opened;
    }
    const int rc = ps_pad_open(host && plan.kind == seat_plan_t::ask ? input.user : PS_PAD_NO_USER, &pad);
    if (rc) {
        BOOST_LOG(error) << "PS5 virtual pad "sv << nr << " failed: "sv << rc;
        pad = nullptr;
        return pad_open::failed;
    }
    BOOST_LOG(info) << "PS5 virtual pad "sv << nr << " attached"sv
                    << (host ? " as the host's controller"sv
                             : input.roles.host_away() ? ", the console asks who uses it; the host is away and keeps the role"sv
                                                       : ", the console asks who uses it"sv);
    return pad_open::opened;
}

// Moves a seated pad to its device's next free profile, signing it in when needed: the pad is added anew in it, which a
// game sees as one player's controller leaving and the next one's coming. One notification says both. The choice is
// kept for this controller of the device (DeviceSeats). Returns whether it switched; round is how many profiles the device
// switches between. Caller holds input.mutex.
static bool switch_seat(input_raw_t &input, int nr, std::size_t &round) {
    const uint32_t current = input.seat[nr];
    const auto device = paired_device(input.owner[nr]);
    const std::string who = device.name.empty() ? "This device"s : device.name;
    const auto set = profiles_of(device.uuid);
    if (!input.pads[nr] || !current || !set || set->free) {
        BOOST_LOG(info) << "PS5 virtual pad "sv << nr << ": a profile switch was held, but its device's profiles aren't set"sv;
        ps_notify((who + " has no profiles to switch between: set them on the web page").c_str());
        return false;
    }
    round = set->users.size();
    // Only to a profile it may join: not one another pad holds, nor one someone else signed in (prospero::join_block).
    const uint32_t next = prospero::next_seat(
        *set, current, [&](uint32_t user) { return prospero::join_block(profile_use(input, nr, user)) != prospero::JoinBlock::none; });
    if (!next) {
        BOOST_LOG(info) << "PS5 virtual pad "sv << nr << ": a profile switch was held, but no other profile is free"sv;
        ps_notify((set->users.size() > 1 ? "No other profile of " + who + " is free" : who + " has only one profile").c_str());
        return false;
    }
    const int signed_in = ps_profile_signed_in(next);
    if (signed_in < 0) {
        BOOST_LOG(warning) << "PS5 virtual pad "sv << nr << ": the console didn't say whether the profile to switch to is signed in"sv;
        ps_notify(("The console didn't answer: " + who + " stays as " + profile_name(current)).c_str());
        return false;
    }
    if (!signed_in) {
        const int rc = ps_profile_sign_in(next);
        note_credential_restore();
        if (rc) {
            BOOST_LOG(error) << "PS5 virtual pad "sv << nr << ": signing in the profile to switch to failed: "sv << rc;
            ps_notify((profile_name(next) + " couldn't be signed in for " + who).c_str());
            return false;
        }
        input.host_profiles.add(next);
    }
    ps_pad_close(input.pads[nr]);
    removed_device();
    input.pads[nr] = nullptr;
    input.seat[nr] = 0;
    input.colors[nr] = {};
    input.triggers[nr] = {};
    input.touch[nr].clear();
    uint32_t seated = next;
    settle_bus();
    if (ps_pad_open(next, &input.pads[nr]) != 0 || ps_profile_bind(ps_pad_device_id(input.pads[nr]), next) != 0) {
        BOOST_LOG(error) << "PS5 virtual pad "sv << nr << ": the switch failed, back to its profile"sv;
        if (input.pads[nr]) {
            ps_pad_close(input.pads[nr]);
            removed_device();
        }
        input.pads[nr] = nullptr;
        seated = current;
        settle_bus();
        if (ps_pad_open(current, &input.pads[nr]) != 0 || ps_profile_bind(ps_pad_device_id(input.pads[nr]), current) != 0) {
            if (input.pads[nr]) {
                ps_pad_close(input.pads[nr]);
                removed_device();
            }
            input.pads[nr] = nullptr;
            input.roles.detach(nr);
            release_profile(input, next);
            release_profile(input, current);
            ps_notify((who + "'s controller was lost switching profiles").c_str());
            return false;
        }
    }
    input.seat[nr] = seated;
    input.host_profiles.seat(seated);
    input.menu_checked[nr] = {};  // the new profile may reach the console's menus, or not
    input.held_back[nr] = false;
    request_motion(input, nr);
    if (seated != next) {
        release_profile(input, next);  // signed in for the switch that didn't happen
        return false;
    }
    seats().remember(device.uuid, input.client_index[nr], next);
    BOOST_LOG(info) << "PS5 virtual pad "sv << nr << " switched to its device's next profile"sv;
    ps_notify((profile_name(current) + " passed the controller to " + profile_name(next)).c_str());
    release_profile(input, current);
    return true;
}

// A switch fires once a pad has held it for switch_hold; it fires again only after letting go. Caller holds input.mutex.
void check_seat_switches(input_raw_t &input) {
    const auto now = std::chrono::steady_clock::now();
    for (int nr = 0; nr < MAX_GAMEPADS; ++nr) {
        if (input.switch_done[nr] || input.switch_since[nr].time_since_epoch().count() == 0) continue;
        if (now - input.switch_since[nr] < prospero::switch_hold) continue;
        input.switch_done[nr] = true;
        // While it waits, the player is told how long once; later switches in the same wait are only ignored.
        if (auto &budget = input.switch_budget[nr]; !budget.allowed(now)) {
            BOOST_LOG(info) << "PS5 virtual pad "sv << nr << ": a profile switch came while it waits after a round of switches, ignored"sv;
            if (budget.tell_once())
                ps_notify(("Wait " + std::to_string(budget.seconds_left(now)) + " s to switch profile again").c_str());
            continue;
        }
        std::size_t round = 0;
        if (switch_seat(input, nr, round)) input.switch_budget[nr].switched(now, round);
        else input.switch_budget[nr].refused(now);
    }
}

// A paired device's profiles changed: each of its controllers sitting in a profile it no longer has leaves it, and waits
// as one that only watches until a press joins it as a profile the device still has. The stream goes on. More profiles,
// or a free choice, take nothing away. A pad the console's question seated (its device could choose freely) leaves too
// when the device gets a list, as which profile it sits in isn't known.
void profiles_changed(const std::string &cert, const std::vector<uint32_t> &added) {
    if (cert.empty()) return;
    std::lock_guard live(live_input_mutex);
    if (!live_input) return;
    auto &input = *live_input;
    std::lock_guard lock(input.mutex);
    const auto device = paired_device(cert);
    const auto set = profiles_of(device.uuid);
    if (!set || set->free) return;
    const std::string who = device.name.empty() ? "This device"s : device.name;
    bool benched = false;
    for (int nr = 0; nr < MAX_GAMEPADS; ++nr) {
        if (!input.pads[nr] || input.owner[nr] != cert) continue;
        const uint32_t seated = input.seat[nr];
        if (seated && std::find(set->users.begin(), set->users.end(), seated) != set->users.end()) continue;
        set_motors(input, nr, 0, 0);
        set_triggers(input, nr, prospero::triggers_off);
        ps_pad_close(input.pads[nr]);  // sends a neutral state before removal
        removed_device();
        input.pads[nr] = nullptr;
        input.seat[nr] = 0;
        input.roles.detach(nr);
        input.colors[nr] = {};
        input.triggers[nr] = {};
        input.touch[nr].clear();
        input.back[nr] = input.ps_held[nr] = input.held_back[nr] = false;
        input.menu_checked[nr] = {};
        input.switch_since[nr] = {};
        input.switch_done[nr] = false;
        input.waiting[nr] = input.spectating[nr] = true;
        input.watch_tried[nr] = input.watch_told[nr] = {};
        const bool other = std::any_of(set->users.begin(), set->users.end(), [&](uint32_t user) { return !seat_taken(input, nr, user); });
        BOOST_LOG(info) << "PS5 virtual pad "sv << nr << " left the profile its device no longer has; it watches until a press"sv
                        << (other ? " joins it as another"sv : ""sv);
        ps_notify(((seated ? who + " can no longer play as " + profile_name(seated) : who + "'s profiles changed") +
                   (other ? ". Press a button to join as another of its profiles" : ": it watches now"))
                      .c_str());
        release_profile(input, seated);
        benched = true;
    }
    // A device streaming now hears at once about profiles it got, not only at its next press: a spectator that it may
    // join, a player that it may switch. A controller just taken out of a profile was told already. The caller passes
    // added profiles only while the device streams (a session's input outlives it for a while).
    if (added.empty() || benched) return;
    bool playing = false;
    for (int nr = 0; nr < MAX_GAMEPADS; ++nr) playing = playing || (input.pads[nr] && input.owner[nr] == cert);
    std::string names;
    for (const uint32_t user : added) names += (names.empty() ? "" : ", ") + profile_name(user);
    BOOST_LOG(info) << "PS5 input: a streaming device got "sv << added.size() << " profile(s)"sv << (playing ? ", it plays"sv : ", it watches"sv);
    ps_notify((playing ? who + " can now also play as " + names + ". To switch profile: Ctrl+Backspace, or hold L1+L2+R1+R2"
                       : who + " can now play as " + names + ". Press a button to join")
                  .c_str());
}
}
