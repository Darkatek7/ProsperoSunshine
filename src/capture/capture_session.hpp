// SPDX-License-Identifier: GPL-3.0-only
// M1: lifecycle around a capture backend. Portable; the PS5 backend lives in platform/ps5.
#pragma once
#include "video/annex_b.hpp"
#include "video/frame_queue.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <vector>

namespace prospero {

// Mirrors ps_capture_* so the session can be tested without a console. Return codes follow
// enum ps_capture_error: 0 success, negative errors; read() returns >0 bytes, 0 for no frame yet.
class CaptureBackend {
public:
    virtual ~CaptureBackend() = default;
    virtual int open() = 0;
    virtual int headers(void *destination, size_t capacity) = 0;
    virtual int read(void *destination, size_t capacity, uint8_t descriptor[0x88]) = 0;
    virtual int close() = 0;
    // Optional encoder controls (R1/R3); backends without them report failure.
    virtual int request_keyframe() { return -1; }
    virtual int set_bitrate(uint32_t, uint32_t) { return -1; }
    virtual int invalidate_frame(uint32_t) { return -1; }
};

// slot: the capture channel (capture.h, PS_CAPTURE_SLOTS), 0 unless a second channel runs next to the first.
std::unique_ptr<CaptureBackend> make_ps5_capture_backend(Codec codec = Codec::h264, int slot = 0);

enum class CaptureStatus { frame, timeout, stopped, revoked, error };

class CaptureSession {
public:
    static constexpr size_t max_frame_bytes = 4 * 1024 * 1024;
    using Descriptor = std::array<uint8_t, 0x88>;

    explicit CaptureSession(std::unique_ptr<CaptureBackend> backend, Codec codec = Codec::h264);
    ~CaptureSession();
    CaptureSession(const CaptureSession &) = delete;
    CaptureSession &operator=(const CaptureSession &) = delete;

    // Opens the backend and caches the parameter sets. Returns 0 or the backend's error code.
    int start();
    // Waits up to timeout for the next frame. IDR frames always carry the parameter sets in front, so every
    // IDR is a decodable entry point. After revoked/error the session is stopped.
    CaptureStatus read(EncodedFrame &frame, std::chrono::milliseconds timeout, std::stop_token stop = {},
                       Descriptor *descriptor = nullptr);
    // Closes the backend once; returns its result. Safe to call repeatedly.
    int stop();
    // Asks the encoder for an IDR within a few frames. 0 or error.
    int request_keyframe() { return running_ ? backend_->request_keyframe() : -1; }
    // Target and peak bitrate in bit/s; 0 keeps a value. 0 or error.
    int set_bitrate(uint32_t average, uint32_t peak) { return running_ ? backend_->set_bitrate(average, peak) : -1; }
    // Tells the encoder that a client lost the frame with this counter (the read descriptor's) and those after it: the
    // next frame is decoded from the newest picture before it (HEVC; video/loss_recovery.hpp). 0 or error.
    int invalidate_frame(uint32_t counter) { return running_ ? backend_->invalidate_frame(counter) : -1; }

    bool running() const { return running_; }
    const std::vector<uint8_t> &parameter_sets() const { return headers_; }
    uint64_t frames() const { return next_index_; }

private:
    std::unique_ptr<CaptureBackend> backend_;
    Codec codec_;
    std::vector<uint8_t> headers_;
    std::vector<uint8_t> buffer_;
    uint64_t next_index_ = 0;
    bool running_ = false;
};

}
