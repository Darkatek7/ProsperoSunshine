// SPDX-License-Identifier: GPL-3.0-only
// What the PS5 platform (platform_ps5.cpp) decides for a paired device with its own console profiles, kept apart from the
// system calls so that the tests can run it: which profile a new controller may join, whether a profile the host signed
// in may be signed out, and which keyboard and mouse a key or button goes to. The platform asks the console and passes the
// answers in.
#pragma once

#include "control/device_profiles.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

namespace prospero {

// The console's answer to a yes-or-no question. The system can fail to answer, and then it counts neither way: nothing is
// signed in or out on a guess.
enum class Answer { no, yes, unknown };

// --- Joining a profile ---

// What the host knows of a profile when a controller of a device with its own profiles looks for one to join.
struct ProfileUse {
    bool held_by_host = false;           // one of the host's controllers sits in it, of this device or another
    Answer signed_in = Answer::unknown;  // on the console
    bool host_signed_in = false;         // the host signed it in itself, so it knows who plays as it
};

// Why a controller can't join a profile now; none when it can.
enum class JoinBlock { none, held, signed_in_elsewhere, unknown };

// A profile someone else signed in may have a controller in the room, and binding another controller to it takes that one
// away. While the console doesn't say whether it is signed in, nothing is risked.
inline JoinBlock join_block(const ProfileUse &use) {
    if (use.held_by_host) return JoinBlock::held;
    if (use.signed_in == Answer::unknown) return JoinBlock::unknown;
    if (use.signed_in == Answer::yes && !use.host_signed_in) return JoinBlock::signed_in_elsewhere;
    return JoinBlock::none;
}

// The device's profiles a new controller tries, in order: the one it last switched to (while the device still has it),
// then the device's list.
inline std::vector<uint32_t> join_order(const ProfileSet &set, uint32_t last) {
    std::vector<uint32_t> order;
    if (last && std::find(set.users.begin(), set.users.end(), last) != set.users.end()) order.push_back(last);
    for (const uint32_t user : set.users)
        if (user != last) order.push_back(user);
    return order;
}

// --- Signing out ---

// How long a profile the host signed in stays signed in after its last controller left: a Moonlight client that
// reconnects within it finds the profile, and the game it was playing, as they were.
constexpr std::chrono::seconds sign_out_grace{45};

// What the host knows of a profile it may sign out.
struct SignOutFacts {
    bool host_signed_in = false;                 // the host signed it in; it never signs out anyone else's
    bool seated = false;                         // one of the host's controllers sits in it
    Answer in_front = Answer::unknown;           // it is the profile in front on the console
    Answer owns_unseated_pad = Answer::unknown;  // a controller of the host that the console asked about belongs to it
};

// now: sign it out. later: ask again in a while. never: it stays signed in, and the host forgets it signed it in.
enum class SignOut { now, later, never };

inline SignOut sign_out(const SignOutFacts &facts) {
    if (!facts.host_signed_in || facts.seated) return SignOut::never;
    // Someone at the TV uses the profile in front: it is theirs from now on.
    if (facts.in_front == Answer::yes) return SignOut::never;
    // A controller that got the profile from the console's question still plays as it, or the console didn't say.
    if (facts.in_front == Answer::unknown || facts.owns_unseated_pad != Answer::no) return SignOut::later;
    return SignOut::now;
}

// --- Keyboard and mouse ---

// Where a key or a mouse button goes: the console's shared keyboard and mouse, the device's own (seated in its profile),
// or nowhere.
enum class KbmRoute { shared, own, none };

// from_session: the input comes from a stream (patch 0022 says whose). Without one it is Sunshine letting go of what it
// held when a stream ended: that may only release, and only on the shared devices, as the device's own ones go with it.
// A device that can choose any profile uses the shared devices. A device with its own profiles uses its own, seated where
// its first controller plays, and nothing while no controller of it plays.
inline KbmRoute kbm_route(bool from_session, bool release, bool limited, bool seated) {
    if (!from_session) return release ? KbmRoute::shared : KbmRoute::none;
    if (!limited) return KbmRoute::shared;
    return seated ? KbmRoute::own : KbmRoute::none;
}

}  // namespace prospero
