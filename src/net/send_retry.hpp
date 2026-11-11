// SPDX-License-Identifier: GPL-3.0-only
// How the PS5 platform layer sends a batch of UDP datagrams (src/sunshine/platform_ps5.cpp, send_batch and send).
// Portable, so that the tests can run it.
//
// A full socket buffer (EAGAIN, ENOBUFS) is waited out in short steps, but for at most `max_waits` steps per batch, not
// per packet: the one video broadcast thread sends every session's frames, and a buffer that stays full would otherwise
// hold all of them for up to 10 ms per packet. Once the batch's steps are spent, each packet left gets one more try
// without waiting. Any other error (EHOSTUNREACH while a phone changes networks, an address the socket can't reach) is
// the same for every packet of the batch, which all go to one address, so the rest of the batch is dropped at once.
// The caller learns how many packets left and how many were dropped; a batch is never sent twice.
#pragma once
#include <cerrno>
#include <cstddef>

namespace prospero {

// Whether an error from sendmsg() means the socket buffer is full for a moment (EWOULDBLOCK is EAGAIN on FreeBSD).
inline constexpr bool send_buffer_full(int error) { return error == EAGAIN || error == EWOULDBLOCK || error == ENOBUFS; }

struct SendBatchResult {
    std::size_t sent = 0;     // packets that left
    std::size_t dropped = 0;  // packets given up on
    int waits = 0;            // steps waited for a full buffer
    int error = 0;            // the error that ended the batch early, 0 if none
};

// Sends packets 0 to count - 1 in order. try_send(i) sends packet i and returns 0 or the errno of the failure; wait() lets
// one step pass (the platform sleeps 100 us).
template<class TrySend, class Wait>
SendBatchResult send_with_retries(std::size_t count, int max_waits, TrySend &&try_send, Wait &&wait) {
    SendBatchResult result;
    for (std::size_t i = 0; i < count; ++i) {
        for (;;) {
            const int error = try_send(i);
            if (error == 0) {
                ++result.sent;
                break;
            }
            if (!send_buffer_full(error)) {
                result.error = error;
                result.dropped += count - i;
                return result;
            }
            if (result.waits >= max_waits) {
                ++result.dropped;
                break;
            }
            ++result.waits;
            wait();
        }
    }
    return result;
}

}
