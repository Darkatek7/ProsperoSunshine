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

    // Signing out: never someone else's, nor one a controller of the host sits in.
    assert(sign_out({false, false, Answer::no, Answer::no}) == SignOut::never);
    assert(sign_out({true, true, Answer::no, Answer::no}) == SignOut::never);
    // The profile in front stays, for whoever sits at the TV.
    assert(sign_out({true, false, Answer::yes, Answer::no}) == SignOut::never);
    // Not on a guess: the console didn't say what is in front, or who a controller the console asked about plays as.
    assert(sign_out({true, false, Answer::unknown, Answer::no}) == SignOut::later);
    assert(sign_out({true, false, Answer::no, Answer::unknown}) == SignOut::later);
    // Such a controller plays as it.
    assert(sign_out({true, false, Answer::no, Answer::yes}) == SignOut::later);
    assert(sign_out({true, false, Answer::no, Answer::no}) == SignOut::now);
    static_assert(sign_out_grace >= std::chrono::seconds(30) && sign_out_grace <= std::chrono::seconds(60));

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
