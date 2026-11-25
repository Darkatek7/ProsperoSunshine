// SPDX-License-Identifier: GPL-3.0-only
// Text from a client's on-screen keyboard, typed key by key as on a US keyboard (keyboard_map.hpp, ascii_key): a key
// goes down, stays down for `hold`, comes up, and the next one waits `hold` again, as the console doesn't see a key
// that is let go within a few ms.
//
// The typing can't wait on the thread that brought the text: Sunshine passes every stream's input on from one thread.
// Measured on 13.00 with 0.6.7, which slept there: a text of 100 characters held every device's controllers for 6.2 s,
// and a D-pad press sent just before it stayed down that long, its release waiting behind the text. So the text is
// queued here and the host's 10 ms tick types it (input_kbm_ps5.cpp, type_texts). Portable
// (tests/text_typist_test.cpp).
#pragma once
#include "input/keyboard_map.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string_view>

namespace prospero {

class TextTypist {
public:
    using clock = std::chrono::steady_clock;
    static constexpr std::chrono::milliseconds hold{30};
    // What may wait: half a minute of typing. A longer paste loses its end, which the log says.
    static constexpr std::size_t max_pending = 512;

    struct Pushed {
        int queued = 0;
        int without_key = 0;  // characters a US keyboard has no key for; a UTF-8 sequence counts once
        int over = 0;  // dropped because the queue was full
    };

    Pushed push(std::string_view text) {
        Pushed result;
        for (const char c : text) {
            const AsciiKey key = ascii_key(c);
            if (!key.usage) {
                result.without_key += (static_cast<uint8_t>(c) & 0xC0) != 0x80;
            } else if (pending_.size() >= max_pending) {
                ++result.over;
            } else {
                pending_.push_back(key);
                ++result.queued;
            }
        }
        return result;
    }

    // The next key goes down no sooner than this: a keyboard just added drops what comes at once.
    void not_before(clock::time_point at) {
        if (at > next_) next_ = at;
    }

    bool idle() const { return pending_.empty() && !down_.usage; }
    std::size_t pending() const { return pending_.size(); }

    // The key that is down now: its usage, 0 while none, and whether it needs Shift.
    uint8_t usage() const { return down_.usage; }
    bool shift() const { return down_.usage && down_.shift; }

    // Called every few ms. True when the key that is down changed, and the caller sends the keyboard's report.
    bool tick(clock::time_point now) {
        if (now < next_) return false;
        if (down_.usage) {
            down_ = {};
        } else if (!pending_.empty()) {
            down_ = pending_.front();
            pending_.pop_front();
        } else {
            return false;
        }
        next_ = now + hold;
        return true;
    }

    // Forgets the text and the key that is down (the caller's keyboard goes, or sends a report without it).
    void clear() {
        pending_.clear();
        down_ = {};
        next_ = {};
    }

private:
    std::deque<AsciiKey> pending_;
    AsciiKey down_{0, false};
    clock::time_point next_{};
};

}  // namespace prospero
