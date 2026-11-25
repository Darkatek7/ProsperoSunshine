// SPDX-License-Identifier: GPL-3.0-only
// Replaces Sunshine's video.cpp. Avcap2 already delivers H.264 or HEVC, so there is no encoder, display or
// colour conversion: frames from CaptureSession go straight into the stream's packet queue.
#include "sunshine/video_ps5.hpp"
#include "capture/capture_session.hpp"
#include "capture/output_mode.h"
#include "control/device_follow_size.hpp"
#include "platform/ps5/capture.h"
#include "video/capture_mode.hpp"
#include "video/channel_choice.hpp"
#include "video/descriptor_clock.hpp"
#include "video/frame_pacing.hpp"
#include "video/hdr10_metadata.hpp"
#include "video/hevc_refs.hpp"
#include "video/keyframe_policy.hpp"
#include "video/loss_recovery.hpp"
#include "src/config.h"
#include "src/globals.h"
#include "src/input.h"
#include "src/logging.h"
#include "src/nvhttp.h"
#include "src/platform/common.h"
#include "src/rtsp.h"
#include "src/stream.h"
#include "src/video.h"
#include <x86intrin.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <span>
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

// The encoder's count of the frame a descriptor came with, from 1 in each session (docs/avcap2-descriptor.md).
uint32_t frame_counter(const prospero::CaptureSession::Descriptor &desc, bool hevc) {
    uint32_t counter;
    std::memcpy(&counter, desc.data() + (hevc ? 0x30 : 0x20), sizeof counter);
    return counter;
}

// Why the output took no picture of this size, when its mode says so: a game set it to 120 Hz, where a PS5 takes no 1440p
// or 4K (capture/output_mode.h). Turning off the console's 120 Hz output keeps 60 Hz. Empty otherwise.
std::string_view at_120hz_note(int slot, uint32_t resolution) {
    const uint32_t output = ps_capture_slot_open_output(slot);
    if (!ps_output_at_120hz(output) || ps_output_holds(output, resolution)) return {};
    return " (the console's output runs at 120 Hz, which takes no 1440p or 4K; 120 Hz output off in the console's video "
           "settings keeps 60 Hz)"sv;
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
    // Its keyframe requests. Its own thread passes them to the channel's keyframe policy every 20 ms, and the holder takes
    // one still there when it reads an IDR, which answers it. Both take them only under captures.mutex.
    safe::mail_raw_t::event_t<bool> idr;
    // The frames its client names as lost (reference_frame_invalidation). Only the holder takes them, under captures.mutex.
    safe::mail_raw_t::event_t<std::pair<int64_t, int64_t>> lost;
    const config_t *config;  // for keyframe_client
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
    // Getting over a lost frame without an IDR, where sunshine.conf turns it on and the channel's stream allows it
    // (loss_recovery.hpp): the slice header's layout, and which stream waits for which frame. They stay with the
    // encoder's session: a stream that takes the open channel over goes on with them.
    std::optional<prospero::hevc::Layout> layout;
    prospero::LossRecovery recovery;
    struct {
        int went_on = 0, keyframes = 0, held = 0;
    } losses;  // since the last log line
    // The open session its holder left behind for a joined stream to take over without closing and reopening the channel,
    // which cost about 185 ms of picture (STATUS.md). Only between the holder's end and the takeover.
    std::unique_ptr<prospero::CaptureSession> parked;
    // What the channel delivered, and opened with, before a reopen followed the output down to 1080p (device_follow_size.hpp),
    // or what a new stream asked for when the output refused it at the start (a title at 120 Hz in front). A later reopen
    // whose output takes it again goes back to it. A joined stream that takes the channel over keeps it, open or reopened; a
    // stream that opens the channel as a new one starts without, unless its own open fell back that way.
    struct followed_t {
        prospero::CaptureMode mode;
        uint32_t average;  // bit/s
    };
    std::optional<followed_t> followed_from;
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

// About what an IDR of `bytes` takes on the wire: Sunshine's FEC share (the larger one when clients over the internet have
// their own) and some 4% of packet headers.
std::size_t wire_bytes(std::size_t bytes) {
    const int fec = std::clamp(std::max(config::stream.fec_percentage, config::ps5.internet_fec_percentage), 0, 255);
    return bytes + bytes * static_cast<std::size_t>(fec) / 100 + bytes / 25;
}

// OpenVideo returned -1 (issue 56): the console's own capture path is stuck, and its recording and Remote Play fail too.
// Restarting the host doesn't free it; only a console restart did.
void log_capture_stuck() {
    BOOST_LOG(error) << "PS5 capture: the console's video capture is stuck (OpenVideo -1). Restart the console; restarting "
                        "the host doesn't help (issue 56)"sv;
}

// The streams on a channel as a reopen sees them, from their client certificates and the heights they asked for: whether
// each device follows the output's size (device_follow_size.txt, else follow_output_size in sunshine.conf). Read anew at
// each reopen, so a change on the web page counts from the next one. Called without captures.mutex.
std::vector<prospero::SizeFollower> size_followers(const std::vector<std::pair<std::string, int>> &streams) {
    const prospero::DeviceFollowSize settings(platf::appdata() / "device_follow_size.txt");
    const auto clients = nvhttp::get_all_clients();
    std::vector<prospero::SizeFollower> followers;
    for (const auto &[cert, height] : streams) {
        bool follows = config::ps5.follow_output_size;
        for (const auto &client : clients) {
            const std::string uuid = client.value("uuid", "");
            if (!uuid.empty() && nvhttp::get_cert_by_uuid(uuid) == cert) {
                follows = settings.follows(uuid, config::ps5.follow_output_size);
                break;
            }
        }
        followers.push_back({follows, height});
    }
    return followers;
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

// At most every 10 s: how the losses the clients named were answered. Caller holds captures.mutex.
void log_loss_recovery(int slot, channel_t &ch, std::chrono::steady_clock::time_point now,
                       std::chrono::steady_clock::time_point &last_logged) {
    if (now - last_logged < 10s) return;
    last_logged = now;
    const auto losses = std::exchange(ch.losses, {});
    if (losses.went_on == 0 && losses.keyframes == 0 && losses.held == 0) return;
    BOOST_LOG(info) << "PS5 capture: channel "sv << slot << ", losses the clients named in 10 s: "sv << losses.went_on
                    << " went on without a keyframe, "sv << losses.keyframes << " needed one"sv;
    // The encoder went on for one stream from a picture another stream on the channel never had.
    if (losses.held)
        BOOST_LOG(info) << "PS5 capture: channel "sv << slot << ", "sv << losses.held
                        << " times in 10 s a stream waited for a keyframe because another's loss was answered from a picture "sv
                           "it didn't have"sv;
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
    // capture.c times sceAvcap2Start by the frames' capture stamps, which count in TSC ticks (capture/start_phase.h).
    ps_capture_set_ticks_per_ms(tsc_per_ns() * 1e6);
    update_offers();
    // The client then names the frames it lost instead of asking for an IDR (capture(), loss_recovery.hpp).
    last_encoder_probe_supported_ref_frames_invalidation = config::ps5.reference_frame_invalidation;
    if (config::ps5.reference_frame_invalidation)
        BOOST_LOG(info) << "PS5: reference frame invalidation announced (reference_frame_invalidation): an HEVC stream "sv
                           "goes on after a lost frame without a keyframe where its client and the loss allow it"sv;
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
    // The frames a client names as lost instead of asking for an IDR, where the host announced that it takes them.
    auto lost_events = mail->event<std::pair<int64_t, int64_t>>(mail::invalidate_ref_frames);
    const bool rfi = config::ps5.reference_frame_invalidation;
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
    const auto check_fallback = [&](bool logged = false) {
        const auto delivered = prospero::capture_mode_of(ps_capture_slot_video_resolution(slot));
        if (delivered.height != delivered_height && !logged) {
            if (delivered.height == mode.height)
                BOOST_LOG(info) << "PS5 capture: back to "sv << delivered.width << 'x' << delivered.height;
            else
                BOOST_LOG(warning) << "PS5 capture: "sv << (delivered_height > delivered.height ? "larger"sv : "smaller"sv)
                                   << " mode refused by the system, delivering "sv << delivered.width << 'x' << delivered.height
                                   << at_120hz_note(slot, mode.resolution);
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
    // The frame numbers the client sees, per stream and across a takeover of the capture. They start at 1, as upstream's
    // do: Moonlight rejects frame 0 (its RtpVideoQueue starts at currentFrameNumber 1), and with it a stream's first IDR.
    int64_t next_index = 1;
    // This stream gets no frame until the next IDR: it joined and has had none yet, or a frame came that is decoded from
    // a picture it never had (loss_recovery.hpp).
    bool held = false;
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
            joined_stream me{channel_data, cert, shutdown_event, idr_events, lost_events, &config, next_index};
            ch.joined.push_back(&me);
            ch.keyframes.force();
            // The joined stream's frames start at that keyframe, and the client shows it: frames are numbered from 1 (in
            // the console round of 2026-11-19 every joining client started at IDR frame #1, without waiting). Two more
            // keyframes used to follow, after 0.25 s and 1 s, from when Moonlight dropped the join's keyframe as frame 0;
            // each was a larger frame for every device on the channel. One lost on the way brings a request like any loss;
            // only while none has come at all (the encoder refused the request) is it forced again, every 0.5 s.
            auto forced_at = std::chrono::steady_clock::now();
            BOOST_LOG(info) << "PS5 capture: another device joined the running stream on channel "sv << slot << " ("sv
                            << ch.joined.size() + 1 << " streams there)"sv;
            if (ch.delivered.height != wanted.height)
                BOOST_LOG(info) << "PS5 capture: the running stream delivers "sv << ch.delivered.width << 'x' << ch.delivered.height;
            // Another joined stream may take the channel over when its holder ends; this one then goes on with it.
            while (!shutdown_event->peek() && ch.streaming) {
                announce_mode(ch.delivered);
                const auto now = std::chrono::steady_clock::now();
                if (!me.keyframe_seen && now - forced_at >= 500ms) {
                    forced_at = now;
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
            ch.recovery.forget(reinterpret_cast<std::uintptr_t>(channel_data));
            next_index = me.next_index;
            held = !me.keyframe_seen;
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
        uint32_t average = 0;
        // A takeover keeps the running picture: the size the channel delivers and the rate it last opened with, so the
        // others keep the picture they had, and a friend on a slow link gets no more than before. The mode its holder
        // opened it with stays the channel's, for the next takeover.
        if (took_over) {
            mode = ch.mode;
            low_rate = ch.low_rate;
            requested = ch.requested;
            average = ch.average;
            BOOST_LOG(info) << "PS5 capture: keeping the running mode "sv << ch.delivered.width << 'x' << ch.delivered.height
                            << '@' << (low_rate ? 30 : 60) << ", "sv << average / 1000 << " kbit/s"sv;
        } else {
            if (slot == 1 && mode.resolution > prospero::mode_1080p.resolution) {
                // Profile 175 grants up to 1080p.
                mode = prospero::mode_1080p;
                BOOST_LOG(info) << "PS5 capture: the second channel delivers up to 1920x1080"sv;
            }
            average = std::min({requested, max_average, prospero::max_average_of(mode, low_rate)});
            ch.delivered = mode;
        }
        const prospero::CaptureMode opens_at = ch.delivered;
        announce_mode(opens_at);
        ch.mode = mode;
        ch.low_rate = low_rate;
        ch.requested = requested;
        ch.average = average;
        ch.streaming = true;
        ch.cert = cert;
        ch.shutdown = shutdown_event;
        ch.video_format = config.videoFormat;
        ch.hdr = hdr;
        // The session the last holder left open, when this stream takes the channel over in its mode.
        std::unique_ptr<prospero::CaptureSession> session;
        if (took_over) session = std::move(ch.parked);
        const bool handed_over = session != nullptr;
        // After an open or reopen, under captures.mutex: its IDR is on its way, unless the open read it itself.
        const auto opened = [&] {
            if (ps_capture_slot_opening_frame_read(slot)) ch.keyframes.force();
            else ch.keyframes.expect_opening_keyframe(std::chrono::steady_clock::now());
            // The encoder counts its frames anew, and the opening IDR answers what the streams lost before it.
            ch.layout = hevc && rfi ? prospero::hevc::layout(session->parameter_sets()) : std::nullopt;
            ch.recovery = prospero::LossRecovery(ch.layout ? ch.layout->most_missing() : 0);
            ch.losses = {};
            if (ch.layout)
                BOOST_LOG(info) << "PS5 capture: channel "sv << slot
                                << " goes on after a loss a client names without a keyframe, "sv
                                   "while no more than "sv
                                << ch.layout->most_missing() << " frames in a row are missing"sv;
            else if (hevc && rfi)
                BOOST_LOG(warning) << "PS5 capture: channel "sv << slot
                                   << " answers every loss with a keyframe: the host doesn't "sv
                                      "read this stream's parameter sets"sv;
        };
        // A channel that opens anew starts its keyframe policy anew; one taken over keeps what it learnt about the
        // streams on it. Only a stream that opens the channel as a new one starts without the size it followed down from
        // (its own open may set one, below).
        if (!handed_over) {
            ch.keyframes = {};
            ch.layout.reset();
            ch.recovery = prospero::LossRecovery();
        }
        if (!took_over) ch.followed_from.reset();
        running_hdr[slot] = hdr;
        running_format[slot] = config.videoFormat;
        update_offers();
        std::string opening;
        if (slot == 1 && !took_over) {
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
        const auto codec = hevc ? prospero::Codec::hevc : prospero::Codec::h264;
        bool lost = false;  // the session could not be reopened: nothing to hand over
        // A reopen changed the picture's size (device_follow_size.hpp): no frame goes out before the IDR that carries the
        // new parameter sets, so the client's decoder meets the new size there first.
        bool await_idr = false;
        uint32_t qp_coarser_logged = 0;

        // The reopen of a running picture: after a capture loss, on the way back to the size the channel followed down
        // from (go_back), and for a joined stream that takes over a channel whose session is gone. The stream keeps the size
        // it delivers: the client set up its decoder for it (issue 39). The title that took the capture leaves the output
        // without a mode for up to about 2 s (10.20), and the reopens wait for it. A settled output may refuse the mode for
        // a moment too (issue 62), so in the first 3 s after `since` that is tried again as well (ps_capture_slot_set_reopening
        // starts them). An output that still refuses the mode after them ends the stream, so the client starts anew at what
        // it takes, unless this stream's device follows the output's size (`follow`, off by default): then a 1440p or 4K
        // the output refuses goes on at 1080p (capture.c). Up to 20 s, 250 ms before each try (but the first, `at_once`).
        enum class reopen_end { recovered, size_refused, left, failed };
        const auto takeover_waits = [&] {
            std::lock_guard lock(captures.mutex);
            return !ch.joined.empty();
        };
        const auto reopen = [&](std::chrono::steady_clock::time_point since, bool follow, bool at_once) {
            auto end = reopen_end::failed;
            ps_capture_slot_set_follow_size(slot, follow);
            ps_capture_slot_set_reopening(slot, 1);
            for (bool first = true; !shutdown_event->peek() && std::chrono::steady_clock::now() - since < 20s; first = false) {
                if (!first || !at_once) std::this_thread::sleep_for(250ms);
                // The stream ended meanwhile: nobody would read what an open brings, unless a joined stream takes the
                // channel over. That one still gets this try: a session that opens is handed to it, while on its own it
                // opens the channel anew through this procedure.
                if (shutdown_event->peek() && !takeover_waits()) break;
                if (session->start() == 0) {
                    end = reopen_end::recovered;
                    break;
                }
                if (ps_capture_slot_size_refused(slot)) {
                    end = reopen_end::size_refused;
                    break;
                }
                if (ps_capture_stuck()) break;  // more tries only hold the stream up (issue 56)
            }
            ps_capture_slot_set_reopening(slot, 0);
            ps_capture_slot_set_follow_size(slot, 0);
            if (end == reopen_end::failed && shutdown_event->peek()) end = reopen_end::left;
            return end;
        };
        // A reopen that didn't recover ends the stream, and there is nothing to hand over.
        const auto reopen_failed = [&](reopen_end end) {
            lost = true;
            if (end == reopen_end::size_refused) {
                const auto refused = prospero::capture_mode_of(ps_capture_slot_video_resolution(slot));
                BOOST_LOG(error) << "PS5 capture: the console's output no longer takes "sv << refused.width << 'x'
                                 << refused.height << at_120hz_note(slot, refused.resolution)
                                 << ", ending the session; the next one starts at what it takes"sv
                                 << (ps_reopen_may_follow(1, refused.resolution)
                                         ? ". Devices set to follow the output's size (web page, Devices) go on at 1920x1080 instead"sv
                                         : ""sv);
                shutdown_event->raise(true);
            } else if (end == reopen_end::left) {
                // The stream ended while the capture was away (the client left, ENDSTREAM, a new stream from the same
                // device): nothing failed that anyone would see, so no error. A joined stream that takes the channel over
                // opens it itself, as a reopen of its running picture (with the grace and its own follow setting): the
                // single try it had before could fail on a title still starting. A stuck capture is still said, the next
                // stream fails on it as well.
                BOOST_LOG(info) << "PS5 capture: the client left during a reopen"sv;
                if (ps_capture_stuck()) log_capture_stuck();
            } else {
                BOOST_LOG(error) << "PS5 capture could not be reopened; ending the session"sv;
                if (ps_capture_stuck()) log_capture_stuck();
                shutdown_event->raise(true);
            }
        };
        const auto log_reopened = [&](std::chrono::steady_clock::time_point since) {
            BOOST_LOG(info) << "PS5 capture reopened after "sv
                            << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - since).count()
                            << " ms"sv;
            if (const auto coarser = ps_capture_slot_qp_coarser(slot); coarser != qp_coarser_logged) {
                qp_coarser_logged = coarser;
                BOOST_LOG(warning) << "PS5 capture: the encoder lost the picture right after opening, QP "sv << coarser
                                   << (coarser == 1 ? " step"sv : " steps"sv) << " coarser for this stream"sv;
            }
        };
        // Whether this stream's device follows the output's size, read anew for each reopen.
        const auto follows_size = [&] { return size_followers({{cert, config.height}}).front(); };
        // The joined streams still going on, and whether each one's device follows the output's size.
        const auto joined_followers = [&] {
            std::vector<std::pair<std::string, int>> streams;
            std::vector<joined_stream *> joined;
            {
                std::lock_guard lock(captures.mutex);
                for (auto *j : ch.joined) {
                    if (j->shutdown->peek()) continue;
                    streams.emplace_back(j->cert, j->config->height);
                    joined.push_back(j);
                }
            }
            return std::pair{joined, size_followers(streams)};
        };
        // A smaller picture ends each joined stream whose device doesn't follow the output's size: its client set up its
        // decoder for the size it had (issue 39). The ones that follow go on with it (device_follow_size.hpp).
        const auto end_non_followers = [&](const prospero::CaptureMode &before, const prospero::CaptureMode &now) {
            const auto [joined, followers] = joined_followers();
            const auto ending = prospero::streams_ended_by_resize(followers, before.height, now.height);
            if (ending.empty()) return;
            std::lock_guard lock(captures.mutex);
            for (const auto i : ending) {
                joined_stream *j = joined[i];
                if (std::find(ch.joined.begin(), ch.joined.end(), j) == ch.joined.end()) continue;  // it left meanwhile
                BOOST_LOG(error) << "PS5 capture: the console's output no longer takes "sv << before.width << 'x' << before.height
                                 << at_120hz_note(slot, before.resolution) << ", ending the stream of a device that joined channel "sv
                                 << slot << "; the next one starts at what it takes. Devices set to follow the output's size "sv
                                    "(web page, Devices) go on at "sv
                                 << now.width << 'x' << now.height << " instead"sv;
                j->shutdown->raise(true);
            }
        };
        // After an open that recovered: what the channel delivers now against `before`. Smaller when the output no longer
        // took the size and this stream's device follows it, larger on the way back (go_back).
        const auto settle_size = [&](const prospero::CaptureMode &before, uint32_t average_before) {
            const auto now = prospero::capture_mode_of(ps_capture_slot_video_resolution(slot));
            const bool smaller = now.height < before.height, resized = now.height != before.height;
            if (smaller)
                BOOST_LOG(warning) << "PS5 capture: the console's output no longer takes "sv << before.width << 'x' << before.height
                                   << (ps_output_at_120hz(ps_capture_slot_open_output(slot)) ? " (120 Hz)"sv : ""sv)
                                   << ", going on at "sv << now.width << 'x' << now.height
                                   << " for the devices that follow the output's size"sv;
            else if (resized)
                BOOST_LOG(info) << "PS5 capture: the console's output takes "sv << now.width << 'x' << now.height
                                << " again, back to it for the devices that follow the output's size"sv;
            if (resized) await_idr = true;
            check_fallback(resized);
            {
                std::lock_guard lock(captures.mutex);
                if (smaller && !ch.followed_from) ch.followed_from = channel_t::followed_t{before, average_before};
                if (resized && !smaller) ch.followed_from.reset();
                ch.average = ps_capture_slot_opening_average(slot);
            }
            if (smaller) end_non_followers(before, now);
            return now;
        };
        // After a reopen that recovered at the size the channel followed down to: back to the size it followed down from,
        // when the output that open read takes it and doesn't run at 120 Hz, and every stream on the channel follows the
        // output's size and asked for at least that much (channel_may_return_to). At the first open after a loss the
        // arbitrator may still hold the 120 Hz title's state and refuse at once (console round of 2026-11-22), so the way
        // back waits for an open that read the output's mode. It stops the session and runs the reopen procedure once more,
        // with its own grace and follow on: a second short gap for the client. An output that refuses still goes on at
        // 1080p, and only the next loss tries again. False when the stream ends.
        const auto go_back = [&] {
            std::optional<channel_t::followed_t> from;
            prospero::CaptureMode before;
            uint32_t average_before = 0;
            {
                std::lock_guard lock(captures.mutex);
                from = ch.followed_from;
                before = ch.delivered;
                average_before = ch.average;
            }
            const uint32_t output = ps_capture_slot_open_output(slot);
            if (!from || before.height >= from->mode.height || !ps_output_holds(output, from->mode.resolution) ||
                ps_output_at_120hz(output))
                return true;
            auto followers = joined_followers().second;
            followers.insert(followers.begin(), follows_size());
            if (!prospero::channel_may_return_to(followers, from->mode.height)) return true;
            char shown[16];
            std::snprintf(shown, sizeof shown, "%#x", static_cast<unsigned>(output));
            BOOST_LOG(info) << "PS5 capture: the console's output takes "sv << from->mode.width << 'x' << from->mode.height
                            << " again (video out mode "sv << shown
                            << "), reopening at it for the devices that follow the output's size"sv;
            const auto since = std::chrono::steady_clock::now();
            session->stop();
            ps_capture_slot_set_video_mode(slot, from->mode.resolution, low_rate ? 0 : 1);
            ps_capture_slot_set_opening_bitrate(slot, from->average, from->average / 100 * (hevc ? 110 : 112));
            if (const auto end = reopen(since, true, true); end != reopen_end::recovered) {
                reopen_failed(end);
                return false;
            }
            log_reopened(since);
            if (const auto now = settle_size(before, average_before); now.height == before.height)
                BOOST_LOG(info) << "PS5 capture: the console's output refused "sv << from->mode.width << 'x' << from->mode.height
                                << " again, staying at "sv << now.width << 'x' << now.height
                                << " for the devices that follow the output's size until the next title switch"sv;
            return true;
        };
        // The reopen procedure, then the size it brought, then maybe the way back up. False when the stream ends.
        const auto recover = [&](std::chrono::steady_clock::time_point since, bool at_once) {
            prospero::CaptureMode before;
            uint32_t average_before = 0;
            {
                std::lock_guard lock(captures.mutex);
                before = ch.delivered;
                average_before = ch.average;
            }
            // A channel that followed the output down reopens at what it delivers; going back up is go_back's.
            if (const auto end = reopen(since, follows_size().follows, at_once); end != reopen_end::recovered) {
                reopen_failed(end);
                return false;
            }
            log_reopened(since);
            settle_size(before, average_before);
            if (!shutdown_event->peek() && !go_back()) return false;
            std::lock_guard lock(captures.mutex);
            opened();
            return true;
        };

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
        } else if (took_over) {
            // The holder's session is gone (its reopen failed or its client left during one): the channel opens anew for a
            // stream whose client shows its picture already, so as a reopen of it, through the procedure above: at the
            // size the channel delivers and the rate it last opened with, with the grace and this device's own follow
            // setting. A new stream's fallback to 1080p would change the size under a client that didn't choose that
            // (issue 39). A smaller picture ends the joined streams that don't follow the output's size.
            ps_capture_slot_set_opening_bitrate(slot, average, average / 100 * (hevc ? 110 : 112));
            ps_capture_slot_set_codec(slot, hevc ? 1 : 0);
            ps_capture_slot_set_ten_bit(slot, hdr);
            ps_capture_slot_set_video_mode(slot, opens_at.resolution, low_rate ? 0 : 1);
            session = std::make_unique<prospero::CaptureSession>(prospero::make_ps5_capture_backend(codec, slot), codec);
            const bool recovered = recover(std::chrono::steady_clock::now(), true);
            update_offers();
            if (!recovered) return;
        } else {
            if (!opening.empty()) BOOST_LOG(info) << opening;
            ps_capture_slot_set_opening_bitrate(slot, average, average / 100 * (hevc ? 110 : 112));
            ps_capture_slot_set_codec(slot, hevc ? 1 : 0);
            ps_capture_slot_set_ten_bit(slot, hdr);
            ps_capture_slot_set_video_mode(slot, mode.resolution, low_rate ? 0 : 1);
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
                if (ps_capture_stuck()) log_capture_stuck();
                shutdown_event->raise(true);
                return;
            }
            check_fallback();
            {
                std::lock_guard lock(captures.mutex);
                ch.average = ps_capture_slot_opening_average(slot);
            }
            // A stream that starts while a title holds the output at 120 Hz gets 1080p (the arbitrator refuses 1440p's and
            // 4K's resource then, or OpenVideo the size), and its picture would stay at that after the title closed. When its
            // device follows the output's size, the channel counts as followed down from the size it asked for, at the rate
            // that size would have opened with, and the next reopen whose output takes it goes back up to it (go_back): the
            // title's close is such a reopen. Only for the output's refusal (capture.c says, and not for an output that never
            // takes the size), not for a fallback after a switch or the encoder failed, and not on the second channel, whose
            // 1080p is its profile's cap: it asked for no more.
            if (ps_reopen_may_follow(1, mode.resolution) &&
                ps_capture_slot_video_resolution(slot) == prospero::mode_1080p.resolution &&
                ps_capture_slot_fell_back_for_output(slot) && follows_size().follows) {
                {
                    std::lock_guard lock(captures.mutex);
                    ch.followed_from = channel_t::followed_t{mode, average};
                }
                BOOST_LOG(info) << "PS5 capture: the console's output takes no "sv << mode.width << 'x' << mode.height << " now"sv
                                << (ps_output_at_120hz(ps_capture_slot_open_output(slot)) ? " (120 Hz)"sv : ""sv)
                                << ", starting at "sv << prospero::mode_1080p.width << 'x' << prospero::mode_1080p.height
                                << "; the stream goes up when it does, for the devices that follow the output's size"sv;
            }
            BOOST_LOG(info) << "PS5 capture "sv << (hdr ? "HEVC HDR"sv : hevc ? "HEVC"sv : "H.264"sv)
                            << (slot ? " (second channel)"sv : ""sv) << " bitrate "sv << ps_capture_slot_opening_average(slot) / 1000
                            << " kbit/s (requested "sv << requested / 1000 << ')';
            // A capture that opens sends an IDR first, and the client's request at the stream's start waits for it (asking
            // the encoder as well brought a second IDR a few frames later). Where the open read that frame itself (H.264
            // below 11.00) the encoder is asked at once. Where the audio thread had opened the channel earlier, the first
            // frame read may be a later one, and the encoder is asked when it isn't an IDR: the next periodic IDR can be
            // ten seconds away.
            std::lock_guard lock(captures.mutex);
            opened();
        }
        if (!handed_over) {
            if (const auto asked = keyframe_client(channel_data, config); asked.remote) {
                const auto pacing = asked.pacing;
                BOOST_LOG(info) << "PS5 video: stream over the internet, up to "sv << pacing.burst_kb
                                << " kB of a frame at once, the rest at "sv << pacing.pace_percent
                                << "% of the stream's rate (internet_burst_kb, internet_pace_percent), error correction "sv
                                << prospero::fec_share(config::stream.fec_percentage, config::ps5.internet_fec_percentage, true)
                                << (config::ps5.internet_fec_percentage >= 0 ? "% (internet_fec_percentage)"sv : "% (fec_percentage)"sv);
            }
        }
        prospero::EncodedFrame frame;
        prospero::CaptureSession::Descriptor desc{};
        // For the debug log: what the descriptor's words and the timestamps handed on do over 300 frames after each open
        // and every 5 minutes (video/descriptor_clock.hpp; issue 61).
        prospero::DescriptorClock descriptor_clock;
        auto keyframes_logged = std::chrono::steady_clock::now();
        auto losses_logged = keyframes_logged;
        // A loss a client named needs an IDR after all. Both under captures.mutex.
        const auto keyframe_for = [&](std::uintptr_t id, std::chrono::steady_clock::time_point now) {
            ++ch.losses.keyframes;
            if (id == reinterpret_cast<std::uintptr_t>(channel_data)) {
                ch.keyframes.request(now, keyframe_client(channel_data, config));
                return;
            }
            for (auto *j : ch.joined)
                if (id == reinterpret_cast<std::uintptr_t>(j->session))
                    ch.keyframes.request(now, keyframe_client(j->session, *j->config));
        };
        const auto name_loss = [&](stream::session_t *stream, std::pair<int64_t, int64_t> frames,
                                   std::chrono::steady_clock::time_point now) {
            const auto id = reinterpret_cast<std::uintptr_t>(stream);
            if (ch.recovery.lost(id, frames.first, frames.second) == prospero::LossRecovery::Loss::keyframe) keyframe_for(id, now);
        };
        uint32_t counter = 0;  // the encoder's count of the frame read last
        // Moonlight asks for an IDR after loss; Avcap2 delivers one within 2-3 frames (R1). Every IDR is a large frame for
        // every stream on the channel, so the keyframe policy decides which requests reach the encoder, and when.
        while (!shutdown_event->peek()) {
            const auto now = std::chrono::steady_clock::now();
            std::optional<prospero::KeyframePolicy::Client> asking;
            if (idr_events->peek()) {
                idr_events->pop();
                asking = keyframe_client(channel_data, config);
            }
            std::optional<std::pair<int64_t, int64_t>> named;
            if (rfi) named = lost_events->try_pop();
            bool ask = false;
            std::optional<uint32_t> invalidate;
            {
                std::lock_guard lock(captures.mutex);
                if (asking) ch.keyframes.request(now, *asking);
                if (held) ch.keyframes.force();  // asked again after the policy's patience while none came
                if (rfi) {
                    // The losses the clients named since the last frame: the encoder is told the oldest one, between two
                    // frames, and the frames read from here on are looked at for the one each client goes on from.
                    if (named) name_loss(channel_data, *named, now);
                    for (auto *j : ch.joined)
                        if (const auto theirs = j->lost->try_pop()) name_loss(j->session, *theirs, now);
                    // (A stream that took the open channel over tells the encoder once it read a frame and knows its count.)
                    if (counter) {
                        const auto call = ch.recovery.call(counter);
                        for (const auto id : call.keyframe) keyframe_for(id, now);
                        invalidate = call.invalidate;
                    }
                    log_loss_recovery(slot, ch, now, losses_logged);
                }
                ask = ch.keyframes.poll(now);
                log_keyframe_policy(slot, ch, now, keyframes_logged);
            }
            if (invalidate && session->invalidate_frame(*invalidate) != 0) {
                // The encoder didn't take it (capture.c logs the code): IDRs for the streams that wait.
                std::lock_guard lock(captures.mutex);
                for (const auto id : ch.recovery.call_failed()) keyframe_for(id, now);
                if (!ask) ask = ch.keyframes.poll(now);
            }
            if (ask && session->request_keyframe()) {
                BOOST_LOG(warning) << "PS5 capture: keyframe request failed, next periodic IDR within ten seconds"sv;
                std::lock_guard lock(captures.mutex);
                ch.keyframes.ask_failed();
                ch.recovery.keyframe_dropped();
            }
            const auto status = session->read(frame, 100ms, {}, &desc);
            if (status == prospero::CaptureStatus::timeout) continue;
            if (status != prospero::CaptureStatus::frame) {
                // Starting or closing a game takes the capture resource away for a moment; reopen instead of ending the
                // stream (recover, above). The reopened capture sends an IDR first (opened()).
                BOOST_LOG(warning) << "PS5 capture lost ("sv << static_cast<int>(status) << "), reopening"sv;
                if (!recover(std::chrono::steady_clock::now(), false)) break;
                descriptor_clock.reset();
                counter = 0;
                continue;
            }
            counter = frame_counter(desc, hevc);
            const auto timestamp = capture_time(desc, frame.read_time, hevc);
            // A request that came in while this IDR was being read left the client before the IDR, which answers it. Taken
            // only at the next turn, after keyframe_sent, it would look blind and bring a second IDR blind_hold later (in the
            // 0.6.6 burst logs 7 of 243 IDRs came 330 to 343 ms after the one before, the client having asked for none since).
            // The same goes for a request of a joined stream that its thread, which looks every 20 ms, hasn't passed in yet.
            std::optional<prospero::KeyframePolicy::Client> crossing;
            if (frame.idr && idr_events->peek()) {
                idr_events->pop();
                crossing = keyframe_client(channel_data, config);
            }
            if (await_idr && !frame.idr) {
                std::lock_guard lock(captures.mutex);
                ch.keyframes.frame_read(false);  // asks for the IDR if the opening one didn't come first
                continue;
            }
            await_idr = false;
            const auto ns = [](std::chrono::steady_clock::time_point t) {
                return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
            };
            if (const auto report = descriptor_clock.add(desc, ns(frame.read_time), ns(timestamp), timestamp == frame.read_time)) {
                BOOST_LOG(debug) << "PS5 video: channel "sv << slot << (hevc ? " HEVC"sv : " H.264"sv) << " descriptor over "sv
                                 << report->steps;
                BOOST_LOG(debug) << "PS5 video: channel "sv << slot << " descriptor at the start of that: "sv << report->words;
            }
            bool goes_on = false;  // this stream's client goes on from this frame after a loss
            {
                std::lock_guard lock(captures.mutex);
                ch.keyframes.frame_read(frame.idr);
                prospero::LossRecovery::Verdict verdict;
                if (rfi) {
                    // While a stream waits: is this the frame it can go on from? Its first slice header says which
                    // pictures it is decoded from. The frames after a call are looked at for the other streams too.
                    std::optional<prospero::hevc::Refs> refs;
                    if (!frame.idr && ch.layout && ch.recovery.watching(counter)) {
                        const std::span<const uint8_t> bytes(frame.bytes);
                        refs = prospero::hevc::refs(*ch.layout, bytes.first(std::min<std::size_t>(bytes.size(), 256)));
                    }
                    verdict = ch.recovery.frame(counter, frame.idr, refs);
                    for (const auto id : verdict.keyframe) keyframe_for(id, frame.read_time);
                    ch.losses.went_on += static_cast<int>(verdict.recovered.size());
                    // A frame decoded from a picture one of the streams never had: that stream gets nothing until the
                    // IDR asked for here, so its client sees a pause of a few frames and no broken picture.
                    for (const auto id : verdict.held) {
                        ++ch.losses.held;
                        ch.keyframes.force();
                        if (id == reinterpret_cast<std::uintptr_t>(channel_data)) held = true;
                        for (auto *j : ch.joined)
                            if (id == reinterpret_cast<std::uintptr_t>(j->session)) j->keyframe_seen = false;
                    }
                }
                if (frame.idr) held = false;
                const auto marked = [&verdict](stream::session_t *stream) {
                    return std::find(verdict.recovered.begin(), verdict.recovered.end(),
                                     reinterpret_cast<std::uintptr_t>(stream)) != verdict.recovered.end();
                };
                goes_on = marked(channel_data);
                if (rfi && !held) ch.recovery.sent(reinterpret_cast<std::uintptr_t>(channel_data), next_index, counter);
                if (frame.idr) {
                    if (crossing) ch.keyframes.request(frame.read_time, *crossing);
                    for (auto *j : ch.joined)
                        if (j->idr->try_pop()) ch.keyframes.request(frame.read_time, keyframe_client(j->session, *j->config));
                    ch.keyframes.keyframe_sent(frame.read_time, wire_bytes(frame.bytes.size()));
                }
                for (auto *j : ch.joined) {
                    // One ended for a size its client didn't set up for gets nothing more (end_non_followers).
                    if (j->shutdown->peek()) continue;
                    if (!j->keyframe_seen && !frame.idr) continue;
                    j->keyframe_seen = true;
                    if (rfi) ch.recovery.sent(reinterpret_cast<std::uintptr_t>(j->session), j->next_index, counter);
                    auto copy = std::make_unique<counted_packet>(std::vector<uint8_t>(frame.bytes), j->next_index++, frame.idr, j->session);
                    copy->frame_timestamp = timestamp;
                    copy->after_ref_frame_invalidation = marked(j->session);
                    packets->raise(std::move(copy));
                }
            }
            if (!held) {
                auto packet = std::make_unique<counted_packet>(std::move(frame.bytes), next_index++, frame.idr, channel_data);
                packet->frame_timestamp = timestamp;
                packet->after_ref_frame_invalidation = goes_on;
                packets->raise(std::move(packet));
            }
            frame.bytes = {};
        }
        // A joined stream takes the channel over: it gets the open session and goes on reading. Otherwise the channel
        // closes before release lets it go, so a new stream can open it.
        {
            std::lock_guard lock(captures.mutex);
            ch.keyframes.forget(reinterpret_cast<std::uintptr_t>(channel_data));
            ch.recovery.forget(reinterpret_cast<std::uintptr_t>(channel_data));
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
