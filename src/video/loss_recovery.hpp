// SPDX-License-Identifier: GPL-3.0-only
// Getting over a lost frame without a keyframe (HEVC; docs/re/encoder-options.md, section 5). A client that lost a
// frame names it; the host tells the encoder (sceAvcap2SetInvalidFrame), which then decodes its next frame from the
// newest picture before the loss, and the host marks that frame for the client as the one to go on from. A keyframe is
// five to ten times a frame's size for every device on the channel; this costs one frame of ordinary size.
//
// What the client does (moonlight-common-c, read in the pinned source), once the host announces it: after a loss it
// drops every frame until one marked as coming after the invalidation, or a keyframe. It names the lost frames (first,
// last) when the next complete frame arrives, and again with a later `last` for every further complete frame it has to
// drop, so one loss brings a request per frame until the marked one arrives. After 120 dropped frames it asks for a
// keyframe.
//
// What decides here, and what was measured on 13.00 (rfi_probe, 2026-11-25):
//  - The encoder counts its frames, and so does each stream (a stream that joined later, or that the host held frames
//    back from, counts differently), so each stream's numbers are kept next to the encoder's for its last frames.
//  - The call names the oldest lost frame of any stream that waits. The frame the client goes on from is the first
//    one whose references all lie before that stream's loss: in the probe the very next frame, but one already being
//    encoded when the call came would still hang on the old chain, so the slice header decides (hevc_refs.hpp), frame
//    by frame, for a few frames.
//  - A decoder tells the order of two pictures from a few bits of a counter (4 on the console: at most 7 pictures may
//    be missing in a row, lost and dropped ones together). Beyond that, and whenever anything here is not known for
//    sure, a keyframe is asked for instead, as before.
//  - Streams that share a channel share the encoder, and every one of them gets the frame another one goes on from.
//    The encoder was told which frames each of them lost, and whether it remembers that for longer than its next
//    frame is not measured, so the frames after a call are checked for every stream: one decoded from a picture a
//    stream never had would break that stream's picture. That stream gets no frame until the next keyframe, which is
//    asked for: its client sees no gap in the frame numbers, only a pause of a few frames.
//  - A request names the frames it is about, so one that left the client before it could have the keyframe or the
//    marked frame that answers it is told apart from one that says this very frame was lost too: its last frame lies
//    before that frame. (The keyframe policy has to guess that from the round trip, keyframe_policy.hpp rule 2.)
// Portable (tests/loss_recovery_test.cpp). Not thread safe: the channel's reader calls it under the channels' lock.
#pragma once
#include "video/hevc_refs.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace prospero {

class LossRecovery {
public:
    using Stream = std::uintptr_t;
    // How many of a stream's last frames are remembered: a client names a loss within a few frames of it.
    static constexpr std::size_t remembered = 64;
    // How many frames after the call may still hang on the old chain before a keyframe is asked for.
    static constexpr uint32_t patience = 3;
    // For how many frames after the call each frame's references are looked at, for the streams that don't wait.
    static constexpr uint32_t watched = 8;
    // How far back a frame may be decoded from: what a stream was missing is remembered that long.
    static constexpr int reach = 64;

    // `most_missing`: Layout::most_missing() of the channel's stream; 0 turns every loss into a keyframe (H.264, a
    // layout hevc_refs.hpp doesn't read), while requests a keyframe answered already are still told apart.
    explicit LossRecovery(int most_missing = 0) : most_missing_(most_missing) {}

    bool usable() const { return most_missing_ > 0; }

    // The encoder's frame `counter` went to `stream` as its frame `index`.
    void sent(Stream stream, int64_t index, uint32_t counter) {
        auto &frames = sent_[stream];
        frames.emplace_back(index, counter);
        if (frames.size() > remembered) frames.pop_front();
    }

    // The stream is gone.
    void forget(Stream stream) {
        sent_.erase(stream);
        waiting_.erase(stream);
        synced_.erase(stream);
        gaps_.erase(stream);
        keyframe_wanted_.erase(stream);
        held_.erase(stream);
    }

    enum class Loss {
        told,  // the encoder will be told (call()), or was told and the stream waits for its frame
        answered,  // nothing to do: a keyframe or the marked frame went out after the frames it names, or a keyframe is asked for
        keyframe,  // ask for a keyframe
    };
    // `stream` lost its frames `first` to `last` (its own numbers), or had to drop them.
    Loss lost(Stream stream, int64_t first, int64_t last) {
        if (keyframe_wanted_.count(stream) || held_.count(stream)) return Loss::answered;
        const auto frames = sent_.find(stream);
        if (frames == sent_.end() || frames->second.empty()) return want_keyframe(stream);
        const auto &known = frames->second;
        const auto counter_of = [&known](int64_t index) -> std::optional<uint32_t> {
            for (const auto &[i, counter] : known)
                if (i == index) return counter;
            return std::nullopt;
        };
        // A last frame from before the remembered ones lies before all of them.
        auto newest = counter_of(std::max(first, last));
        if (!newest && last < known.front().first) newest = known.front().second - 1;
        if (newest) {
            if (keyframe_ && before(*newest, *keyframe_)) return Loss::answered;
            if (const auto at = synced_.find(stream); at != synced_.end() && before(*newest, at->second)) return Loss::answered;
        }
        const auto from = counter_of(first);
        if (!usable() || !from) return want_keyframe(stream);
        // The last keyframe is among the lost frames: nothing before it is left to go on from.
        if (keyframe_ && !before(*keyframe_, *from)) return want_keyframe(stream);
        const auto [it, added] = waiting_.try_emplace(stream, *from);
        if (added) {
            call_due_ = true;
        } else if (before(*from, it->second)) {
            it->second = *from;
            call_due_ = true;
        }
        return Loss::told;
    }

    // What to do between two frames, `newest` being the counter of the frame read last.
    struct Call {
        std::optional<uint32_t> invalidate;  // tell the encoder this frame and those after it are lost
        std::vector<Stream> keyframe;  // these streams need a keyframe after all
    };
    Call call(uint32_t newest) {
        Call result;
        if (!call_due_) return result;
        call_due_ = false;
        // Even the very next frame would come too late for a stream that is missing too many by then.
        for (auto it = waiting_.begin(); it != waiting_.end();) {
            if (missing(newest + 1, it->second) > most_missing_) {
                result.keyframe.push_back(it->first);
                keyframe_wanted_.insert(it->first);
                it = waiting_.erase(it);
            } else {
                ++it;
            }
        }
        if (waiting_.empty()) return result;
        uint32_t oldest = waiting_.begin()->second;
        for (const auto &[stream, from] : waiting_)
            if (before(from, oldest)) oldest = from;
        result.invalidate = oldest;
        give_up_after_ = newest + patience;
        watch_until_ = newest + watched;
        return result;
    }

    // The encoder refused the call: every waiting stream needs a keyframe.
    std::vector<Stream> call_failed() { return take_all(); }

    // The keyframe that was asked for doesn't come (the encoder refused): a stream that names its loss again is
    // answered anew.
    void keyframe_dropped() { keyframe_wanted_.clear(); }

    // Whether the frame with this counter has to come with its references: a stream waits for the one it goes on
    // from, or the encoder was told of a loss a few frames ago and may still decode from an unusual picture.
    bool watching(uint32_t counter) const {
        return !waiting_.empty() || (watch_until_ && !gaps_.empty() && before(counter, *watch_until_));
    }

    // A frame was read: `counter`, whether it is a keyframe, and its references if its header could be read (needed
    // only while watching(counter)).
    struct Verdict {
        std::vector<Stream> recovered;  // mark the frame for these streams: they go on from it
        std::vector<Stream> keyframe;  // these streams need a keyframe after all
        std::vector<Stream>
            held;  // not this frame nor any until a keyframe, which they need: it is decoded from one they never had
    };
    Verdict frame(uint32_t counter, bool keyframe, const std::optional<hevc::Refs> &refs) {
        Verdict result;
        if (keyframe) {  // every client goes on from it
            keyframe_ = counter;
            waiting_.clear();
            synced_.clear();
            gaps_.clear();
            keyframe_wanted_.clear();
            held_.clear();
            watch_until_.reset();
            call_due_ = false;
            return result;
        }
        if (!watching(counter)) return result;
        // What a stream was missing so long ago that no frame is decoded from it any more.
        for (auto it = gaps_.begin(); it != gaps_.end();) {
            std::erase_if(it->second, [counter](const auto &gap) { return missing(counter, gap.second) > reach; });
            it = it->second.empty() ? gaps_.erase(it) : std::next(it);
        }
        if (!refs || refs->keyframe || refs->used.empty()) {
            result.keyframe = take_all();
            return result;
        }
        // A stream that doesn't wait would get this frame as well: decoded from a picture it never had, the frame
        // breaks its picture, and so does every frame after it until a keyframe.
        for (const auto &[stream, gaps] : gaps_) {
            if (waiting_.count(stream) || held_.count(stream) || has_all(stream, counter, *refs)) continue;
            result.held.push_back(stream);
            held_.insert(stream);
        }
        for (auto it = waiting_.begin(); it != waiting_.end();) {
            const uint32_t from = it->second;
            if (clean(it->first, counter, from, *refs) && missing(counter, from) <= most_missing_) {
                result.recovered.push_back(it->first);
                synced_[it->first] = counter;
                gaps_[it->first].emplace_back(from, counter);
                it = waiting_.erase(it);
            } else if ((!call_due_ && !before(counter, give_up_after_)) || missing(counter + 1, from) > most_missing_) {
                // (While a loss named since the last call still waits for its own call, the patience starts anew.)
                result.keyframe.push_back(it->first);
                keyframe_wanted_.insert(it->first);
                it = waiting_.erase(it);
            } else {
                ++it;
            }
        }
        return result;
    }

    bool waiting() const { return !waiting_.empty(); }

private:
    // The counter may wrap (after two years at 60 fps); two frames compared here are never 2^31 apart.
    static bool before(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) < 0; }
    // How many pictures a stream is missing in a row when it goes on from frame `at` after losing those from `from` on.
    static int missing(uint32_t at, uint32_t from) {
        const uint32_t count = at - from;
        return count > 0x7fffffffu ? 0 : count > 100000u ? 100000 : static_cast<int>(count);
    }
    // Whether the stream has every picture the frame is decoded from: none of them among the frames it was missing
    // before a frame it went on from, and none further back than those are remembered.
    bool has_all(Stream stream, uint32_t counter, const hevc::Refs &refs) const {
        const auto gaps = gaps_.find(stream);
        for (const int distance : refs.used) {
            if (distance >= 0 || distance < -reach) return false;
            if (gaps == gaps_.end()) continue;
            const uint32_t picture = counter - static_cast<uint32_t>(-distance);
            for (const auto &[from, to] : gaps->second)
                if (!before(picture, from) && before(picture, to)) return false;
        }
        return true;
    }
    // Whether a stream that lost the frames from `from` on can go on from this frame: it is decoded only from pictures
    // before the loss, and the stream has them.
    bool clean(Stream stream, uint32_t counter, uint32_t from, const hevc::Refs &refs) const {
        for (const int distance : refs.used)
            if (distance >= 0 || !before(counter - static_cast<uint32_t>(-static_cast<int64_t>(distance)), from)) return false;
        return has_all(stream, counter, refs);
    }
    Loss want_keyframe(Stream stream) {
        waiting_.erase(stream);
        keyframe_wanted_.insert(stream);
        return Loss::keyframe;
    }
    std::vector<Stream> take_all() {
        std::vector<Stream> all;
        for (const auto &[stream, from] : waiting_) {
            all.push_back(stream);
            keyframe_wanted_.insert(stream);
        }
        waiting_.clear();
        call_due_ = false;
        return all;
    }

    int most_missing_;
    std::map<Stream, std::deque<std::pair<int64_t, uint32_t>>> sent_;  // index, counter
    std::map<Stream, uint32_t> waiting_;  // the encoder's counter of each waiting stream's first lost frame
    std::map<Stream, uint32_t> synced_;  // the frame each stream last went on from
    std::map<Stream, std::vector<std::pair<uint32_t, uint32_t>>>
        gaps_;  // and what it was missing before such frames: from, to (not included)
    std::set<Stream> keyframe_wanted_;  // told to ask for a keyframe, none read since
    std::set<Stream> held_;  // get no frame until a keyframe
    std::optional<uint32_t> keyframe_;  // the last keyframe read
    bool call_due_ = false;
    uint32_t give_up_after_ = 0;
    std::optional<uint32_t> watch_until_;  // frames before this one are looked at after the last call
};

}  // namespace prospero
