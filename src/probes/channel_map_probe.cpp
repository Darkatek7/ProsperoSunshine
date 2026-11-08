// SPDX-License-Identifier: GPL-3.0-only
// Whether a payload's own sound reaches OpenAudio's encoder type 3, to find which of its 12 channels is which speaker. The
// probe plays a quiet tone on one channel of a sceAudioOut port at a time (60 Hz plus 1 kHz, 1.2 s each, a pause between)
// and reads type 3 meanwhile; the log gets the port's state (output channels) and, for every played channel, the RMS of all
// 12 captured channels from the middle of the tone. This version tries a stereo port of each type (MAIN as the system user,
// BGM, VOICE, AUX); an earlier one played the two 8-channel layouts (formats kFloat8 and kFloat8Std) on MAIN. On 13.00 none of
// it reached the capture (STATUS.md, "Für 0.3.1").
// The host must not be streaming: a busy capture is tried again after 5 s, eight times.
#include "platform/ps5/capture.h"
#include "platform/ps5/diag.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C" {
int sceAudioOutInit(void);
int sceAudioOutOpen(int32_t user, int32_t type, int32_t index, uint32_t length, uint32_t rate, uint32_t format);
int sceAudioOutOutput(int32_t handle, const void *samples);
int sceAudioOutClose(int32_t handle);
int sceAudioOutGetPortState(int32_t handle, void *state);
int sceUserServiceInitialize(void *);
int sceUserServiceGetForegroundUser(uint32_t *);
}

namespace {
constexpr int kCaptured = 12;
constexpr uint32_t kLength = 256;
[[maybe_unused]] constexpr int kFloatStereo = 4, kFloat8 = 5, kFloat8Std = 7;

// What plays now: pass * 16 + channel, or -1 in a pause. Set by the player, read by the capture loop.
std::atomic<int> playing{-1};
std::atomic<bool> settled{false};  // the middle of a tone, where the RMS counts
std::atomic<bool> done{false};

struct Sum {
    double square[kCaptured] = {};
    long frames = 0;
};
Sum sums[4][8];

void play(int handle, int channels, int pass, int channel, double seconds) {
    std::vector<float> buffer(kLength * channels);
    const long total = static_cast<long>(seconds * 48000);
    playing = channel < 0 ? -1 : pass * 16 + channel;
    for (long at = 0; at < total; at += kLength) {
        settled = channel >= 0 && at > total / 4 && at < total * 3 / 4;
        for (uint32_t i = 0; i < kLength; ++i) {
            const double t = static_cast<double>(at + i) / 48000;
            const float value = channel < 0 ? 0.0f
                : static_cast<float>(0.06 * std::sin(2 * M_PI * 60 * t) + 0.03 * std::sin(2 * M_PI * 1000 * t));
            for (int c = 0; c < channels; ++c) buffer[i * channels + c] = c == channel ? value : 0.0f;
        }
        const int rc = sceAudioOutOutput(handle, buffer.data());
        if (at == 0 && channel <= 0) ps_diag_log("ProsperoSunshine: channel_map: pass %d channel %d output rc=%#x\n", pass, channel, static_cast<unsigned>(rc));
    }
    settled = false;
    playing = -1;
}

void player(int32_t user) {
    ps_diag_log("ProsperoSunshine: channel_map: AudioOutInit rc=%#x\n", static_cast<unsigned>(sceAudioOutInit()));
    const struct { int format, channels, playable, type; int32_t user; const char *name; } passes[] = {
        {kFloatStereo, 2, 2, 0, 255, "STEREO main system-user"}, {kFloatStereo, 2, 2, 1, user, "STEREO bgm"},
        {kFloatStereo, 2, 2, 2, user, "STEREO voice"}, {kFloatStereo, 2, 2, 127, user, "STEREO aux"}};
    for (int pass = 0; pass < 4; ++pass) {
        const int handle = sceAudioOutOpen(passes[pass].user, passes[pass].type, 0, kLength, 48000, static_cast<uint32_t>(passes[pass].format));
        ps_diag_log("ProsperoSunshine: channel_map: pass %d %s open=%#x\n", pass, passes[pass].name,
                    static_cast<unsigned>(handle));
        if (handle < 0) continue;
        // SceAudioOutPortState: u16 output (1 primary, 2 secondary, 0x40 headphone, ...), u8 channels, u8, i16 volume,
        // u16 reroute counter, u64 flag.
        uint8_t state[32] = {};
        const int src = sceAudioOutGetPortState(handle, state);
        uint16_t output = 0;
        std::memcpy(&output, state, 2);
        ps_diag_log("ProsperoSunshine: channel_map: port state rc=%#x output=%#x channels=%u raw=%02x%02x%02x%02x%02x%02x%02x%02x\n",
                    static_cast<unsigned>(src), output, state[2], state[0], state[1], state[2], state[3], state[4],
                    state[5], state[6], state[7]);
        play(handle, passes[pass].channels, pass, -1, 0.5);
        for (int c = 0; c < passes[pass].playable; ++c) {
            play(handle, passes[pass].channels, pass, c, 1.2);
            play(handle, passes[pass].channels, pass, -1, 0.4);
        }
        sceAudioOutClose(handle);
    }
    done = true;
}

std::vector<uint8_t> video(4 * 1024 * 1024);
}  // namespace

int main() {
    mkdir("/data/prosperosunshine/channel_map", 0777);
    ps_diag_open("/data/prosperosunshine/channel_map/channel_map.log", "ProsperoSunshine: channel_map_probe");
    ps_capture *s = nullptr;
    for (int busy = 0;; busy++) {
        const int rc = ps_capture_open(&s);
        if (rc == 0) break;
        ps_diag_log("ProsperoSunshine: channel_map video open rc=%d\n", rc);
        if (rc != PS_CAPTURE_BUSY || busy == 7) return 1;
        sleep(5);
    }
    const int arc = ps_capture_audio_open_ex(s, 3, 0, 0, 2, 180, 0);
    ps_diag_log("ProsperoSunshine: channel_map: type 3 open rc=%#x\n", static_cast<unsigned>(arc));
    if (arc) {
        ps_capture_close(s);
        return 1;
    }
    sceUserServiceInitialize(nullptr);
    uint32_t user = 0;
    ps_diag_log("ProsperoSunshine: channel_map: foreground user rc=%#x user=%#x\n",
                static_cast<unsigned>(sceUserServiceGetForegroundUser(&user)), user);
    std::thread thread(player, static_cast<int32_t>(user));
    std::vector<uint8_t> packet(0x40000);
    uint8_t header[0x10], desc[0x88];
    int packets = 0;
    while (!done) {
        for (int n; (n = ps_capture_audio_read(s, packet.data(), packet.size(), header)) != 0;) {
            if (n < 0) {
                ps_diag_log("ProsperoSunshine: channel_map: read rc=%#x\n", static_cast<unsigned>(n));
                done = true;
                break;
            }
            ++packets;
            const int now = playing;
            if (now < 0 || !settled) continue;
            Sum &sum = sums[now / 16][now % 16];
            const float *f = reinterpret_cast<const float *>(packet.data());
            const int frames = n / (kCaptured * 4);
            for (int i = 0; i < frames; ++i)
                for (int c = 0; c < kCaptured; ++c) sum.square[c] += static_cast<double>(f[i * kCaptured + c]) * f[i * kCaptured + c];
            sum.frames += frames;
        }
        while (ps_capture_read(s, video.data(), video.size(), desc) > 0) {}
        usleep(2000);
    }
    thread.join();
    ps_diag_log("ProsperoSunshine: channel_map: packets=%d\n", packets);
    for (int pass = 0; pass < 4; ++pass)
        for (int c = 0; c < 8; ++c) {
            const Sum &sum = sums[pass][c];
            if (!sum.frames) continue;
            char line[256];
            int at = std::snprintf(line, sizeof line, "pass %d played %d frames %ld rms:", pass, c, sum.frames);
            for (int k = 0; k < kCaptured && at < static_cast<int>(sizeof line); ++k)
                at += std::snprintf(line + at, sizeof line - at, " %.4f", std::sqrt(sum.square[k] / sum.frames));
            ps_diag_log("ProsperoSunshine: channel_map: %s\n", line);
        }
    ps_diag_log("ProsperoSunshine: channel_map: done close=%d\n", ps_capture_close(s));
    return 0;
}
