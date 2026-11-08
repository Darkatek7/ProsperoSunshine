// SPDX-License-Identifier: GPL-3.0-only
// Co-op research: can one process hold two ResourceArbitrator profiles and two Avcap2 video channels at once, so that
// each device gets its own mode? It does (STATUS.md); the host now opens a second channel on 175 (capture.c's slot 1).
// Per variant: waits until both profiles are free, registers both, asks RUNNING for each and waits for each grant, then
// Initialize, OpenVideo and Start for both channels, reads both side by side for `seconds`, and closes everything again
// (Stop, Close, encoder memory, Terminate, STOP, unregister) before the next variant. Logs to
// /data/prosperosunshine/dual_open_probe.log and klog: every rc, the arbitrator's answers with their time, and per channel
// and second the frames, kbit/s and the longest gap between frames; a summary line per channel at the end of a variant.
//
// The host must be stopped first (the app's "Stop host"): the probe refuses while 127.0.0.1:47989 or :47995 answers.
// Load the encoder for the bitrate figures: the home screen is too quiet; the Mario 64 demo or a noise page is not.
// Runs about (seconds + 3) s per variant, around 80 s with the defaults. Don't take screenshots while it runs.
//
// Variants come from /data/prosperosunshine/dual_open_probe.conf if it exists, else the built-in list below. Lines:
//   seconds <n>                                   reading time per variant, default 10
//   variant <name> <ra> <codec> <res> <mbit> [<ra> <codec> <res> <mbit>]
// codec: h264 | hevc; res: 360 | 540 | 720 | 1080 | 1440 | 2160; ra: the ResourceArbitrator profile (180, 185, 175 grant
// scanin[2K], 177 and 182 scanin[4K], docs/re/avcap2-exports.md). One channel only gives a baseline. '#' starts a comment.
#include "platform/ps5/capture.h"
#include "platform/ps5/diag.h"
#include "platform/ps5/experimental.h"
#include "platform/ps5/ra_resolve.h"
#include "probe_host_check.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>
extern "C" {
#include <ps5/kernel.h>
}

#define LOG(...) ps_diag_log("ProsperoSunshine: dual_open " __VA_ARGS__)

namespace {

using steady = std::chrono::steady_clock;
using load_fn = int (*)(const char *, size_t, const void *, uint32_t, void *, int *);
using handle_fn = int (*)(intptr_t);

constexpr int again = static_cast<int>(0x81950002);
constexpr int resource_not_active = static_cast<int>(0x81950008);
constexpr const char *conf_path = "/data/prosperosunshine/dual_open_probe.conf";

// ---- ResourceArbitrator answers. The callback has no context argument; it only touches these atomics and dispatches on id.

struct slot {
    std::atomic<int> id{0}, granted{0}, revoked{0}, refused{0}, refused_value{-1};
};
slot slots[2];

// Every answer with its time since the variant began, logged by the main thread later on.
struct event_record {
    int id, event, value;
    long ms;
    std::atomic<bool> done{false};
};
constexpr int max_events = 64;
event_record events[max_events];
std::atomic<int> event_count{0};
std::atomic<long long> epoch_ms{0};

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(steady::now().time_since_epoch()).count();
}

long ms_since(steady::time_point from) {
    return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(steady::now() - from).count());
}

int resource_event(int id, int event, const uint8_t *data) {
    const int index = event_count.fetch_add(1);
    if (index >= 0 && index < max_events) {
        event_record &e = events[index];
        e.id = id;
        e.event = event;
        e.value = data ? data[0] : -1;
        e.ms = static_cast<long>(now_ms() - epoch_ms.load());
        e.done.store(true);
    }
    for (slot &s : slots) {
        if (s.id.load() != id) continue;
        if (event == 1 && data) {
            s.granted.store(data[0] == 1);
            if (data[0] != 1) {
                s.refused_value.store(data[0]);
                s.refused.store(1);
            }
        }
        if (event == 2) {
            s.revoked.store(1);
            s.granted.store(0);
        }
    }
    return 0;
}

int logged_events = 0;
void log_new_events() {
    const int count = event_count.load() < max_events ? event_count.load() : max_events;
    for (; logged_events < count && events[logged_events].done.load(); logged_events++) {
        const event_record &e = events[logged_events];
        LOG("ra event id=%d event=%d value=%d t_ms=%ld\n", e.id, e.event, e.value, e.ms);
    }
}

// ---- System calls, resolved once.

struct calls {
    uint32_t av = 0;
    int (*reg)(int, void *, void *) = nullptr;
    int (*unregister)(int) = nullptr;
    int (*request)(int, const char *, void *, int) = nullptr;
    int (*list)(int32_t *) = nullptr;
    int (*init)() = nullptr;
    int (*terminate)() = nullptr;
    int (*mode)(int *) = nullptr;
    int (*open_video)(intptr_t *, const void *, uint8_t) = nullptr;
    handle_fn start = nullptr, stop = nullptr, close = nullptr;
    int (*headers)(intptr_t, void *) = nullptr;
    int (*read)(intptr_t, void *, size_t, void *, uint32_t) = nullptr;
    int (*read2)(intptr_t, void *, size_t, void *, uint32_t) = nullptr;
    int (*reserve)(const void *, void *, void *) = nullptr;
    int (*free_memory)(void *, void *) = nullptr;
};

uint32_t module(load_fn load, const char *name) {
    uint32_t handle = 0;
    if (kernel_dynlib_handle(-1, name, &handle) == 0 && handle) return handle;
    char path[128];
    int result = -1;
    std::snprintf(path, sizeof path, "/system/common/lib/%s", name);
    const int rc = load(path, 0, nullptr, 0, nullptr, &result);
    LOG("load %s rc=%#x result=%#x\n", name, rc, result);
    if (kernel_dynlib_handle(-1, name, &handle)) return 0;
    return handle;
}

bool resolve(calls &c) {
    static const char *const kernels[] = {"libkernel_web.sprx", "libkernel_sys.sprx", "libkernel.sprx"};
    load_fn load = nullptr;
    for (const char *k : kernels) {
        uint32_t h = 0;
        if (!kernel_dynlib_handle(-1, k, &h) && h) load = (load_fn)kernel_dynlib_dlsym(-1, h, "sceKernelLoadStartModule");
        if (load) break;
    }
    if (!load || !module(load, "libSceIpmi.sprx")) return false;
    c.av = module(load, "libSceAvcap2.sprx");
    const uint32_t ra = module(load, "libSceResourceArbitrator.sprx");
    if (!c.av || !ra) return false;
    ps_ra_functions found{};
    if (ps_ra_resolve(ra, &found)) return false;
    const intptr_t base = kernel_dynlib_mapbase_addr(-1, ra);
    if (!base) return false;
    c.reg = reinterpret_cast<int (*)(int, void *, void *)>(base + found.offset[PS_RA_REGISTER]);
    c.unregister = reinterpret_cast<int (*)(int)>(base + found.offset[PS_RA_UNREGISTER]);
    c.request = reinterpret_cast<int (*)(int, const char *, void *, int)>(base + found.offset[PS_RA_REQUEST]);
    c.list = reinterpret_cast<int (*)(int32_t *)>(base + found.offset[PS_RA_LIST]);
#define SYM(field, name) c.field = reinterpret_cast<decltype(c.field)>(kernel_dynlib_dlsym(-1, c.av, name))
    SYM(init, "sceAvcap2Initialize");
    SYM(terminate, "sceAvcap2Terminate");
    SYM(mode, "sceAvcap2GetVideoOutMode");
    SYM(open_video, "sceAvcap2OpenVideo");
    SYM(start, "sceAvcap2Start");
    SYM(stop, "sceAvcap2Stop");
    SYM(close, "sceAvcap2Close");
    SYM(headers, "sceAvcap2GetNonVclNalUnitStream");
    SYM(read, "sceAvcap2ReadVideo");
    SYM(read2, "sceAvcap2ReadVideo2");
#undef SYM
    // Encoder memory for HEVC, as capture.c reserves it (by NID).
    c.reserve = reinterpret_cast<decltype(c.reserve)>(kernel_dynlib_resolve(-1, c.av, "yN0w4tWivqs"));
    c.free_memory = reinterpret_cast<decltype(c.free_memory)>(kernel_dynlib_resolve(-1, c.av, "96UQhNnBlxc"));
    return c.init && c.terminate && c.mode && c.open_video && c.start && c.stop && c.close && c.headers && c.read && c.read2;
}

std::string active_list(const calls &c, int *rc = nullptr) {
    int32_t now[32] = {};
    const int list_rc = c.list(now);
    if (rc) *rc = list_rc;
    std::string out;
    for (const int32_t id : now)
        if (id) out += (out.empty() ? "" : ",") + std::to_string(id);
    return out.empty() ? "none" : out;
}

bool listed(const calls &c, int id) {
    int32_t now[32] = {};
    if (c.list(now)) return true;  // unknown counts as busy
    for (const int32_t entry : now)
        if (entry == id) return true;
    return false;
}

void put_u32(uint8_t *p, size_t at, uint32_t value) { std::memcpy(p + at, &value, 4); }
void put_u64(uint8_t *p, size_t at, uint64_t value) { std::memcpy(p + at, &value, 8); }

// ---- Variants.

struct channel_spec {
    int resource = 0;
    uint32_t codec = 0, resolution = 4, mbit = 20;
};

struct variant {
    std::string name;
    std::vector<channel_spec> channels;
};

const char *const mode_names[] = {"", "360p", "540p", "720p", "1080p", "1440p", "2160p"};

uint32_t resolution_of(const std::string &word) {
    static const char *const heights[] = {"", "360", "540", "720", "1080", "1440", "2160"};
    for (uint32_t i = 1; i < 7; i++)
        if (word == heights[i] || word == mode_names[i]) return i;
    return 0;
}

std::vector<variant> defaults() {
    return {
        {"a20", {{180, 0, 4, 20}, {185, 0, 4, 20}}},  // (a) H.264 1080p60 twice, 20 Mbit/s
        {"a5", {{180, 0, 4, 5}, {185, 0, 4, 5}}},     // (a) the same at 5 Mbit/s
        {"b", {{180, 0, 4, 20}, {185, 1, 4, 20}}},    // (b) H.264 + HEVC 1080p
        {"c", {{180, 0, 4, 20}, {185, 0, 3, 10}}},    // (c) 1080p + 720p
        {"d180", {{182, 0, 6, 40}, {180, 0, 4, 20}}}, // (d) 4K on 182 + 1080p on 180
        {"d185", {{182, 0, 6, 40}, {185, 0, 4, 20}}}, // (d) 4K on 182 + 1080p on 185
    };
}

// Reads the conf file if there is one. Returns false on a line it can't read: better no run than a different one.
bool read_conf(std::vector<variant> &variants, int &seconds) {
    FILE *in = std::fopen(conf_path, "r");
    if (!in) {
        LOG("no %s: built-in variants\n", conf_path);
        return true;
    }
    std::vector<variant> parsed;
    char line[256];
    int number = 0;
    bool ok = true;
    while (ok && std::fgets(line, sizeof line, in)) {
        number++;
        if (char *hash = std::strchr(line, '#')) *hash = 0;
        char word[32] = {}, name[32] = {}, codec[2][8] = {}, res[2][8] = {};
        int ra[2] = {}, mbit[2] = {}, value = 0;
        if (std::sscanf(line, "%31s", word) != 1) continue;
        if (!std::strcmp(word, "seconds") && std::sscanf(line, "%*s %d", &value) == 1 && value > 0 && value <= 120) {
            seconds = value;
            continue;
        }
        const int fields = std::sscanf(line, "%*s %31s %d %7s %7s %d %d %7s %7s %d", name, &ra[0], codec[0], res[0], &mbit[0], &ra[1],
                                       codec[1], res[1], &mbit[1]);
        if (std::strcmp(word, "variant") || (fields != 5 && fields != 9)) {
            ok = false;
            break;
        }
        variant v{name, {}};
        for (int i = 0; i < (fields == 9 ? 2 : 1); i++) {
            const std::string c = codec[i];
            channel_spec spec{ra[i], c == "hevc" ? 1u : 0u, resolution_of(res[i]), static_cast<uint32_t>(mbit[i])};
            if (ra[i] <= 0 || (c != "h264" && c != "hevc") || !spec.resolution || mbit[i] < 1 || mbit[i] > 150) ok = false;
            v.channels.push_back(spec);
        }
        if (v.channels.size() == 2 && v.channels[0].resource == v.channels[1].resource) ok = false;
        parsed.push_back(v);
    }
    std::fclose(in);
    if (!ok) {
        LOG("refused: %s line %d is not 'seconds <1..120>' or 'variant <name> <ra> <h264|hevc> <res> <mbit> [...]' with two different ra\n",
            conf_path, number);
        return false;
    }
    if (!parsed.empty()) variants = parsed;
    LOG("read %s: %zu variants, seconds=%d\n", conf_path, variants.size(), seconds);
    return true;
}

// ---- One channel's state.

struct channel {
    channel_spec spec;
    slot *answers = nullptr;
    char label = 'A';
    bool registered = false, requested = false, opened = false, started = false, has_memory = false;
    intptr_t handle = 0;
    uint8_t memory[0x40] = {};
    int register_rc = 0, running_rc = 0, grant_ms = -1, open_rc = 0, start_rc = 0, headers_rc = 0, header_bytes = 0;
    // Statistics over the reading time, and for the current second.
    long frames = 0, second_frames = 0, max_gap_ms = 0, second_gap_ms = 0, first_frame_ms = -1, error_ms = -1;
    uint64_t bytes = 0, second_bytes = 0;
    int largest = 0, error = 0;
    bool revoke_logged = false;
    steady::time_point started_at, last_frame;
};

std::string describe(const channel_spec &s) {
    char text[64];
    std::snprintf(text, sizeof text, "ra=%d %s %s %u Mbit/s", s.resource, s.codec ? "hevc" : "h264", mode_names[s.resolution], s.mbit);
    return text;
}

bool usable(const channel &ch) { return ch.answers->granted.load() && !ch.answers->revoked.load(); }

// The grant of a RUNNING request, waited for as capture.c does (6 s).
bool wait_grant(channel &ch) {
    int waited = 0;
    for (; waited < 6000 && !ch.answers->granted.load() && !ch.answers->revoked.load() && !ch.answers->refused.load(); waited += 10)
        usleep(10000);
    const bool ok = usable(ch);
    ch.grant_ms = ok ? waited : -1;
    LOG("%c RUNNING outcome=%s value=%d waited_ms=%d\n", ch.label,
        ok ? "granted" : ch.answers->revoked.load() ? "revoked" : ch.answers->refused.load() ? "refused" : "timeout",
        ch.answers->refused_value.load(), waited);
    return ok;
}

void open_channel(const calls &c, channel &ch, int output) {
    const channel_spec &s = ch.spec;
    const uint32_t average = s.mbit * 1000000u;
    // Peak as the host's spikes use it: HEVC 1.1, H.264 1.12 times the average.
    const ps_capture_video_config values = {s.resolution, 1, s.codec, 0, average, s.codec ? average / 100 * 110 : average / 100 * 112,
                                            s.resource, 0};
    uint8_t config[0x2e8] = {};
    ps_capture_build_config(config, &values, static_cast<uint32_t>(output), PS_EXP_STANDARD);
    if (s.codec == 1 && ps_capture_record_shift() >= 0) {
        // Memory Mode 1 as Remote Play sets it up, as capture.c does for HEVC in the standard variant.
        if (!c.reserve || !c.free_memory) {
            LOG("%c encoder memory: calls not found\n", ch.label);
            ch.open_rc = -1;
            return;
        }
        uint8_t request[0x90] = {};
        put_u64(request, 0, sizeof request);
        std::memcpy(request + 8, config + 8, 0x28);
        std::memcpy(request + 0x30, config + 0x30, 0x58);
        put_u64(config, 0x198, 0x50);
        put_u32(config, 0x1a0, 1);
        const int rc = c.reserve(request, config + 0x1a8, config + 0x1c8);
        LOG("%c encoder memory rc=%#x\n", ch.label, static_cast<unsigned>(rc));
        if (rc) {
            ch.open_rc = rc;
            return;
        }
        std::memcpy(ch.memory, config + 0x1a8, sizeof ch.memory);
        ch.has_memory = true;
    }
    int rc = 0, attempt = 0;
    for (;; attempt++) {
        rc = c.open_video(&ch.handle, config, 0);
        if (rc != resource_not_active || attempt == 10 || ch.answers->revoked.load()) break;
        usleep(40000);
    }
    ch.open_rc = rc;
    LOG("%c OpenVideo rc=%#x repeated=%d\n", ch.label, static_cast<unsigned>(rc), attempt);
    if (rc == resource_not_active) LOG("%c active resources: %s\n", ch.label, active_list(c).c_str());
    if (rc) return;
    ch.opened = true;
    rc = c.start(ch.handle);
    ch.start_rc = rc;
    LOG("%c Start rc=%#x\n", ch.label, static_cast<unsigned>(rc));
    if (rc) return;
    ch.started = true;
    ch.started_at = ch.last_frame = steady::now();
    // Parameter sets: SPS, PPS and (HEVC) VPS, each a u64 length and its bytes.
    uint8_t nal[0x1a8] = {};
    put_u64(nal, 0, sizeof nal);
    ch.headers_rc = c.headers(ch.handle, nal);
    static const size_t offsets[3] = {0x10, 0x98, 0x120};
    uint64_t sizes[3] = {};
    for (int i = 0; i < 3; i++) std::memcpy(&sizes[i], nal + offsets[i], 8);
    ch.header_bytes = static_cast<int>(sizes[0] + sizes[1] + sizes[2]);
    LOG("%c headers rc=%#x sizes=%llu,%llu,%llu\n", ch.label, static_cast<unsigned>(ch.headers_rc),
        static_cast<unsigned long long>(sizes[0]), static_cast<unsigned long long>(sizes[1]), static_cast<unsigned long long>(sizes[2]));
}

int read_frame(const calls &c, const channel &ch, std::vector<uint8_t> &buffer) {
    uint8_t descriptor[0x88] = {};
    if (ch.spec.codec == 1) {
        put_u64(descriptor, 0, 0x88);
        return c.read2(ch.handle, buffer.data(), buffer.size(), descriptor, 1);
    }
    put_u64(descriptor, 0, 0x70);
    return c.read(ch.handle, buffer.data(), buffer.size(), descriptor, 1);
}

long ms_between(steady::time_point from, steady::time_point to) {
    return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count());
}

// Polls every started channel until `seconds` are over or none is left; one log line per second for all channels.
void read_all(const calls &c, std::vector<channel> &channels, int seconds, std::vector<uint8_t> &buffer) {
    const steady::time_point begin = steady::now();
    int second = 0;
    for (;;) {
        if (ms_since(begin) >= (second + 1) * 1000L) {
            std::string line;
            for (channel &ch : channels) {
                if (!ch.started) continue;
                char part[128];
                std::snprintf(part, sizeof part, " %c frames=%ld kbit=%llu max_gap_ms=%ld%s", ch.label, ch.second_frames,
                              static_cast<unsigned long long>(ch.second_bytes * 8 / 1000), ch.second_gap_ms, ch.error ? " (error)" : "");
                line += part;
                ch.second_frames = ch.second_gap_ms = 0;
                ch.second_bytes = 0;
            }
            LOG("t=%d%s\n", second + 1, line.c_str());
            log_new_events();
            second++;
        }
        if (second >= seconds) break;
        bool any_frame = false, any_alive = false;
        for (channel &ch : channels) {
            if (!ch.started || ch.error) continue;
            any_alive = true;
            if (ch.answers->revoked.load() && !ch.revoke_logged) {
                ch.revoke_logged = true;
                LOG("%c revoked at t_ms=%ld\n", ch.label, ms_since(begin));
            }
            const int rc = read_frame(c, ch, buffer);
            if (rc == again || rc == 0) continue;
            if (rc < 0 || static_cast<size_t>(rc) > buffer.size()) {
                ch.error = rc;
                ch.error_ms = ms_since(begin);
                LOG("%c read error rc=%#x at t_ms=%ld after %ld frames\n", ch.label, static_cast<unsigned>(rc), ch.error_ms, ch.frames);
                continue;
            }
            const steady::time_point now = steady::now();
            if (ch.frames == 0) {
                ch.first_frame_ms = ms_between(ch.started_at, now);
            } else {
                const long gap = ms_between(ch.last_frame, now);
                if (gap > ch.max_gap_ms) ch.max_gap_ms = gap;
                if (gap > ch.second_gap_ms) ch.second_gap_ms = gap;
            }
            ch.last_frame = now;
            ch.frames++;
            ch.second_frames++;
            ch.bytes += static_cast<uint64_t>(rc);
            ch.second_bytes += static_cast<uint64_t>(rc);
            if (rc > ch.largest) ch.largest = rc;
            any_frame = true;
        }
        if (!any_alive) {
            LOG("no channel left to read at t_ms=%ld\n", ms_since(begin));
            break;
        }
        if (!any_frame) usleep(1000);
    }
}

// Stop, Close, encoder memory, Terminate, STOP, unregister, in the order ps_capture_close uses, the second channel first.
// Returns false when an unregister failed: a registration might be left, so no further variant runs.
bool close_all(const calls &c, std::vector<channel> &channels, bool initialized) {
    bool failed = false, unregistered = true;
    const auto each = [&](auto &&step) {
        for (auto it = channels.rbegin(); it != channels.rend(); ++it) step(*it);
    };
    const auto note = [&](char label, const char *what, int rc) {
        LOG("%c %s rc=%#x\n", label, what, static_cast<unsigned>(rc));
        if (rc < 0) failed = true;
    };
    each([&](channel &ch) { if (ch.started) note(ch.label, "Stop", c.stop(ch.handle)); });
    each([&](channel &ch) { if (ch.opened) note(ch.label, "Close", c.close(ch.handle)); });
    each([&](channel &ch) { if (ch.has_memory) note(ch.label, "free encoder memory", c.free_memory(ch.memory, ch.memory + 0x20)); });
    if (initialized) note('-', "Terminate", c.terminate());
    each([&](channel &ch) { if (ch.requested) note(ch.label, "STOP", c.request(ch.spec.resource, "STOP", nullptr, getpid())); });
    each([&](channel &ch) {
        if (!ch.registered) return;
        const int rc = c.unregister(ch.spec.resource);
        note(ch.label, "unregister", rc);
        if (rc) unregistered = false;
    });
    each([&](channel &ch) { ch.started = ch.opened = ch.has_memory = ch.requested = ch.registered = false; });
    log_new_events();
    for (slot &s : slots) s.id.store(0);
    LOG("closed failed=%d unregistered=%d active=%s\n", failed, unregistered, active_list(c).c_str());
    return unregistered;
}

// One variant from start to end. Returns false if no further variant may run.
bool run_variant(const calls &c, const variant &v, int seconds, std::vector<uint8_t> &buffer) {
    std::vector<channel> channels(v.channels.size());
    std::string summary;
    for (size_t i = 0; i < channels.size(); i++) {
        channel &ch = channels[i];
        ch.spec = v.channels[i];
        ch.label = static_cast<char>('A' + i);
        ch.answers = &slots[i];
        summary += std::string(" ") + ch.label + ": " + describe(ch.spec);
    }
    LOG("variant %s start%s\n", v.name.c_str(), summary.c_str());
    // Both profiles must be free: the host or Remote Play may hold one, or our last close may still be listed for a moment.
    for (const channel &ch : channels) {
        for (int waited = 0; listed(c, ch.spec.resource) && waited < 2000; waited += 20) usleep(20000);
        if (listed(c, ch.spec.resource)) {
            LOG("variant %s skipped: ra %d is in use (active=%s)\n", v.name.c_str(), ch.spec.resource, active_list(c).c_str());
            return true;
        }
    }
    for (channel &ch : channels) {
        ch.answers->granted.store(0);
        ch.answers->revoked.store(0);
        ch.answers->refused.store(0);
        ch.answers->refused_value.store(-1);
        ch.answers->id.store(ch.spec.resource);
    }
    // Nothing of ours is registered here, so no answer comes in while this resets.
    for (event_record &e : events) e.done.store(false);
    epoch_ms.store(now_ms());
    event_count.store(0);
    logged_events = 0;

    // Register and ask RUNNING for one profile after the other, each waiting for its grant.
    for (channel &ch : channels) {
        ch.register_rc = c.reg(ch.spec.resource, reinterpret_cast<void *>(resource_event), nullptr);
        LOG("%c register ra=%d rc=%#x\n", ch.label, ch.spec.resource, static_cast<unsigned>(ch.register_rc));
        if (ch.register_rc) break;
        ch.registered = ch.requested = true;  // STOP is sent even if RUNNING reports failure, as capture.c does
        ch.running_rc = c.request(ch.spec.resource, "RUNNING", nullptr, getpid());
        LOG("%c RUNNING rc=%#x\n", ch.label, static_cast<unsigned>(ch.running_rc));
        if (ch.running_rc < 0 || !wait_grant(ch)) break;
    }
    log_new_events();
    std::string state;
    for (const channel &ch : channels)
        state += std::string(" ") + ch.label + "=" + (usable(ch) ? "granted" : ch.answers->revoked.load() ? "revoked" : "not granted");
    LOG("grants:%s active=%s\n", state.c_str(), active_list(c).c_str());

    bool initialized = false, any_usable = false;
    for (const channel &ch : channels) any_usable |= usable(ch);
    if (any_usable) {
        const int rc = c.init();
        LOG("Initialize rc=%#x\n", static_cast<unsigned>(rc));
        initialized = rc == 0;
    }
    if (initialized) {
        int output = 0;
        for (int attempt = 0; attempt < 50 && c.mode(&output) == 0 && !output; attempt++) usleep(20000);
        LOG("video out mode %#x\n", static_cast<unsigned>(output));
        if (output & 3) {
            LOG("variant %s: VR output, nothing opened\n", v.name.c_str());
        } else {
            for (channel &ch : channels)
                if (usable(ch)) open_channel(c, ch, output);
            LOG("after the opens: grants:%s%s active=%s\n", usable(channels[0]) ? " A=granted" : " A=lost",
                channels.size() > 1 ? (usable(channels[1]) ? " B=granted" : " B=lost") : "", active_list(c).c_str());
            bool any_started = false;
            for (const channel &ch : channels) any_started |= ch.started;
            if (any_started) read_all(c, channels, seconds, buffer);
        }
    }
    for (const channel &ch : channels) {
        const double kbit = static_cast<double>(ch.bytes) * 8 / 1000 / seconds;
        LOG("result %s %c %s register=%#x running=%#x grant_ms=%d open=%#x start=%#x headers=%#x/%d frames=%ld kbit_avg=%.0f "
            "max_gap_ms=%ld first_frame_ms=%ld largest=%d error=%#x error_ms=%ld revoked=%d\n",
            v.name.c_str(), ch.label, describe(ch.spec).c_str(), static_cast<unsigned>(ch.register_rc), static_cast<unsigned>(ch.running_rc),
            ch.grant_ms, static_cast<unsigned>(ch.open_rc), static_cast<unsigned>(ch.start_rc), static_cast<unsigned>(ch.headers_rc),
            ch.header_bytes, ch.frames, kbit, ch.max_gap_ms, ch.first_frame_ms, ch.largest, static_cast<unsigned>(ch.error), ch.error_ms,
            ch.answers->revoked.load());
    }
    return close_all(c, channels, initialized);
}

}  // namespace

int main() {
    char header[128];
    std::snprintf(header, sizeof header, "ProsperoSunshine: dual_open_probe firmware=%#x", kernel_get_fw_version());
    ps_diag_open("/data/prosperosunshine/dual_open_probe.log", header);
    if (probe::host_running()) {
        LOG("refused: the host is running (127.0.0.1:47989 or :47995 answers); stop it first (app: Stop host)\n");
        return 1;
    }
    std::vector<variant> variants = defaults();
    int seconds = 10;
    if (!read_conf(variants, seconds)) return 1;
    calls c;
    if (!resolve(c)) {
        LOG("refused: Avcap2 or ResourceArbitrator calls not resolved\n");
        return 1;
    }
    LOG("start: %zu variants, %d s each, active=%s\n", variants.size(), seconds, active_list(c).c_str());
    std::vector<uint8_t> buffer(8 * 1024 * 1024);
    for (const variant &v : variants) {
        if (probe::host_running()) {
            LOG("stopping: the host came up\n");
            break;
        }
        if (!run_variant(c, v, seconds, buffer)) {
            LOG("stopping: an unregister failed, a registration may be left; check that the host still streams\n");
            return 1;
        }
        sleep(1);
    }
    LOG("done\n");
    return 0;
}

