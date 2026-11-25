// SPDX-License-Identifier: GPL-3.0-only
// Sunshine's audio for the PS5 (platf::audio_control): game audio from Avcap2, shared by every stream's microphone,
// and paced silence while there is none.
#include "audio/audio_meter.hpp"
#include "audio/channel_map.hpp"
#include "platform/ps5/capture.h"
#include "platform/ps5/shared_capture.hpp"
#include "src/config.h"
#include "src/file_handler.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std::literals;

namespace platf {

// Silent microphone: keeps the audio stream alive with correctly paced zero frames.
namespace {
class silent_mic final : public mic_t {
public:
    silent_mic(int channels, std::uint32_t rate, std::uint32_t frame_size)
        : samples_(static_cast<size_t>(channels) * frame_size),
          period_(std::chrono::nanoseconds(1'000'000'000LL * frame_size / rate)),
          next_(std::chrono::steady_clock::now()) {}
    capture_e sample(std::vector<float> &frame_buffer) override {
        next_ += period_;
        std::this_thread::sleep_until(next_);
        frame_buffer.assign(samples_, 0.0f);
        return capture_e::ok;
    }

private:
    size_t samples_;
    std::chrono::nanoseconds period_;
    std::chrono::steady_clock::time_point next_;
};

// Game audio from Avcap2 as raw PCM: float32, stereo, 48 kHz, 1024 frames per packet (R2). Sunshine's
// own Opus encoder then cuts the frames Moonlight negotiated (5 ms by default). With the app's "Surround 5.1/7.1"
// (surround_sound) and a client that asks for 5.1 or 7.1, the channel is read as encoder type 3, 12 channels, and each
// microphone gets its own layout from it (audio/channel_map.hpp). Without the setting, or when type 3 fails, stereo.
//
// When the capture session is lost (a game start takes the resource), replaced by a reopen, or not
// there yet as the stream starts, the audio channel is gone; the microphone then delivers paced
// silence and reopens its channel on the current session every half second, so the stream itself
// never ends because of audio.
//
// The console has one audio channel. With several streams (a second player), the microphone that opened it reads it
// and hands every packet to all microphones; the others only wait for theirs. When the reading one goes, the next to
// notice opens the channel anew.
class avcap_mic;
struct audio_hub {
    std::mutex mutex;
    std::condition_variable arrived;
    std::vector<avcap_mic *> mics;
    avcap_mic *reader = nullptr;
    int source_channels = 2;  // of the channel the reader opened: 2 (type 0) or 12 (type 3)
    int wanted_channels = 2;  // the widest layout any microphone asks for
    bool surround_enabled = false;  // the app's Surround switch
    // Type 3's level per channel, logged every 30 s: which channels carry sound on a console with surround output.
    std::array<double, prospero::kAvcapSurroundChannels> squares{};
    std::size_t frames = 0;
    std::chrono::steady_clock::time_point levels_at{};
    // What was read and what went on to the streams, one line every 5 s at debug level (audio/audio_meter.hpp).
    prospero::AudioMeter meter;
};

class avcap_mic final : public mic_t {
public:
    static std::unique_ptr<mic_t> open(int channels, std::uint32_t sample_rate, std::uint32_t frame_size, bool surround) {
        int rc = 0;
        // Not on a stuck console (issue 56): the video's open for this stream tries once, audio waits for its session.
        auto shared = prospero::SharedCapture::acquire(rc, 0, false);
        if (!shared) BOOST_LOG(warning) << "PS5 audio: capture session unavailable: "sv << rc << ", retrying"sv;
        auto mic = std::unique_ptr<avcap_mic>(new avcap_mic(std::move(shared), channels, sample_rate, frame_size));
        {
            std::lock_guard lock(hub_.mutex);
            hub_.surround_enabled = surround;
        }
        // A title started with the stream can hold the capture for a moment. Returning no microphone here left the
        // whole stream silent: Sunshine only asks for a new one after a reinit, which the silent fallback never sends.
        if (mic->another_reads()) BOOST_LOG(info) << "PS5 audio: sharing the channel another stream reads"sv;
        else if (!mic->shared_ || !mic->open_channel(mic->wanted_channels()))
            mic->retry_at_ = std::chrono::steady_clock::now() + 500ms;
        return mic;
    }

    ~avcap_mic() override {
        if (open_) close_channel();
        std::lock_guard lock(hub_.mutex);
        std::erase(hub_.mics, this);
        hub_.wanted_channels = wanted_channels_locked();
    }

    capture_e sample(std::vector<float> &frame_buffer) override {
        const size_t needed = static_cast<size_t>(frame_size_) * static_cast<size_t>(channels_);
        const auto start = std::chrono::steady_clock::now();
        for (;;) {
            bool follower = false;
            {
                std::unique_lock lock(hub_.mutex);
                follower = hub_.reader && hub_.reader != this;
                if (follower) hub_.arrived.wait_for(lock, 100ms, [&] { return pending_.size() >= needed; });
                if (pending_.size() >= needed) return take(frame_buffer, needed);
            }
            // A reader that sends nothing for a while is treated like a lost channel: silence, and a new try later.
            if (follower) {
                if (std::chrono::steady_clock::now() - start > 500ms) return silence(frame_buffer);
                continue;
            }
            if (open_ && generation_ != shared_->generation()) {
                set_open(false);  // session was reopened
                surround_failed_ = false;  // a new session may allow type 3 again
            }
            if (!open_) return silence(frame_buffer);
            // A stream that joined with a wider layout makes the reader open the 12-channel bed.
            if (source_channels_ == 2 && !surround_failed_ && wanted_channels() > 2 && surround_enabled()) {
                close_channel();
                if (!open_channel(wanted_channels())) return silence(frame_buffer);
            }
            const int rc = shared_->with_session(
                [&](ps_capture *s) { return ps_capture_audio_read(s, packet_.data(), packet_.size(), header_); });
            if (rc < 0) {
                BOOST_LOG(warning) << "PS5 audio: ReadAudio failed: "sv << rc << ", reopening"sv;
                if (source_channels_ == prospero::kAvcapSurroundChannels) surround_failed_ = true;
                close_channel();
                return silence(frame_buffer);
            }
            if (rc == 0) {
                // A stalled channel (no packet for a while) is treated like a lost one.
                if (std::chrono::steady_clock::now() - start > 500ms) {
                    close_channel();
                    return silence(frame_buffer);
                }
                std::this_thread::sleep_for(1ms);
                continue;
            }
            const size_t frame_bytes = static_cast<size_t>(source_channels_) * sizeof(float);
            if (static_cast<size_t>(rc) % frame_bytes != 0) {
                BOOST_LOG(warning) << "PS5 audio: packet of "sv << rc << " bytes is not "sv << source_channels_
                                   << " channels, dropped"sv;
                continue;
            }
            hand_out(reinterpret_cast<const float *>(packet_.data()), static_cast<size_t>(rc) / sizeof(float));
        }
    }

private:
    avcap_mic(std::shared_ptr<prospero::SharedCapture> shared, int channels, std::uint32_t sample_rate,
              std::uint32_t frame_size)
        : shared_(std::move(shared)), channels_(channels), frame_size_(frame_size),
          period_(std::chrono::nanoseconds(1'000'000'000LL * frame_size / sample_rate)), packet_(0xC000) {
        std::lock_guard lock(hub_.mutex);
        if (hub_.mics.empty()) hub_.meter = {};  // the first stream after a pause starts the debug line's count anew
        hub_.mics.push_back(this);
        hub_.wanted_channels = std::max(hub_.wanted_channels, channels_);
    }

    bool another_reads() {
        std::lock_guard lock(hub_.mutex);
        return hub_.reader && hub_.reader != this;
    }

    int wanted_channels() {
        std::lock_guard lock(hub_.mutex);
        return hub_.wanted_channels;
    }

    // Caller holds hub_.mutex.
    static int wanted_channels_locked() {
        int wanted = 2;
        for (avcap_mic *mic : hub_.mics) wanted = std::max(wanted, mic->channels_);
        return wanted;
    }

    bool surround_enabled() {
        std::lock_guard lock(hub_.mutex);
        return hub_.surround_enabled;
    }

    void set_open(bool open) {
        open_ = open;
        std::lock_guard lock(hub_.mutex);
        if (open) {
            hub_.reader = this;
            pending_.clear();
        } else if (hub_.reader == this) {
            hub_.reader = nullptr;
            hub_.arrived.notify_all();
        }
    }

    // Every microphone gets the packet, in its own layout. One that falls behind keeps the last 200 ms, so its stream
    // doesn't lag.
    void hand_out(const float *samples, size_t count) {
        std::lock_guard lock(hub_.mutex);
        const int source = hub_.source_channels;
        const size_t frames = count / static_cast<size_t>(source);
        hub_.meter.read(samples, frames, source);
        if (source == prospero::kAvcapSurroundChannels) log_levels(samples, frames);
        for (avcap_mic *mic : hub_.mics) {
            const size_t most = 48000 / 5 * static_cast<size_t>(mic->channels_);
            prospero::map_channels(samples, source, frames, mic->channels_, mic->pending_);
            if (mic->pending_.size() > most)
                mic->pending_.erase(mic->pending_.begin(),
                                    mic->pending_.begin() + static_cast<std::ptrdiff_t>(mic->pending_.size() - most));
        }
        hub_.arrived.notify_all();
    }

    // Caller holds hub_.mutex. pending_ is in this microphone's layout already.
    capture_e take(std::vector<float> &frame_buffer, size_t needed) {
        next_ = std::chrono::steady_clock::now();
        frame_buffer.assign(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(needed));
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(needed));
        hub_.meter.delivered(frame_size_);
        log_meter(next_);
        return capture_e::ok;
    }

    // Caller holds hub_.mutex. Every stream's audio thread comes by here, with sound or with silence, so the line also
    // comes while the channel is lost.
    static void log_meter(std::chrono::steady_clock::time_point now) {
        if (const auto line = hub_.meter.line(now, static_cast<int>(hub_.mics.size()))) BOOST_LOG(debug) << *line;
    }

    // Caller holds hub_.mutex.
    static void log_levels(const float *samples, size_t frames) {
        constexpr int n = prospero::kAvcapSurroundChannels;
        for (size_t i = 0; i < frames; ++i)
            for (int c = 0; c < n; ++c) hub_.squares[c] += static_cast<double>(samples[i * n + c]) * samples[i * n + c];
        hub_.frames += frames;
        const auto now = std::chrono::steady_clock::now();
        if (now < hub_.levels_at) return;
        if (hub_.levels_at != std::chrono::steady_clock::time_point{} && hub_.frames) {
            std::string line;
            for (int c = 0; c < n; ++c) {
                char level[16];
                std::snprintf(level, sizeof level, " %.4f", std::sqrt(hub_.squares[c] / static_cast<double>(hub_.frames)));
                line += level;
            }
            BOOST_LOG(info) << "PS5 audio: 12-channel levels (RMS, channel 0 to 11):"sv << line;
        }
        hub_.squares = {};
        hub_.frames = 0;
        hub_.levels_at = now + 30s;
    }

    bool open_channel(int wanted) {
        // The generation is read under the lock that opens the channel. Read before it, a reopen in between (a game
        // starting) left the channel on the new session marked with the old generation: the next sample() then dropped
        // it without closing it, and every later open failed on the channel already there.
        unsigned generation = 0;
        int source = 2, surround_rc = 0;
        const bool try_surround = surround_enabled() && wanted > 2 && !surround_failed_;
        const int rc = shared_->exclusive([&](ps_capture *s) {
            generation = shared_->generation();
            if (try_surround) {
                surround_rc = ps_capture_audio_open(s, 3, 0, 0);
                if (!surround_rc) {
                    source = prospero::kAvcapSurroundChannels;
                    return 0;
                }
            }
            return ps_capture_audio_open(s, 0, 0, 0);
        });
        if (rc) {
            BOOST_LOG(warning) << "PS5 audio: OpenAudio failed: "sv << rc;
            return false;
        }
        if (try_surround && surround_rc) {
            surround_failed_ = true;
            BOOST_LOG(warning) << "PS5 audio: 12 channels refused ("sv << surround_rc << "), stereo"sv;
        } else if (try_surround) {
            BOOST_LOG(info) << "PS5 audio: 12 channels from the console for "sv << channels_ << " speakers"sv;
        }
        source_channels_ = source;
        {
            std::lock_guard lock(hub_.mutex);
            hub_.source_channels = source;
            hub_.levels_at = {};
        }
        generation_ = generation;
        set_open(true);
        return true;
    }

    void close_channel() {
        // After a reopen the channel died with the old session; closing would hit the new one. Checked under the lock,
        // so a reopen can't slip in between.
        shared_->exclusive([&](ps_capture *s) { return generation_ == shared_->generation() ? ps_capture_audio_close(s) : 0; });
        set_open(false);
    }

    capture_e silence(std::vector<float> &frame_buffer) {
        if (std::chrono::steady_clock::now() >= retry_at_ && !another_reads()) {
            // Only the session video opened: if video failed too, the stream is ending, and opening one here would hold
            // up its teardown.
            if (!shared_) shared_ = prospero::SharedCapture::attach();
            if (shared_ && open_channel(wanted_channels())) BOOST_LOG(info) << "PS5 audio: channel reopened"sv;
            retry_at_ = std::chrono::steady_clock::now() + 500ms;
        }
        {
            // What arrives during the silence would only play late.
            std::lock_guard lock(hub_.mutex);
            pending_.clear();
            hub_.meter.filled(frame_size_);
            log_meter(std::chrono::steady_clock::now());
        }
        next_ = std::max(next_ + period_, std::chrono::steady_clock::now() - period_);
        std::this_thread::sleep_until(next_);
        frame_buffer.assign(static_cast<size_t>(frame_size_) * channels_, 0.0f);
        return capture_e::ok;
    }

    static inline audio_hub hub_;
    std::shared_ptr<prospero::SharedCapture> shared_;
    int channels_;
    int source_channels_ = 2;  // of the channel this microphone opened: 2 (type 0) or 12 (type 3)
    bool surround_failed_ = false;  // type 3 was refused or failed; don't retry until a new session
    std::uint32_t frame_size_;
    std::chrono::nanoseconds period_;
    std::vector<uint8_t> packet_;
    uint8_t header_[0x10] = {};
    std::vector<float> pending_;  // guarded by hub_.mutex
    bool open_ = false;
    unsigned generation_ = 0;
    std::chrono::steady_clock::time_point next_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point retry_at_{};
};

class silent_audio_control final : public audio_control_t {
public:
    int set_sink(const std::string &) override { return 0; }
    std::unique_ptr<mic_t> microphone(const std::uint8_t *, int channels, std::uint32_t sample_rate,
                                      std::uint32_t frame_size, bool, bool) override {
        if (sample_rate == 48000) {
            // The app's "Surround 5.1/7.1" (off by default). Sunshine doesn't know the key and leaves it alone.
            const auto settings = config::parse_config(file_handler::read_file(config::sunshine.config_file.c_str()));
            const auto setting = settings.find("surround_sound");
            const bool surround = setting != settings.end() && setting->second == "1";
            if (auto mic = avcap_mic::open(channels, sample_rate, frame_size, surround)) {
                BOOST_LOG(info) << "PS5 audio: game audio via Avcap2, "sv << channels << " channels, "sv << frame_size
                                << " frames per packet"sv;
                return mic;
            }
        }
        BOOST_LOG(warning) << "PS5 audio: sending silence (rate "sv << sample_rate << ')';
        return std::make_unique<silent_mic>(channels, sample_rate, frame_size);
    }
    bool is_sink_available(const std::string &) override { return true; }
    std::optional<sink_t> sink_info() override {
        sink_t sink;
        sink.host = "ps5";
        return sink;
    }
};
}

std::unique_ptr<audio_control_t> audio_control() { return std::make_unique<silent_audio_control>(); }

}
