// SPDX-License-Identifier: GPL-3.0-only
// SharedCapture (shared_capture.cpp) and the PS5 capture backend (capture_backend.cpp) over capture.c's real open path,
// against tests/fake_avcap.h (through tests/shared_capture_fake.c): when the video and the audio open a capture, on a
// stuck console (issue 56) and after the parameter sets failed.
#include "capture/capture_session.hpp"
#include "platform/ps5/shared_capture.hpp"
#include <cassert>
#include <iostream>

extern "C" {
void fake_test_reset(void);
void fake_test_stuck(int on);
void fake_test_fail_headers(int times);
int fake_test_opens(void);  // OpenVideo calls since the reset
int fake_test_logged(const char *text);
}

using prospero::SharedCapture;

namespace {
// Each connect on a stuck console makes one OpenVideo -1, the video's: audio (open_when_stuck false) opens nothing.
void stuck_console() {
    fake_test_reset();
    fake_test_stuck(1);
    int rc = 0;
    // A new stream's video tries once.
    assert(!SharedCapture::acquire(rc) && rc != 0);
    assert(fake_test_opens() == 1 && ps_capture_stuck() && fake_test_logged("capture OpenVideo rc=0xffffffff\n"));
    // Its audio, after it: no second open.
    assert(!SharedCapture::acquire(rc, 0, false) && rc == PS_CAPTURE_ERROR);
    assert(fake_test_opens() == 1 && !SharedCapture::attach());
    // The next stream's audio ahead of its video: still none of its own, and the video tries once more.
    assert(!SharedCapture::acquire(rc, 0, false) && fake_test_opens() == 1);
    assert(!SharedCapture::acquire(rc) && fake_test_opens() == 2);

    // A good open clears the flag, and audio opens a session itself again.
    fake_test_stuck(0);
    auto video = SharedCapture::acquire(rc);
    assert(video && rc == 0 && fake_test_opens() == 3 && !ps_capture_stuck());
    video.reset();
    auto audio = SharedCapture::acquire(rc, 0, false);
    assert(audio && rc == 0 && fake_test_opens() == 4);
    video = SharedCapture::acquire(rc);
    assert(video == audio && fake_test_opens() == 4);

    // A session audio still holds, lost while the console got stuck: the video's reopen fails, audio's makes none.
    fake_test_stuck(1);
    video->mark_failed();
    video.reset();
    assert(!SharedCapture::acquire(rc) && fake_test_opens() == 5 && ps_capture_stuck());
    assert(!SharedCapture::acquire(rc, 0, false) && rc == PS_CAPTURE_ERROR && fake_test_opens() == 5);
    assert(SharedCapture::attach() == audio);
    // Once OpenVideo works again, the video reopens it in place, and audio follows through the generation.
    fake_test_stuck(0);
    const unsigned generation = audio->generation();
    video = SharedCapture::acquire(rc);
    assert(video == audio && fake_test_opens() == 6 && audio->generation() == generation + 1);
    assert(SharedCapture::acquire(rc, 0, false) == audio && fake_test_opens() == 6);
    video.reset();
    audio.reset();
    assert(!SharedCapture::attach());
}

// On a working console audio still reopens the session it finds, as the video would: one marked failed, and one opened in
// another mode (audio usually reaches a new stream's session first).
void healthy_console() {
    fake_test_reset();
    int rc = 0;
    auto held = SharedCapture::acquire(rc);
    assert(held && rc == 0 && fake_test_opens() == 1 && !ps_capture_stuck());
    held->mark_failed();
    unsigned generation = held->generation();
    assert(SharedCapture::acquire(rc, 0, false) == held && rc == 0 && fake_test_opens() == 2);
    assert(held->generation() == generation + 1);
    // A new stream's bitrate: the session's mode is no longer the current one.
    ps_capture_set_opening_bitrate(8000000, 0);
    const auto current = [](ps_capture *session) { return ps_capture_mode_current(session); };
    assert(!held->with_session(current));
    generation = held->generation();
    assert(SharedCapture::acquire(rc, 0, false) == held && rc == 0 && fake_test_opens() == 3);
    assert(held->generation() == generation + 1 && held->with_session(current));
    // Current and not failed: taken as it is.
    assert(SharedCapture::acquire(rc, 0, false) == held && fake_test_opens() == 3);
    ps_capture_set_opening_bitrate(6000000, 0);
    held.reset();
    assert(!SharedCapture::attach());
}

// Parameter sets that fail while audio holds the session mark it failed: the next start reopens it instead of getting
// the same session back.
void failed_headers() {
    fake_test_reset();
    int rc = 0;
    auto audio = SharedCapture::acquire(rc, 0, false);
    assert(audio && fake_test_opens() == 1);
    const unsigned generation = audio->generation();
    prospero::CaptureSession video(prospero::make_ps5_capture_backend(prospero::Codec::h264, 0));
    fake_test_fail_headers(1);
    assert(video.start() != 0 && !video.running() && fake_test_logged("capture headers rc=0x80c78000\n"));
    assert(fake_test_opens() == 1 && SharedCapture::attach() == audio);
    assert(video.start() == 0 && video.running() && !video.parameter_sets().empty());
    assert(fake_test_opens() == 2 && audio->generation() == generation + 1);
    // The session is good again: the next start takes it as it is.
    video.stop();
    assert(video.start() == 0 && fake_test_opens() == 2);
    video.stop();
    audio.reset();
    assert(!SharedCapture::attach());
}
}

int main() {
    stuck_console();
    healthy_console();
    failed_headers();
    std::cout << "PASS: shared capture (audio opens nothing on a stuck console but reopens on a working one, failed "
                 "parameter sets reopen the session)\n";
}
