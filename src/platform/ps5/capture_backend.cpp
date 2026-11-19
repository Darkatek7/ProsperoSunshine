// SPDX-License-Identifier: GPL-3.0-only
#include "capture/capture_session.hpp"
#include "platform/ps5/shared_capture.hpp"

namespace prospero {
namespace {
class Ps5CaptureBackend final : public CaptureBackend {
public:
    Ps5CaptureBackend(Codec codec, int slot) : codec_(codec), slot_(slot) {}
    int open() override {
        int rc = 0;
        shared_ = SharedCapture::acquire(rc, slot_);
        return rc;
    }
    int headers(void *destination, size_t capacity) override {
        const int rc = shared_->with_session([&](ps_capture *s) { return ps_capture_headers(s, destination, capacity); });
        // The stream can't start without them, and audio may hold the session on: the next open() reopens it, as after a
        // failed read, instead of handing out the same session again.
        if (rc <= 0) shared_->mark_failed();
        return rc;
    }
    int read(void *destination, size_t capacity, uint8_t descriptor[0x88]) override {
        const int rc = shared_->with_session([&](ps_capture *s) {
            return codec_ == Codec::hevc ? ps_capture_read2(s, destination, capacity, descriptor)
                                         : ps_capture_read(s, destination, capacity, descriptor);
        });
        // Lost resource or a dead channel: the next open() reopens the shared session.
        if (rc < 0) shared_->mark_failed();
        return rc;
    }
    int request_keyframe() override {
        return shared_->exclusive([](ps_capture *s) { return ps_capture_request_keyframe(s); });
    }
    int set_bitrate(uint32_t average, uint32_t peak) override {
        return shared_->exclusive([&](ps_capture *s) { return ps_capture_set_bitrate(s, average, peak); });
    }
    // Releases this user's reference; the session closes once audio has released it too.
    int close() override {
        shared_.reset();
        return 0;
    }

private:
    Codec codec_;
    int slot_;
    std::shared_ptr<SharedCapture> shared_;
};
}

std::unique_ptr<CaptureBackend> make_ps5_capture_backend(Codec codec, int slot) {
    return std::make_unique<Ps5CaptureBackend>(codec, slot);
}
}
