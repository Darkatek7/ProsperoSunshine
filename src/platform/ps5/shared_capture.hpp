// SPDX-License-Identifier: GPL-3.0-only
// One Avcap2 session per capture slot (capture.h), shared by the video backend and the audio microphone, which
// Sunshine runs on separate threads. The session closes when the last user releases it. Audio always uses slot 0's;
// slot 1 is a second video channel next to it, for a device that wants another mode.
//
// The system can take the capture resource away (starting a game does). A user that sees a read
// or the headers fail marks the session failed; the next acquire() then closes and reopens it in place, and the
// generation counter tells the other users that their channels belong to the old session.
#pragma once
#include "platform/ps5/capture.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>

namespace prospero {

class SharedCapture {
public:
    // Returns the open session, opening or reopening it if needed; on failure null and rc holds
    // the error. Audio usually gets here first, so a session whose mode differs from what was set
    // since (codec, bitrate, frame rate) is reopened; audio follows through generation().
    // open_when_stuck false (audio): while the last OpenVideo returned -1 (ps_capture_stuck, issue 56), a session that
    // would have to be opened or reopened is not, and the result is null; only video tries once more for a new stream.
    static std::shared_ptr<SharedCapture> acquire(int &rc, int slot = 0, bool open_when_stuck = true);
    // The open session if there is one, without opening it or waiting for an open in progress; else null.
    static std::shared_ptr<SharedCapture> attach(int slot = 0);
    ~SharedCapture();
    SharedCapture(const SharedCapture &) = delete;
    SharedCapture &operator=(const SharedCapture &) = delete;

    // Calls fn(ps_capture *) while the session cannot be replaced (reads may run concurrently).
    template <class F> auto with_session(F &&fn) {
        std::shared_lock lock(mutex_);
        return fn(session_);
    }
    // Calls fn(ps_capture *) alone; for calls that change session state.
    template <class F> auto exclusive(F &&fn) {
        std::unique_lock lock(mutex_);
        return fn(session_);
    }

    void mark_failed() { failed_ = true; }
    unsigned generation() const { return generation_; }

private:
    SharedCapture(ps_capture *session, int slot) : session_(session), slot_(slot) {}
    int reopen();  // caller holds mutex_ exclusively

    ps_capture *session_;
    int slot_;
    std::shared_mutex mutex_;
    std::atomic_bool failed_{false};
    std::atomic<unsigned> generation_{0};
};

}
