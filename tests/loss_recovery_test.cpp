// SPDX-License-Identifier: GPL-3.0-only
#include "video/loss_recovery.hpp"
#include <cassert>
#include <iostream>
#include <vector>
using namespace prospero;

namespace {
using Streams = std::vector<LossRecovery::Stream>;
using Loss = LossRecovery::Loss;
const LossRecovery::Stream a = 1, b = 2;

hevc::Refs from(std::vector<int> used, std::vector<int> kept = {}) {
    hevc::Refs refs;
    refs.used = std::move(used);
    refs.kept = std::move(kept);
    return refs;
}
// A stream that got the encoder's frames up to `last` under numbers `offset` lower, from the encoder's frame `first` on.
void send(LossRecovery &recovery, LossRecovery::Stream stream, uint32_t last, int offset = 0, uint32_t first = 1) {
    for (uint32_t counter = std::max(first, static_cast<uint32_t>(offset) + 1); counter <= last; ++counter)
        recovery.sent(stream, static_cast<int64_t>(counter) - offset, counter);
}
}  // namespace

int main() {
    // Without a usable layout every loss needs a keyframe, asked for once.
    {
        LossRecovery recovery;
        send(recovery, a, 100);
        assert(!recovery.usable() && recovery.lost(a, 100, 100) == Loss::keyframe && !recovery.waiting());
        assert(!recovery.call(100).invalidate && recovery.call(100).keyframe.empty());
        assert(recovery.lost(a, 100, 101) == Loss::answered);  // the client names it again while the keyframe is on its way
        recovery.frame(103, true, std::nullopt);
        send(recovery, a, 105, 0, 101);
        assert(recovery.lost(a, 100, 102) == Loss::answered);  // sent before it could have the keyframe
        assert(recovery.lost(a, 100, 103) == Loss::keyframe);  // the keyframe was lost as well
        recovery.frame(106, true, std::nullopt);
        send(recovery, a, 110, 0, 106);
        assert(recovery.lost(a, 108, 108) == Loss::keyframe);  // a new loss
    }
    // The console's case: frame 100 lost, the encoder is told, frame 101 comes from 99 and the stream goes on from it.
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        assert(recovery.lost(a, 100, 100) == Loss::told && recovery.waiting());
        auto call = recovery.call(100);
        assert(call.invalidate == 100u && call.keyframe.empty());
        assert(!recovery.call(100).invalidate);  // once per loss
        const auto verdict = recovery.frame(101, false, from({-2}, {-1, -3}));
        assert(verdict.recovered == Streams{a} && verdict.keyframe.empty() && !recovery.waiting());
        assert(recovery.frame(102, false, from({-1})).recovered.empty());
    }
    // Three frames lost, as measured: 198 to 200, frame 201 comes from 197.
    {
        LossRecovery recovery(7);
        send(recovery, a, 200);
        assert(recovery.lost(a, 198, 200) == Loss::told);
        assert(recovery.call(200).invalidate == 198u);
        assert(recovery.frame(201, false, from({-4})).recovered == Streams{a});
    }
    // The client names the loss again for every frame it has to drop: no second call while it waits, and what it sent
    // before it could have the marked frame is answered by that frame.
    {
        LossRecovery recovery(7);
        send(recovery, a, 101);
        assert(recovery.lost(a, 100, 100) == Loss::told);
        assert(recovery.call(101).invalidate == 100u);
        assert(recovery.lost(a, 100, 101) == Loss::told);
        assert(!recovery.call(101).invalidate);
        assert(recovery.frame(102, false, from({-3})).recovered == Streams{a});
        send(recovery, a, 103, 0, 102);
        assert(recovery.lost(a, 100, 101) == Loss::answered && !recovery.waiting());
        // The marked frame was lost too: the encoder is told again, the same first frame.
        assert(recovery.lost(a, 100, 102) == Loss::told);
        assert(recovery.call(103).invalidate == 100u);
        assert(recovery.frame(104, false, from({-5})).recovered == Streams{a});
        // A loss after it is a new one.
        send(recovery, a, 106, 0, 104);
        assert(recovery.lost(a, 105, 105) == Loss::told);
        assert(recovery.call(106).invalidate == 105u);
        assert(recovery.frame(107, false, from({-3})).recovered == Streams{a});
    }
    // A frame that was already being encoded still hangs on the old chain: the next one is the one.
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        recovery.lost(a, 100, 100);
        recovery.call(100);
        auto verdict = recovery.frame(101, false, from({-1}));
        assert(verdict.recovered.empty() && verdict.keyframe.empty() && recovery.waiting());
        verdict = recovery.frame(102, false, from({-3}));
        assert(verdict.recovered == Streams{a} && !recovery.waiting());
    }
    // The encoder never leaves the old chain: a keyframe after three frames, and the client's next requests wait for it.
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        recovery.lost(a, 100, 100);
        recovery.call(100);
        assert(recovery.frame(101, false, from({-1})).keyframe.empty());
        assert(recovery.frame(102, false, from({-1})).keyframe.empty());
        const auto verdict = recovery.frame(103, false, from({-1}));
        assert(verdict.keyframe == Streams{a} && verdict.recovered.empty() && !recovery.waiting());
        send(recovery, a, 103, 0, 101);
        assert(recovery.lost(a, 100, 103) == Loss::answered);
        recovery.keyframe_dropped();  // the encoder refused the keyframe: the next request starts anew
        assert(recovery.lost(a, 100, 103) == Loss::told);
    }
    // More than seven pictures missing puts the next one in the wrong place: eight lost is a keyframe at once, seven
    // goes through when the very next frame is the one, and not when one more has to be dropped.
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        assert(recovery.lost(a, 93, 100) == Loss::told);
        const auto call = recovery.call(100);
        assert(!call.invalidate && call.keyframe == Streams{a} && !recovery.waiting());
    }
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        recovery.lost(a, 94, 100);
        assert(recovery.call(100).invalidate == 94u);
        assert(recovery.frame(101, false, from({-8})).recovered == Streams{a});
    }
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        recovery.lost(a, 94, 100);
        recovery.call(100);
        assert(recovery.frame(101, false, from({-1})).keyframe == Streams{a});
    }
    // A stream that joined later counts its frames from its own start: its frame 50 is the encoder's 100.
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        send(recovery, b, 100, 50);
        assert(recovery.lost(b, 50, 50) == Loss::told);
        assert(recovery.call(100).invalidate == 100u);
        assert(recovery.frame(101, false, from({-2})).recovered == Streams{b});
    }
    // Two streams wait: the encoder is told the older loss, and a frame from before it serves both.
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        send(recovery, b, 100, 50);
        assert(recovery.lost(b, 50, 50) == Loss::told && recovery.lost(a, 98, 99) == Loss::told);
        assert(recovery.call(100).invalidate == 98u);
        const auto verdict = recovery.frame(101, false, from({-4}));
        assert((verdict.recovered == Streams{a, b}) && !recovery.waiting());
    }
    // A frame from between the two losses serves only the stream whose loss is the later one.
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        send(recovery, b, 100, 50);
        recovery.lost(b, 50, 50);
        recovery.lost(a, 98, 99);
        recovery.call(100);
        auto verdict = recovery.frame(101, false, from({-2}));
        assert(verdict.recovered == Streams{b} && verdict.keyframe.empty() && recovery.waiting());
        verdict = recovery.frame(102, false, from({-5}));
        assert(verdict.recovered == Streams{a});
    }
    // A second, older loss of the same stream while it waits: the older one counts, and the encoder is told again.
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        recovery.lost(a, 100, 100);
        assert(recovery.call(100).invalidate == 100u);
        assert(recovery.lost(a, 99, 100) == Loss::told);
        assert(recovery.call(100).invalidate == 99u);
        assert(recovery.frame(101, false, from({-2})).recovered.empty());  // from 99: the stream doesn't have it
        assert(recovery.frame(102, false, from({-4})).recovered == Streams{a});
    }
    // A loss named after the call and before its own: the frame in between is judged, the patience starts anew.
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        send(recovery, b, 100, 50);
        recovery.lost(a, 100, 100);
        recovery.call(100);
        recovery.lost(b, 50, 50);
        auto verdict = recovery.frame(101, false, from({-2}));
        assert((verdict.recovered == Streams{a, b}));
    }
    // A frame decoded from one the stream lost the time before is none to go on from.
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        recovery.lost(a, 98, 99);
        recovery.call(100);
        assert(recovery.frame(101, false, from({-4})).recovered == Streams{a});  // 98 to 100 are missing
        send(recovery, a, 103, 0, 101);
        recovery.lost(a, 103, 103);
        recovery.call(103);
        assert(recovery.frame(104, false, from({-3, -5})).recovered.empty());  // 99 is one of them
        assert(recovery.frame(105, false, from({-4, -8})).recovered == Streams{a});
    }
    // Two streams share the encoder. One went on from frame 103 after missing 100 to 102; then the other loses 102 and
    // the encoder is told. A frame decoded from 101 serves the second and breaks the first, which never had 101: it
    // gets a keyframe. Decoded from 99, it serves both.
    for (const int reference : {-3, -5}) {
        LossRecovery recovery(7);
        send(recovery, a, 102);
        send(recovery, b, 102, 50);
        recovery.lost(a, 100, 101);
        recovery.call(102);
        assert(recovery.frame(103, false, from({-4})).recovered == Streams{a});
        recovery.sent(a, 103, 103);
        recovery.sent(b, 53, 103);
        assert(recovery.lost(b, 52, 52) == Loss::told);
        assert(recovery.call(103).invalidate == 102u);
        assert(recovery.watching(104));
        const auto verdict = recovery.frame(104, false, from({reference}));
        assert(verdict.recovered == Streams{b} && verdict.keyframe.empty());
        assert(verdict.held == (reference == -3 ? Streams{a} : Streams{}));
        // The frames after the call are still looked at while nobody waits, and then no more.
        assert(!recovery.waiting() && recovery.watching(110) && !recovery.watching(111));
        if (reference == -5) {
            assert(recovery.frame(105, false, from({-1})).held.empty());
            assert(recovery.frame(106, false, from({-5})).held == Streams{a});  // from 101 after all
            assert(recovery.frame(107, false, from({-6})).held.empty());  // held already
            assert(recovery.frame(108, false, std::nullopt).held.empty());
            // The held stream's client may still name a frame it lost before: the keyframe on its way answers it.
            assert(recovery.lost(a, 103, 103) == Loss::answered);
            recovery.frame(109, true, std::nullopt);
            recovery.sent(a, 104, 109);
            recovery.sent(a, 105, 110);
            assert(recovery.lost(a, 105, 105) == Loss::told);
        }
    }
    // What a stream was missing counts for as long as a frame may reach back, also over several losses.
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        recovery.lost(a, 100, 100);
        recovery.call(100);
        assert(recovery.frame(101, false, from({-2})).recovered == Streams{a});  // 100 is missing
        send(recovery, a, 110, 0, 101);
        recovery.lost(a, 110, 110);
        recovery.call(110);
        assert(recovery.frame(111, false, from({-2})).recovered == Streams{a});  // and 110
        send(recovery, a, 120, 0, 111);
        recovery.lost(a, 120, 120);
        recovery.call(120);
        assert(recovery.frame(121, false, from({-11})).recovered.empty());  // from 110
        assert(recovery.frame(122, false, from({-22})).recovered.empty());  // from 100
        assert(recovery.frame(123, false, from({-4})).recovered == Streams{a});
        // A frame decoded from further back than anything is remembered is none to go on from.
        send(recovery, a, 130, 0, 121);
        recovery.lost(a, 130, 130);
        recovery.call(130);
        assert(recovery.frame(131, false, from({-65})).recovered.empty());
        assert(recovery.frame(132, false, from({-64})).recovered == Streams{a});
    }
    // A frame the client names that isn't remembered (too long ago, or never sent to it) needs a keyframe.
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        assert(recovery.lost(a, 36, 100) == Loss::keyframe);
        assert(recovery.lost(b, 100, 100) == Loss::keyframe);
        LossRecovery other(7);
        send(other, a, 100);
        assert(other.lost(a, 37, 37) == Loss::told);  // the oldest one remembered
        assert(other.lost(a, 101, 101) == Loss::keyframe && !other.waiting());  // never sent
    }
    // A keyframe that comes while a stream waits ends the wait; nothing is marked, a keyframe needs no mark. What the
    // client sent before it could have it is answered, a keyframe that was lost itself needs another.
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        recovery.lost(a, 100, 100);
        recovery.call(100);
        const auto verdict = recovery.frame(101, true, std::nullopt);
        assert(verdict.recovered.empty() && verdict.keyframe.empty() && !recovery.waiting());
        send(recovery, a, 103, 0, 101);
        assert(recovery.lost(a, 100, 100) == Loss::answered);
        assert(recovery.lost(a, 100, 102) == Loss::keyframe);
        assert(recovery.lost(a, 100, 103) == Loss::answered);
        recovery.frame(104, true, std::nullopt);
        send(recovery, a, 106, 0, 104);
        assert(recovery.lost(a, 105, 105) == Loss::told);  // a loss after the keyframe goes on without one
        assert(recovery.call(106).invalidate == 105u);
    }
    // A header that can't be read, or a frame without references: a keyframe for everyone who waits.
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        send(recovery, b, 100, 50);
        recovery.lost(a, 100, 100);
        recovery.lost(b, 50, 50);
        recovery.call(100);
        assert((recovery.frame(101, false, std::nullopt).keyframe == Streams{a, b}) && !recovery.waiting());
        recovery.frame(102, true, std::nullopt);
        send(recovery, a, 103, 0, 101);
        recovery.lost(a, 103, 103);
        recovery.call(103);
        assert(recovery.frame(104, false, from({})).keyframe == Streams{a});
    }
    // The encoder refuses the call.
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        recovery.lost(a, 100, 100);
        assert(recovery.call(100).invalidate);
        assert(recovery.call_failed() == Streams{a} && !recovery.waiting());
    }
    // A stream that is gone neither waits nor is remembered.
    {
        LossRecovery recovery(7);
        send(recovery, a, 100);
        recovery.lost(a, 100, 100);
        recovery.forget(a);
        assert(!recovery.waiting() && !recovery.call(100).invalidate && recovery.lost(a, 100, 100) == Loss::keyframe);
    }
    // Frames without anyone waiting cost nothing and change nothing.
    {
        LossRecovery recovery(7);
        const auto verdict = recovery.frame(5, false, std::nullopt);
        assert(verdict.recovered.empty() && verdict.keyframe.empty());
    }
    // The encoder's counter wraps.
    {
        LossRecovery recovery(7);
        for (uint32_t i = 0; i < 10; ++i) recovery.sent(a, 1000 + i, 0xfffffff8u + i);  // frame 1008 is counter 0
        assert(recovery.lost(a, 1007, 1008) == Loss::told);
        assert(recovery.call(1).invalidate == 0xffffffffu);
        assert(recovery.frame(2, false, from({-2})).recovered.empty());  // from counter 0
        assert(recovery.frame(3, false, from({-5})).recovered == Streams{a});
        recovery.sent(a, 1011, 3);
        assert(recovery.lost(a, 1007, 1009) == Loss::answered);
    }
    std::cout << "PASS loss_recovery_test\n";
    return 0;
}
