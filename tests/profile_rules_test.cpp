// SPDX-License-Identifier: GPL-3.0-only
#include "input/profile_rules.hpp"
#include <cassert>
using namespace prospero;

int main() {
    // Joining: a profile one of the host's controllers holds, of any device, is taken.
    assert(join_block({true, Answer::no, false}) == JoinBlock::held);
    assert(join_block({true, Answer::yes, true}) == JoinBlock::held);
    // Not signed in: the host signs it in.
    assert(join_block({false, Answer::no, false}) == JoinBlock::none);
    // Signed in by the host for a controller that left: free again.
    assert(join_block({false, Answer::yes, true}) == JoinBlock::none);
    // Signed in by someone else: a controller in the room may play as it.
    assert(join_block({false, Answer::yes, false}) == JoinBlock::signed_in_elsewhere);
    // The console didn't say.
    assert(join_block({false, Answer::unknown, false}) == JoinBlock::unknown);
    assert(join_block({false, Answer::unknown, true}) == JoinBlock::unknown);

    const auto set = *parse_profiles("1b1d9381,1b1d9382,1b1d9383");
    assert((join_order(set, 0) == std::vector<uint32_t>{0x1b1d9381, 0x1b1d9382, 0x1b1d9383}));
    assert((join_order(set, 0x1b1d9382) == std::vector<uint32_t>{0x1b1d9382, 0x1b1d9381, 0x1b1d9383}));
    // A last profile the device no longer has doesn't come first.
    assert((join_order(set, 0x1b1d9389) == std::vector<uint32_t>{0x1b1d9381, 0x1b1d9382, 0x1b1d9383}));
    assert(join_order(*parse_profiles("none"), 0x1b1d9381).empty());

    // Starting a game while nobody is in front: only a device with profiles of its own starts it for one of them.
    assert(launch_sign_in(set, Answer::no) == LaunchSignIn::yes);
    assert(launch_sign_in(*parse_profiles("1b1d9381,guest"), Answer::no) == LaunchSignIn::yes);
    // Someone is in front: the game starts for them, as before.
    assert(launch_sign_in(set, Answer::yes) == LaunchSignIn::no);
    // Not on a guess.
    assert(launch_sign_in(set, Answer::unknown) == LaunchSignIn::unknown);
    // Free, not set, only watching, or only the Quick Play guest: nothing changes.
    assert(launch_sign_in(*parse_profiles("free"), Answer::no) == LaunchSignIn::no);
    assert(launch_sign_in(std::nullopt, Answer::no) == LaunchSignIn::no);
    assert(launch_sign_in(*parse_profiles("none"), Answer::no) == LaunchSignIn::no);
    assert(launch_sign_in(*parse_profiles("guest"), Answer::no) == LaunchSignIn::no);

    // The welcome screen over that game: X only once the pad has settled, the game runs without the screen and nobody is
    // in front.
    using A = Answer;
    assert(welcome_step({true, A::yes, A::no, A::no}) == WelcomeStep::press);
    assert(welcome_step({false, A::yes, A::no, A::no}) == WelcomeStep::wait);
    assert(welcome_step({true, A::no, A::no, A::no}) == WelcomeStep::wait);
    assert(welcome_step({true, A::unknown, A::no, A::no}) == WelcomeStep::wait);
    assert(welcome_step({true, A::yes, A::unknown, A::no}) == WelcomeStep::wait);
    assert(welcome_step({true, A::yes, A::no, A::unknown}) == WelcomeStep::wait);
    // The game has the screen, or someone is in front: nothing to press.
    assert(welcome_step({true, A::yes, A::yes, A::no}) == WelcomeStep::done);
    assert(welcome_step({false, A::unknown, A::unknown, A::yes}) == WelcomeStep::done);
    static_assert(welcome_window >= std::chrono::seconds(20) && welcome_settle >= std::chrono::milliseconds(500));
    // The greeting: only for a device streaming alone while nobody is in front.
    assert(welcome_greeting(true, A::no));
    assert(!welcome_greeting(true, A::yes));
    assert(!welcome_greeting(true, A::unknown));
    assert(!welcome_greeting(false, A::no));

    // Signing out: never someone else's, nor one a controller of the host sits in.
    assert(sign_out({false, false, Answer::no, Answer::no}) == SignOut::never);
    assert(sign_out({true, true, Answer::no, Answer::no}) == SignOut::never);
    // The profile in front stays signed in, kept for its device (issue 52) and for whoever sits at the TV.
    assert(sign_out({true, false, Answer::yes, Answer::no}) == SignOut::kept);
    assert(sign_out({true, false, Answer::yes, Answer::yes}) == SignOut::kept);
    // Not on a guess: the console didn't say what is in front, or who a controller the console asked about plays as.
    assert(sign_out({true, false, Answer::unknown, Answer::no}) == SignOut::later);
    assert(sign_out({true, false, Answer::no, Answer::unknown}) == SignOut::later);
    // Such a controller plays as it.
    assert(sign_out({true, false, Answer::no, Answer::yes}) == SignOut::later);
    assert(sign_out({true, false, Answer::no, Answer::no}) == SignOut::now);
    // The game the host started for it is still open: it waits, and goes once the game is closed.
    assert(sign_out({true, false, Answer::no, Answer::no, Answer::yes}) == SignOut::later);
    assert(sign_out({true, false, Answer::no, Answer::no, Answer::unknown}) == SignOut::later);
    assert(sign_out({true, false, Answer::no, Answer::no, Answer::no}) == SignOut::now);
    // In front or seated still decides first.
    assert(sign_out({true, false, Answer::yes, Answer::no, Answer::yes}) == SignOut::kept);
    assert(sign_out({true, true, Answer::no, Answer::no, Answer::yes}) == SignOut::never);
    static_assert(sign_out_grace >= std::chrono::seconds(30) && sign_out_grace <= std::chrono::seconds(60));

    // The host's profiles (issue 52): one kept as it was in front can be joined again, but isn't signed out by the host.
    {
        HostProfiles host;
        host.add(0x1b1d9381);
        host.add(0x1b1d9382);
        host.add(0x1b1d9381);
        assert((host.signed_in == std::vector<uint32_t>{0x1b1d9381, 0x1b1d9382}));
        assert(host.knows(0x1b1d9381) && host.signed_in_by_host(0x1b1d9381) && !host.knows(0x1b1d9383));
        host.keep(0x1b1d9381);
        assert((host.signed_in == std::vector<uint32_t>{0x1b1d9382}));
        assert((host.kept == std::vector<uint32_t>{0x1b1d9381}));
        assert(host.knows(0x1b1d9381) && !host.signed_in_by_host(0x1b1d9381));
        // Its device comes back: free to join, as a profile the host signed in.
        assert(join_block({false, Answer::yes, host.knows(0x1b1d9381)}) == JoinBlock::none);
        // Keeping one the host didn't sign in, or one already kept, changes nothing.
        host.keep(0x1b1d9383);
        host.keep(0x1b1d9381);
        assert((host.kept == std::vector<uint32_t>{0x1b1d9381}) && !host.knows(0x1b1d9383));
        // A controller sits in it: the host's again, signed out after it leaves.
        host.seat(0x1b1d9381);
        assert(host.kept.empty() && host.signed_in_by_host(0x1b1d9381));
        // Seating one that isn't kept changes nothing.
        host.seat(0x1b1d9383);
        assert(!host.knows(0x1b1d9383));
        // Signed out on the console: forgotten, so someone else signing it in later blocks the device again.
        host.keep(0x1b1d9382);
        host.forget(0x1b1d9382);
        assert(!host.knows(0x1b1d9382));
        assert(join_block({false, Answer::yes, host.knows(0x1b1d9382)}) == JoinBlock::signed_in_elsewhere);
        // Signed in anew by the host while kept: the host's again.
        host.keep(0x1b1d9381);
        host.add(0x1b1d9381);
        assert(host.kept.empty() && host.signed_in_by_host(0x1b1d9381));
    }

    // Keyboard and mouse: without a stream behind it, only a release, and only on the shared devices.
    assert(kbm_route(false, false, false, false) == KbmRoute::none);
    assert(kbm_route(false, false, true, true) == KbmRoute::none);
    assert(kbm_route(false, true, true, true) == KbmRoute::shared);
    // A device that can choose any profile: the shared devices.
    assert(kbm_route(true, false, false, false) == KbmRoute::shared);
    assert(kbm_route(true, true, false, false) == KbmRoute::shared);
    // A device with its own profiles: its own devices while a controller of it plays, else nothing.
    assert(kbm_route(true, false, true, true) == KbmRoute::own);
    assert(kbm_route(true, true, true, true) == KbmRoute::own);
    assert(kbm_route(true, false, true, false) == KbmRoute::none);
    assert(kbm_route(true, true, true, false) == KbmRoute::none);
    return 0;
}
