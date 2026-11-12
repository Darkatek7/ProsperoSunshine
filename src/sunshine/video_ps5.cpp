// SPDX-License-Identifier: GPL-3.0-only
// Replaces Sunshine's video.cpp. Avcap2 already delivers H.264 or HEVC, so there is no encoder, display or
// colour conversion: frames from CaptureSession go straight into the stream's packet queue.
#include "sunshine/video_ps5.hpp"
#include "capture/capture_session.hpp"
#include "platform/ps5/capture.h"
#include "video/capture_mode.hpp"
#include "video/channel_choice.hpp"
#include "video/frame_pacing.hpp"
#include "video/hdr10_metadata.hpp"
#include "video/keyframe_policy.hpp"
#include "src/config.h"
#include "src/globals.h"
#include "src/input.h"
#include "src/logging.h"
#include "src/rtsp.h"
#include "src/stream.h"
#include "src/video.h"
#include <x86intrin.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std::literals;

namespace video {
// 4:2:0 only. In Sunshine 1 means "disabled", 2 "Main" and 3 "Main and Main 10"; 0 would still advertise
// HEVC/AV1 in the RTSP DESCRIBE reply. Set in probe_encoders.
int active_hevc_mode = 1;
int active_av1_mode = 1;
bool last_encoder_probe_supported_ref_frames_invalidation = false;
std::array<bool, 3> last_encoder_probe_supported_yuv444_for_codec = {false, false, false};
std::array<bool, 3> last_encoder_probe_supported_dynamic_range_for_codec = {false, false, false};
std::array<bool, 3> last_encoder_probe_supported_dynamic_range_yuv444_for_codec = {false, false, false};

namespace {
// TSC ticks per steady_clock nanosecond, calibrated once; Avcap2 stamps frames with the TSC
// (descriptor word 9, docs/avcap2-descriptor.md).
double tsc_per_ns() {
    static const double ratio = [] {
        const auto t0 = std::chrono::steady_clock::now();
        const uint64_t c0 = __rdtsc();
        std::this_thread::sleep_for(100ms);
        const uint64_t c1 = __rdtsc();
        const auto t1 = std::chrono::steady_clock::now();
        return static_cast<double>(c1 - c0) / std::chrono::duration<double, std::nano>(t1 - t0).count();
    }();
    return ratio;
}

std::chrono::steady_clock::time_point capture_time(const prospero::CaptureSession::Descriptor &desc,
                                                   std::chrono::steady_clock::time_point read_time, bool hevc) {
    uint64_t stamp;
    std::memcpy(&stamp, desc.data() + (hevc ? 0x58 : 0x48), sizeof stamp);
    const uint64_t now = __rdtsc();
    if (!stamp || stamp > now) return read_time;
    const auto age = std::chrono::nanoseconds(static_cast<int64_t>((now - stamp) / tsc_per_ns()));
    return age < 1s ? read_time - age : read_time;
}

// The HEVC setup is measured on firmware 13.00 and 10.20 only. Elsewhere a client that prefers HEVC would lose the stream
// if it fails, while H.264 may still run, so it takes a switch in the app, and a function that fails is off until the
// host restarts: the client's next stream sees that here (serverinfo and RTSP read these for each stream).
// hevc_mode = 1 in sunshine.conf turns it off, 2 turns off HDR.
// A device that wants another codec or dynamic range than the running streams gets a capture channel of its own while one
// is free, so then everything is offered. With both channels busy it has to join one and gets its picture, so only their
// codecs and dynamic ranges are offered: Moonlight then picks one of them. It can't be made to pick HEVC, so a client set
// to H.264 is still turned away while both channels run HEVC.
static_assert(prospero::capture_channels == PS_CAPTURE_SLOTS);
std::atomic_int running_format[PS_CAPTURE_SLOTS] = {-1, -1};  // each channel's videoFormat, -1 while no stream holds it
std::atomic_bool running_hdr[PS_CAPTURE_SLOTS] = {false, false};
void update_offers() {
    const int hevc_mode = config::video.hevc_mode;
    const int allowed = hevc_mode == 1 || !ps_capture_hevc_allowed() ? 1 : hevc_mode == 2 || !ps_capture_hdr_allowed() ? 2 : 3;
    prospero::Channels running{};
    for (int slot = 0; slot < PS_CAPTURE_SLOTS; slot++) {
        const int format = running_format[slot];
        running[slot] = {format >= 0, format, running_hdr[slot], 0};
    }
    active_hevc_mode = prospero::offered_hevc_mode(allowed, running);
    last_encoder_probe_supported_dynamic_range_for_codec[1] = active_hevc_mode == 3;
}

// A frame for one stream's session. The session counts it until the video broadcast thread sent or dropped it, and
// stream::session::join() waits for that (patch 0026): the frame points at the session in the queue and while it is paced.
// Created only while the session's stream runs: by its own capture thread, or for a joined stream under captures.mutex
// while it is on the channel.
struct counted_packet final : packet_raw_generic {
    counted_packet(std::vector<uint8_t> &&bytes, int64_t index, bool idr, stream::session_t *session)
        : packet_raw_generic(std::move(bytes), index, idr), ref(session) {
        channel_data = session;
    }
    stream::session::video_frame_ref_t ref;
};

// A stream that joined a channel another stream holds (a second player). It lives on its own capture thread and is only
// touched under captures.mutex.
struct joined_stream {
    stream::session_t *session;
    std::string cert;
    safe::mail_raw_t::event_t<bool> shutdown;
    int64_t next_index;
    bool keyframe_seen = false;  // its frames start at an IDR
};

// One capture channel (capture.h's slots). The stream that opens it reads the frames and hands each one to the streams
// that joined since; a second session reading the same Avcap2 ring would starve the first.
struct channel_t {
    bool streaming = false;  // a stream holds the channel
    std::string cert;
    safe::mail_raw_t::event_t<bool> shutdown;
    int video_format = 0;
    bool hdr = false;
    prospero::CaptureMode delivered{};
    // What the stream holding the channel opened it with. A joined stream that takes the channel over keeps it.
    prospero::CaptureMode mode{};
    bool low_rate = false;
    uint32_t requested = 0;  // bit/s
    uint32_t average = 0;    // bit/s the channel opens with
    std::vector<joined_stream *> joined;
    // Which IDR requests of the streams on the channel go to the encoder, and when (keyframe_policy.hpp). The stream that
    // reads the frames asks; the joined ones pass their requests in.
    prospero::KeyframePolicy keyframes;
    // The open session its holder left behind for a joined stream to take over without closing and reopening the channel,
    // which cost about 185 ms of picture (STATUS.md). Only between the holder's end and the takeover.
    std::unique_ptr<prospero::CaptureSession> parked;
};

// Up to two channels, each with its own mode: a second one on profile 175 runs next to the first, up to 1080p (STATUS.md).
// Streams that want the same codec, dynamic range and size share a channel: two would deliver the same stream twice.
struct {
    std::mutex mutex;
    std::condition_variable changed;  // a channel was let go, or a joined stream left
    channel_t channels[PS_CAPTURE_SLOTS];
} captures;

// Whether a stream from this device runs, holding a channel or joined. Caller holds captures.mutex.
bool streams_from(const std::string &cert) {
    if (cert.empty()) return false;
    for (const channel_t &ch : captures.channels) {
        if (ch.streaming && ch.cert == cert) return true;
        if (std::any_of(ch.joined.begin(), ch.joined.end(), [&](const joined_stream *j) { return j->cert == cert; })) return true;
    }
    return false;
}

std::string_view stream_kind(int video_format, bool hdr) {
    return video_format == 1 ? (hdr ? "HEVC HDR"sv : "HEVC"sv) : "H.264"sv;
}

// "HEVC HDR 1920x1080@60", for the log.
std::string describe(int video_format, bool hdr, const prospero::CaptureMode &mode, bool low_rate) {
    return std::string(stream_kind(video_format, hdr)) + ' ' + std::to_string(mode.width) + 'x' + std::to_string(mode.height) + '@' +
           (low_rate ? "30" : "60");
}

std::string describe(const channel_t &ch) { return describe(ch.video_format, ch.hdr, ch.delivered, ch.low_rate); }

// A stream as the keyframe policy sees it: its control connection's round trip now (the control thread keeps it), what it
// asked for, and whether it streams over the internet (Moonlight's smaller packets then).
prospero::KeyframePolicy::Client keyframe_client(stream::session_t *session, const config_t &config) {
    prospero::KeyframePolicy::Client client;
    client.id = reinterpret_cast<std::uintptr_t>(session);
    client.kbps = config.bitrate > 0 ? static_cast<std::uint32_t>(config.bitrate) : 0;
    client.fps = config.framerate > 0 && config.framerate <= 30 ? 30 : 60;  // what the console delivers, as the pacer has it
    if (session) {
        client.rtt = std::chrono::milliseconds(stream::session::rtt_ms(*session));
        client.remote = prospero::remote_packet_size(stream::session::packet_size(*session));
    }
    client.pacing = prospero::PacingSettings{config::ps5.internet_pace_percent, config::ps5.internet_burst_kb}.checked();
    return client;
}

// About what an IDR of `bytes` takes on the wire: Sunshine's FEC share and some 4% of packet headers.
std::size_t wire_bytes(std::size_t bytes) {
    const int fec = std::clamp(config::stream.fec_percentage, 0, 255);
    return bytes + bytes * static_cast<std::size_t>(fec) / 100 + bytes / 25;
}

// At most every 10 s, and only for a stream that keeps asking: what the keyframe policy did. Caller holds captures.mutex.
void log_keyframe_policy(int slot, channel_t &ch, std::chrono::steady_clock::time_point now,
                         std::chrono::steady_clock::time_point &last_logged) {
    if (now - last_logged < 10s) return;
    last_logged = now;
    const auto counts = ch.keyframes.take_counts();
    // A stream that loses packets: at least one request a second, or one that crossed an IDR.
    if (counts.blind == 0 && counts.requests < 10) return;
    BOOST_LOG(info) << "PS5 capture: channel "sv << slot << ", "sv << counts.requests << " keyframe requests in 10 s: "sv
                    << counts.blind << " likely sent before the client could have the last IDR (answered "sv
                    << prospero::KeyframePolicy::blind_hold.count() << " ms later unless one came), "sv << counts.merged
                    << " answered by one on its way; "sv << counts.asked << " IDRs asked for"sv;
}

// The channels as choose_channel sees them. Caller holds captures.mutex.
prospero::Channels channel_states() {
    prospero::Channels states{};
    for (int slot = 0; slot < PS_CAPTURE_SLOTS; slot++) {
        const channel_t &ch = captures.channels[slot];
        // A parked session still holds the channel in its mode: a new stream joins it and may take it over.
        states[slot] = {ch.streaming || ch.parked != nullptr, ch.video_format, ch.hdr, ch.delivered.height};
    }
    return states;
}
}

std::optional<bool> hdr_conflict(bool hdr) {
    std::lock_guard lock(captures.mutex);
    return prospero::hdr_conflict(channel_states(), hdr);
}

std::vector<running_capture_t> running_captures() {
    std::lock_guard lock(captures.mutex);
    std::vector<running_capture_t> result;
    for (int slot = 0; slot < PS_CAPTURE_SLOTS; slot++) {
        const channel_t &ch = captures.channels[slot];
        if (!ch.streaming) continue;
        running_capture_t capture;
        capture.channel = slot;
        capture.cert = ch.cert;
        for (const auto *j : ch.joined) capture.joined.push_back(j->cert);
        capture.video_format = ch.video_format;
        capture.hdr = ch.hdr;
        capture.width = ch.delivered.width;
        capture.height = ch.delivered.height;
        capture.fps = ch.low_rate ? 30 : 60;
        capture.average = ch.average;
        result.push_back(std::move(capture));
    }
    return result;
}

int probe_encoders() {
    tsc_per_ns();
    update_offers();
    BOOST_LOG(info) << "PS5: Avcap2 passthrough, "sv
                    << (active_hevc_mode == 3   ? "H.264, HEVC and HEVC HDR, "sv
                        : active_hevc_mode == 2 ? "H.264 and HEVC, "sv
                                                : "H.264, "sv)
                    << (ps_capture_small_modes_allowed() ? "640x360 to "sv : ps_capture_large_modes_allowed() ? "up to "sv : ""sv)
                    << (ps_capture_large_modes_allowed() ? "3840x2160, "sv : "1920x1080, "sv) << "TSC "sv
                    << tsc_per_ns() << " ticks/ns"sv;
    return 0;
}

void capture(safe::mail_t mail, config_t config, stream::session_t *channel_data) {
    auto shutdown_event = mail->event<bool>(mail::shutdown);
    auto packets = mail::man->queue<packet_t>(mail::video_packets);
    auto idr_events = mail->event<bool>(mail::idr);
    auto touch_port_event = mail->event<input::touch_port_t>(mail::touch_port);
    auto hdr_event = mail->event<hdr_info_t>(mail::hdr);

    // Avcap2 encodes at 30 or 60 fps. On 13.00 and 10.20 360p, 540p and 720p start as well (STATUS.md), so a client that
    // asks for less gets less to receive and decode.
    const bool hevc = config.videoFormat == 1;
    const bool low_rate_wanted = config.framerate > 0 && config.framerate <= 30;
    const auto wanted =
        prospero::choose_capture_mode(config.height, ps_capture_large_modes_allowed(), ps_capture_small_modes_allowed());
    BOOST_LOG(info) << "PS5 capture requested "sv << config.width << 'x' << config.height << '@' << config.framerate
                    << ", delivering "sv << wanted.width << 'x' << wanted.height << '@' << (low_rate_wanted ? 30 : 60);
    // What this stream gets: its own request, or the mode of the channel it joined or took over.
    auto mode = wanted;
    bool low_rate = low_rate_wanted;
    int delivered_height = 0;
    const auto announce_mode = [&](const prospero::CaptureMode &delivered) {
        if (delivered.height == delivered_height) return;
        delivered_height = delivered.height;
        input::touch_port_t port{};
        port.width = port.logical_width = delivered.width;
        port.height = port.logical_height = delivered.height;
        touch_port_event->raise(port);
    };
    announce_mode(mode);
    // Avcap2 reports no mastering metadata, and the stream says only BT.2020 and PQ in its SPS. Zeros reach the client's
    // display as zero primaries and a zero peak, so an HDR stream announces the usual HDR10 values (hdr10_metadata.hpp).
    const bool hdr = hevc && config.dynamicRange > 0 && ps_capture_hdr_allowed();
    if (hdr) {
        constexpr auto values = prospero::hdr10_default_metadata();
        SS_HDR_METADATA metadata{};
        for (int i = 0; i < 3; i++) {
            metadata.displayPrimaries[i].x = values.primaries[i].x;
            metadata.displayPrimaries[i].y = values.primaries[i].y;
        }
        metadata.whitePoint.x = values.white_point.x;
        metadata.whitePoint.y = values.white_point.y;
        metadata.maxDisplayLuminance = values.max_display_luminance;
        metadata.minDisplayLuminance = values.min_display_luminance;
        metadata.maxContentLightLevel = values.max_content_light_level;
        metadata.maxFrameAverageLightLevel = values.max_frame_average_light_level;
        metadata.maxFullFrameLuminance = values.max_full_frame_luminance;
        hdr_event->raise(std::make_unique<hdr_info_raw_t>(true, metadata));
    } else {
        hdr_event->raise(std::make_unique<hdr_info_raw_t>(false));
    }
    int slot = 0;  // the channel this stream holds or joined
    // The session falls back to 1080p when the system refuses a larger or smaller mode.
    const auto check_fallback = [&] {
        const auto delivered = prospero::capture_mode_of(ps_capture_slot_video_resolution(slot));
        if (delivered.height != delivered_height) {
            if (delivered.height == mode.height)
                BOOST_LOG(info) << "PS5 capture: back to "sv << delivered.width << 'x' << delivered.height;
            else
                BOOST_LOG(warning) << "PS5 capture: "sv << (delivered_height > delivered.height ? "larger"sv : "smaller"sv)
                                   << " mode refused by the system, delivering "sv << delivered.width << 'x' << delivered.height;
        }
        announce_mode(delivered);
        std::lock_guard lock(captures.mutex);
        captures.channels[slot].delivered = delivered;  // for the streams that join
    };

    // A new stream from a device that streams already ends its old one and takes its place: a client that retries, like
    // Moonlight Web when its browser didn't get the picture, would otherwise wait until the old session timed out.
    // A stream from another device joins a running channel with its codec, dynamic range and size (a second player) and
    // gets its picture, or gets a channel of its own (choose_channel). When the stream holding a channel ends, a stream
    // that joined it takes it over.
    const std::string cert = channel_data ? stream::session::client_cert(*channel_data) : std::string{};
    int64_t next_index = 0;  // per stream, across a takeover of the capture
    std::unique_lock lock(captures.mutex);
    if (streams_from(cert)) {
        BOOST_LOG(info) << "PS5 capture: the same device started a new stream, ending its old one"sv;
        for (channel_t &ch : captures.channels) {
            if (ch.streaming && ch.cert == cert) ch.shutdown->raise(true);
            for (auto *j : ch.joined)
                if (j->cert == cert) j->shutdown->raise(true);
        }
        captures.changed.wait_for(lock, 5s, [&] { return !streams_from(cert); });
        if (streams_from(cert)) {
            BOOST_LOG(warning) << "PS5 capture: the old stream did not end; ending the new one"sv;
            shutdown_event->raise(true);
            return;
        }
    }
    bool second_usable = true;  // false once the second channel failed to open for this stream
    for (;;) {
        if (!lock.owns_lock()) lock.lock();
        // Which channel: choose_channel (channel_choice.hpp) has the rules.
        const auto pick = prospero::choose_channel(channel_states(), config.videoFormat, hdr, wanted, second_usable);
        if (pick.kind == prospero::ChannelPick::refuse) {
            std::string running;
            for (const channel_t &ch : captures.channels)
                if (ch.streaming) running += (running.empty() ? "" : ", ") + describe(ch);
            BOOST_LOG(warning) << "PS5 capture busy with "sv << running << "; this device asked for "sv
                               << stream_kind(config.videoFormat, hdr) << ", ending its stream"sv;
            shutdown_event->raise(true);
            return;
        }
        slot = pick.channel;
        channel_t &ch = captures.channels[slot];
        bool took_over = false;
        if (pick.kind == prospero::ChannelPick::join) {
            joined_stream me{channel_data, cert, shutdown_event, next_index};
            ch.joined.push_back(&me);
            ch.keyframes.force();
            // That keyframe usually goes out before the client's first video ping, when Sunshine doesn't know where to send
            // it yet; the client then drops every frame until it asks for one itself, after 2 s. Two more, after 0.25 s and
            // 1 s, reach a client on the LAN and one that connects slower (measured: the first picture after 0.6 s instead of
            // 2.2 s with one more at 0.5 s).
            const auto joined_at = std::chrono::steady_clock::now();
            int late_keyframes = 0;
            BOOST_LOG(info) << "PS5 capture: another device joined the running stream on channel "sv << slot << " ("sv
                            << ch.joined.size() + 1 << " streams there)"sv;
            if (ch.delivered.height != wanted.height)
                BOOST_LOG(info) << "PS5 capture: the running stream delivers "sv << ch.delivered.width << 'x' << ch.delivered.height;
            // Another joined stream may take the channel over when its holder ends; this one then goes on with it.
            while (!shutdown_event->peek() && ch.streaming) {
                announce_mode(ch.delivered);
                const auto now = std::chrono::steady_clock::now();
                if (late_keyframes < 2 && now - joined_at >= (late_keyframes ? 1000ms : 250ms)) {
                    ++late_keyframes;
                    ch.keyframes.force();
                }
                if (idr_events->peek()) {
                    idr_events->pop();
                    ch.keyframes.request(now, keyframe_client(channel_data, config));
                }
                captures.changed.wait_for(lock, 20ms);
            }
            std::erase(ch.joined, &me);
            ch.keyframes.forget(reinterpret_cast<std::uintptr_t>(channel_data));
            next_index = me.next_index;
            captures.changed.notify_all();
            if (shutdown_event->peek()) {
                // The last one waiting closes a session that its holder left for a takeover.
                if (!ch.streaming && ch.joined.empty() && ch.parked) {
                    ch.parked->stop();
                    ch.parked.reset();
                    BOOST_LOG(info) << "PS5 capture: closed channel "sv << slot << ", nobody took it over"sv;
                    lock.unlock();
                    captures.changed.notify_all();
                }
                return;
            }
            BOOST_LOG(info) << "PS5 capture: the stream holding channel "sv << slot << " ended, this one takes it over"sv;
            took_over = true;
        }
        // config.bitrate is in kbit/s. The channel opens with its bitrate in the config: raising it later above the
        // opening value stops the encoder (docs/re/hevc.md). Peak as Remote Play: H.264 average * 1.12, HEVC * 1.10.
        // Above 50 Mbit/s is measured on firmware 13.00 only (docs/re/hevc.md); elsewhere it takes a switch in the app.
        const uint32_t default_average = 6'000'000, max_average = ps_capture_high_bitrate_allowed() ? 150'000'000 : 50'000'000;
        uint32_t requested = config.bitrate > 0 ? static_cast<uint32_t>(config.bitrate) * 1000u : default_average;
        mode = wanted;
        low_rate = low_rate_wanted;
        // A takeover keeps the running mode: the others keep the picture they had, and a friend on a slow link gets no more
        // than before.
        if (took_over) {
            mode = ch.mode;
            low_rate = ch.low_rate;
            requested = ch.requested;
            BOOST_LOG(info) << "PS5 capture: keeping the running mode "sv << mode.width << 'x' << mode.height << '@'
                            << (low_rate ? 30 : 60) << ", "sv << requested / 1000 << " kbit/s"sv;
        } else if (slot == 1 && mode.resolution > prospero::mode_1080p.resolution) {
            // Profile 175 grants up to 1080p.
            mode = prospero::mode_1080p;
            BOOST_LOG(info) << "PS5 capture: the second channel delivers up to 1920x1080"sv;
        }
        announce_mode(mode);
        const uint32_t average = std::min({requested, max_average, prospero::max_average_of(mode, low_rate)});
        ch.mode = mode;
        ch.low_rate = low_rate;
        ch.requested = requested;
        ch.average = average;
        ch.streaming = true;
        ch.cert = cert;
        ch.shutdown = shutdown_event;
        ch.video_format = config.videoFormat;
        ch.hdr = hdr;
        ch.delivered = mode;
        // The session the last holder left open, when this stream takes the channel over in its mode.
        std::unique_ptr<prospero::CaptureSession> session;
        if (took_over) session = std::move(ch.parked);
        const bool handed_over = session != nullptr;
        // A channel that opens anew starts its keyframe policy anew; one taken over keeps what it learnt about the
        // streams on it.
        if (!handed_over) ch.keyframes = {};
        running_hdr[slot] = hdr;
        running_format[slot] = config.videoFormat;
        update_offers();
        std::string opening;
        if (slot == 1) {
            opening = "PS5 capture: second channel on profile 175 for " + describe(ch) + ", " + std::to_string(average / 1000) + " kbit/s";
            if (captures.channels[0].streaming) opening += ", next to " + describe(captures.channels[0]);
        }
        lock.unlock();
        const auto release = util::fail_guard([slot] {
            {
                std::lock_guard lock(captures.mutex);
                channel_t &held = captures.channels[slot];
                held.streaming = false;
                held.shutdown.reset();
                running_format[slot] = -1;
                // The joined streams left while the session was being parked for them. It closes before the channel
                // counts as free, so a new stream can open it.
                if (held.joined.empty() && held.parked) {
                    held.parked->stop();
                    held.parked.reset();
                    BOOST_LOG(info) << "PS5 capture: closed channel "sv << slot << ", nobody took it over"sv;
                }
                update_offers();
            }
            captures.changed.notify_all();
        });
        if (handed_over) {
            // Same mode, same encoder: the frames go on where the last holder stopped reading them, and this stream has
            // had its keyframe while it was joined.
            {
                std::lock_guard lock(captures.mutex);
                ch.delivered = prospero::capture_mode_of(ps_capture_slot_video_resolution(slot));
                ch.average = ps_capture_slot_opening_average(slot);
            }
            update_offers();
            BOOST_LOG(info) << "PS5 capture: took the open channel "sv << slot << " over, no reopen"sv;
        } else {
            if (!opening.empty()) BOOST_LOG(info) << opening;
            ps_capture_slot_set_opening_bitrate(slot, average, average / 100 * (hevc ? 110 : 112));
            ps_capture_slot_set_codec(slot, hevc ? 1 : 0);
            ps_capture_slot_set_ten_bit(slot, hdr);
            ps_capture_slot_set_video_mode(slot, mode.resolution, low_rate ? 0 : 1);
            const auto codec = hevc ? prospero::Codec::hevc : prospero::Codec::h264;
            session = std::make_unique<prospero::CaptureSession>(prospero::make_ps5_capture_backend(codec, slot), codec);
            const int start_rc = session->start();
            update_offers();
            if (start_rc) {
                if (slot == 1) {
                    // The first channel may still take this stream: a channel with its codec and dynamic range to join, or
                    // the first one when it is free.
                    BOOST_LOG(warning) << "PS5 capture: the second channel failed to open: "sv << start_rc << ", trying the first"sv;
                    second_usable = false;
                    continue;
                }
                BOOST_LOG(error) << "PS5 capture failed to start: "sv << start_rc;
                shutdown_event->raise(true);
                return;
            }
            check_fallback();
            {
                std::lock_guard lock(captures.mutex);
                ch.average = ps_capture_slot_opening_average(slot);
            }
            BOOST_LOG(info) << "PS5 capture "sv << (hdr ? "HEVC HDR"sv : hevc ? "HEVC"sv : "H.264"sv)
                            << (slot ? " (second channel)"sv : ""sv) << " bitrate "sv << ps_capture_slot_opening_average(slot) / 1000
                            << " kbit/s (requested "sv << requested / 1000 << ')';
            if (const auto asked = keyframe_client(channel_data, config); asked.remote) {
                const auto pacing = asked.pacing;
                BOOST_LOG(info) << "PS5 video: stream over the internet, up to "sv << pacing.burst_kb
                                << " kB of a frame at once, the rest at "sv << pacing.pace_percent
                                << "% of the stream's rate (internet_burst_kb, internet_pace_percent)"sv;
            }
            // The audio thread may have opened the channel earlier, and the next periodic IDR can be ten seconds away.
            std::lock_guard lock(captures.mutex);
            ch.keyframes.force();
        }
        prospero::EncodedFrame frame;
        prospero::CaptureSession::Descriptor desc{};
        bool lost = false;  // the session could not be reopened: nothing to hand over
        uint32_t qp_coarser_logged = 0;
        auto keyframes_logged = std::chrono::steady_clock::now();
        // Moonlight asks for an IDR after loss; Avcap2 delivers one within 2-3 frames (R1). Every IDR is a large frame for
        // every stream on the channel, so the keyframe policy decides which requests reach the encoder, and when.
        while (!shutdown_event->peek()) {
            const auto now = std::chrono::steady_clock::now();
            std::optional<prospero::KeyframePolicy::Client> asking;
            if (idr_events->peek()) {
                idr_events->pop();
                asking = keyframe_client(channel_data, config);
            }
            bool ask = false;
            {
                std::lock_guard lock(captures.mutex);
                if (asking) ch.keyframes.request(now, *asking);
                ask = ch.keyframes.poll(now);
                log_keyframe_policy(slot, ch, now, keyframes_logged);
            }
            if (ask && session->request_keyframe()) {
                BOOST_LOG(warning) << "PS5 capture: keyframe request failed, next periodic IDR within ten seconds"sv;
                std::lock_guard lock(captures.mutex);
                ch.keyframes.ask_failed();
            }
            const auto status = session->read(frame, 100ms, {}, &desc);
            if (status == prospero::CaptureStatus::timeout) continue;
            if (status != prospero::CaptureStatus::frame) {
                // Starting or closing a game takes the capture resource away for a moment; reopen
                // instead of ending the stream. The first frame after a reopen is an IDR.
                BOOST_LOG(warning) << "PS5 capture lost ("sv << static_cast<int>(status) << "), reopening"sv;
                const auto lost_at = std::chrono::steady_clock::now();
                bool recovered = false, size_refused = false;
                // The stream keeps the size it delivers: the client set up its decoder for it (issue 39). The title that
                // took the capture leaves the output without a mode for up to about 2 s (10.20), and the reopens wait for
                // it. An output that no longer takes the mode ends the stream, so the client starts anew at what it takes.
                ps_capture_slot_set_reopening(slot, 1);
                while (!shutdown_event->peek() && std::chrono::steady_clock::now() - lost_at < 20s) {
                    std::this_thread::sleep_for(250ms);
                    if (session->start() == 0) {
                        recovered = true;
                        break;
                    }
                    if ((size_refused = ps_capture_slot_size_refused(slot))) break;
                }
                ps_capture_slot_set_reopening(slot, 0);
                if (size_refused) {
                    const auto delivered = prospero::capture_mode_of(ps_capture_slot_video_resolution(slot));
                    BOOST_LOG(error) << "PS5 capture: the console's output no longer takes "sv << delivered.width << 'x'
                                     << delivered.height << ", ending the session; the next one starts at what it takes"sv;
                    shutdown_event->raise(true);
                    lost = true;
                    break;
                }
                if (!recovered) {
                    BOOST_LOG(error) << "PS5 capture could not be reopened; ending the session"sv;
                    shutdown_event->raise(true);
                    lost = true;
                    break;
                }
                BOOST_LOG(info) << "PS5 capture reopened after "sv
                                << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                                         lost_at).count()
                                << " ms"sv;
                if (const auto coarser = ps_capture_slot_qp_coarser(slot); coarser != qp_coarser_logged) {
                    qp_coarser_logged = coarser;
                    BOOST_LOG(warning) << "PS5 capture: the encoder lost the picture right after opening, QP "sv << coarser
                                       << (coarser == 1 ? " step"sv : " steps"sv) << " coarser for this stream"sv;
                }
                check_fallback();
                continue;
            }
            const auto timestamp = capture_time(desc, frame.read_time, hevc);
            {
                std::lock_guard lock(captures.mutex);
                if (frame.idr) ch.keyframes.keyframe_sent(frame.read_time, wire_bytes(frame.bytes.size()));
                for (auto *j : ch.joined) {
                    if (!j->keyframe_seen && !frame.idr) continue;
                    j->keyframe_seen = true;
                    auto copy = std::make_unique<counted_packet>(std::vector<uint8_t>(frame.bytes), j->next_index++, frame.idr, j->session);
                    copy->frame_timestamp = timestamp;
                    packets->raise(std::move(copy));
                }
            }
            auto packet = std::make_unique<counted_packet>(std::move(frame.bytes), next_index++, frame.idr, channel_data);
            packet->frame_timestamp = timestamp;
            packets->raise(std::move(packet));
            frame.bytes = {};
        }
        // A joined stream takes the channel over: it gets the open session and goes on reading. Otherwise the channel
        // closes before release lets it go, so a new stream can open it.
        {
            std::lock_guard lock(captures.mutex);
            ch.keyframes.forget(reinterpret_cast<std::uintptr_t>(channel_data));
            if (!lost && !ch.joined.empty()) {
                ch.parked = std::move(session);
                BOOST_LOG(info) << "PS5 capture: leaving channel "sv << slot << " open for the stream that takes it over"sv;
            }
        }
        if (session) session->stop();
        return;
    }
}
}
