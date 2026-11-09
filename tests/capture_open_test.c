// SPDX-License-Identifier: GPL-3.0-only
// The Experimental switches through capture.c's real open path, against fake_avcap.h: which records go out in which order,
// what the host offers afterwards, and what host_diag.log gets.
#define PS_FIRST_FRAME_MS 20
#include "fake_avcap.h"
#include <assert.h>

#define START_FAILED ((int)0x80c78000)
#define INVALID ((int)0x81950001)

static const char *const marker = "/tmp/prospero_experimental_attempt";

static void set_switches(int hevc, int hdr, int large, int bitrate)
{
    int switches[PS_EXP_SWITCH_COUNT] = {0};
    switches[PS_EXP_HEVC] = hevc;
    switches[PS_EXP_HDR] = hdr;
    switches[PS_EXP_LARGE] = large;
    switches[PS_EXP_BITRATE] = bitrate;
    ps_capture_set_experimental(switches, marker);
}

static void setup(uint32_t firmware, int hevc, int hdr, int large, uint32_t codec, uint32_t ten_bit, uint32_t resolution)
{
    fake_firmware = firmware;
    fake_decide = NULL;
    fake_opens = 0;
    fake_output = 0x13c;
    ps_capture_set_reopening(0);
    fake_log[0] = 0;
    unlink(marker);
    set_switches(hevc, hdr, large, 0);
    ps_capture_set_codec(codec);
    ps_capture_set_ten_bit(ten_bit);
    ps_capture_set_video_mode(resolution, 1);
    ps_capture_set_opening_bitrate(20000000, 22000000);
    fake_log[0] = 0;
}

static int full_fields(const uint8_t *record) { return record[0x5c] == 9; }

static int open_and_close(void)
{
    ps_capture *session = NULL;
    const int rc = ps_capture_open_or_fall_back(&session);
    assert(!rc == (session != NULL));
    ps_capture_close(session);
    return rc;
}

static int logged(const char *text) { return strstr(fake_log, text) != NULL; }

static int start_needs_full(const char *step, const uint8_t *record)
{
    return !strcmp(step, "Start") && !full_fields(record) ? START_FAILED : 0;
}
static int open_always_fails(const char *step, const uint8_t *record)
{
    (void)record;
    return !strcmp(step, "OpenVideo") ? INVALID : 0;
}
static int no_frame_unless_full(const char *step, const uint8_t *record)
{
    return !strcmp(step, "read") && !full_fields(record) ? AGAIN : 0;
}
static int read_fails(const char *step, const uint8_t *record)
{
    (void)record;
    return !strcmp(step, "read") ? START_FAILED : 0;
}
static int large_fails(const char *step, const uint8_t *record)
{
    uint32_t resolution;
    memcpy(&resolution, record + 0x18, sizeof resolution);
    return !strcmp(step, "OpenVideo") && resolution >= 5 ? INVALID : 0;
}
// The output in front can't hold a larger mode (issue 39): OpenVideo answers RESOURCE_NOT_ACTIVE.
static int large_refused(const char *step, const uint8_t *record)
{
    uint32_t resolution;
    memcpy(&resolution, record + 0x18, sizeof resolution);
    return !strcmp(step, "OpenVideo") && resolution >= 5 ? RESOURCE_NOT_ACTIVE : 0;
}
// A rate above 50 Mbit/s fails to start, whatever else the record holds.
static int high_rate_fails(const char *step, const uint8_t *record)
{
    uint32_t average;
    memcpy(&average, record + 0xc8, sizeof average);
    return !strcmp(step, "Start") && average > PS_EXP_SAFE_AVERAGE ? START_FAILED : 0;
}
static int full_and_high_rate_fail(const char *step, const uint8_t *record)
{
    return full_fields(record) ? INVALID : high_rate_fails(step, record);
}
static void setup_high(uint32_t firmware, int hevc, uint32_t codec, uint32_t average)
{
    setup(firmware, hevc, 0, 0, codec, 0, 4);
    set_switches(hevc, 0, 0, 1);
    ps_capture_set_opening_bitrate(average, average / 100 * 112);
    fake_log[0] = 0;
}
// The switch for modes below 1080p alone.
static void setup_small(uint32_t firmware, int small, uint32_t codec, uint32_t resolution)
{
    setup(firmware, 0, 0, 0, codec, 0, resolution);
    int switches[PS_EXP_SWITCH_COUNT] = {0};
    switches[PS_EXP_SMALL] = small;
    ps_capture_set_experimental(switches, marker);
    fake_log[0] = 0;
}
static int small_fails(const char *step, const uint8_t *record)
{
    uint32_t resolution;
    memcpy(&resolution, record + 0x18, sizeof resolution);
    return !strcmp(step, "OpenVideo") && resolution < 4 ? INVALID : 0;
}
// While the output has no mode OpenVideo refuses every mode, as the service does while a title starts or closes.
static int refused_without_output(const char *step, const uint8_t *record)
{
    (void)record;
    return !strcmp(step, "OpenVideo") && !fake_output ? RESOURCE_NOT_ACTIVE : 0;
}
static uint32_t record_u32(size_t at)
{
    uint32_t value;
    memcpy(&value, fake_record + at, sizeof value);
    return value;
}

static int refuse_resource(const char *step, const uint8_t *record)
{
    (void)record;
    return !strcmp(step, "grant") ? 3 : 0;
}
static int revoke_resource(const char *step, const uint8_t *record)
{
    (void)record;
    return !strcmp(step, "grant") ? FAKE_REVOKE : 0;
}
static int no_answer(const char *step, const uint8_t *record)
{
    (void)record;
    return !strcmp(step, "grant") ? FAKE_SILENT : 0;
}

static int reads;
static int count_reads(const char *step, const uint8_t *record)
{
    (void)record;
    if (!strcmp(step, "read")) reads++;
    return 0;
}
// 10.x firmware that doesn't know the slice fields: Start fails while cfg+0x44 is set.
static int start_refuses_slices(const char *step, const uint8_t *record)
{
    return !strcmp(step, "Start") && record[0x44] ? START_FAILED : 0;
}
static int marker_seen;
static int look_for_marker(const char *step, const uint8_t *record)
{
    (void)record;
    if (!strcmp(step, "OpenVideo")) marker_seen = access(marker, F_OK) == 0;
    return 0;
}

int main(void)
{
    // 13.60: the minimal variant fails at Start, the full one runs and stays.
    setup(0x13600000u, 1, 0, 0, 1, 0, 4);
    assert(ps_capture_hevc_allowed() && !ps_capture_hdr_allowed() && !ps_capture_large_modes_allowed());
    fake_decide = start_needs_full;
    assert(open_and_close() == 0);
    assert(fake_opens == 2);
    assert(logged("experimental attempt switches=hevc firmware=0x13600000 trial=hevc codec=hevc bits=8 mode=1920x1080 fps=60 kbit=20000 variant=minimal\n"));
    assert(logged("experimental result=failed switches=hevc firmware=0x13600000 trial=hevc codec=hevc bits=8 mode=1920x1080 fps=60 kbit=20000 "
                  "variant=minimal error=-1 rc=0x80c78000 step=Start\n"));
    assert(logged("experimental result=ok switches=hevc firmware=0x13600000 trial=hevc codec=hevc bits=8 mode=1920x1080 fps=60 kbit=20000 "
                  "variant=full error=0 rc=0 step=none\n"));
    assert(!logged("disabled"));
    // The full variant on 13.60: what 13.00 keeps from 0x198 on sits 8 higher, also the encoder's memory.
    uint64_t at_shifted;
    memcpy(&at_shifted, fake_record + 0x198 + 8, sizeof at_shifted);
    assert(at_shifted == 0x50 && fake_record[0x1a8 + 8] == 0x11 && fake_record[0x1c8 + 8] == 0x22 && fake_record[0x228] == 0xff);
    uint32_t last_field;
    memcpy(&last_field, fake_record + 0x2c4 + 8, sizeof last_field);
    assert(last_field == 0x6e);
    fake_log[0] = 0;
    fake_opens = 0;
    assert(open_and_close() == 0);
    assert(fake_opens == 1 && logged("trial=none") && logged("variant=full"));
    assert(ps_capture_hevc_allowed());

    // A session that passed its trial counts as current, or every user would reopen it.
    setup(0x13600000u, 1, 0, 0, 1, 0, 4);
    ps_capture *current = NULL;
    assert(ps_capture_open_or_fall_back(&current) == 0 && ps_capture_mode_current(current));
    ps_capture_close(current);

    // The minimal variant alone: nothing but cfg+0x24.
    setup(0x13600000u, 1, 0, 0, 1, 0, 4);
    assert(open_and_close() == 0);
    assert(fake_opens == 1 && !full_fields(fake_record) && fake_record[0x24] == 1);
    uint64_t memory_header;
    memcpy(&memory_header, fake_record + 0x198 + 8, sizeof memory_header);
    assert(memory_header != 0x50);
    assert(logged("result=ok") && logged("variant=minimal"));

    // Both variants fail: HEVC is off until the host restarts, and the next stream gets H.264 without any trial.
    setup(0x13600000u, 1, 0, 0, 1, 0, 4);
    fake_decide = open_always_fails;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(fake_opens == 2);
    assert(logged("variant=minimal error=-1 rc=0x81950001 step=OpenVideo") && logged("variant=full error=-1 rc=0x81950001 step=OpenVideo"));
    assert(logged("experimental disabled feature=hevc\n"));
    assert(!ps_capture_hevc_allowed() && atomic_load(&video_codec[0]) == 0);
    fake_decide = NULL;
    fake_opens = 0;
    fake_log[0] = 0;
    assert(open_and_close() == 0);
    assert(fake_opens == 1 && logged("codec=h264") && logged("trial=none") && logged("variant=standard"));

    // No first frame in time, then a read that fails: the steps the records can't show.
    setup(0x13600000u, 1, 0, 0, 1, 0, 4);
    fake_decide = no_frame_unless_full;
    assert(open_and_close() == 0);
    assert(fake_opens == 2 && logged("variant=minimal error=-1 rc=0 step=first frame") && logged("variant=full error=0"));
    setup(0x13600000u, 1, 0, 0, 1, 0, 4);
    fake_decide = read_fails;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(logged("rc=0x80c78000 step=first frame") && !ps_capture_hevc_allowed());

    // HDR fails and HEVC stays with its own switch; with the HDR switch alone both go, because HDR was all that offered HEVC.
    setup(0x13600000u, 1, 1, 0, 1, 1, 4);
    assert(ps_capture_hdr_allowed());
    fake_decide = open_always_fails;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(logged("trial=hdr codec=hevc bits=10") && logged("experimental disabled feature=hdr\n") && !logged("feature=hevc"));
    assert(!ps_capture_hdr_allowed() && ps_capture_hevc_allowed() && atomic_load(&video_ten_bit[0]) == 0 && atomic_load(&video_codec[0]) == 1);
    setup(0x13600000u, 0, 1, 0, 1, 1, 4);
    assert(ps_capture_hevc_allowed());
    fake_decide = open_always_fails;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(!ps_capture_hdr_allowed() && !ps_capture_hevc_allowed() && atomic_load(&video_codec[0]) == 0);

    // A larger mode that fails gives 1080p in the same stream, and stays off.
    setup(0x13600000u, 0, 0, 1, 0, 0, 6);
    assert(ps_capture_large_modes_allowed());
    fake_decide = large_fails;
    assert(open_and_close() == 0);
    assert(fake_opens == 2 && ps_capture_video_resolution() == 4);
    assert(logged("trial=large codec=h264 bits=8 mode=3840x2160") && logged("experimental disabled feature=large\n"));
    assert(!ps_capture_large_modes_allowed());

    // HEVC at 4K: both variants at 4K, then at 1080p, where the first one runs. The mode was at fault, not the codec.
    setup(0x13600000u, 1, 0, 1, 1, 0, 6);
    fake_decide = large_fails;
    assert(open_and_close() == 0);
    assert(fake_opens == 3 && ps_capture_video_resolution() == 4);
    assert(logged("mode=1920x1080 fps=60 kbit=5000 variant=minimal error=0") && logged("feature=large") && !logged("feature=hevc"));
    assert(logged("fallback to 1080p at 5000 kbit/s, 20000 asked for the larger mode"));
    assert(ps_capture_hevc_allowed() && !ps_capture_large_modes_allowed());

    // The output in front refuses 1440p (issue 39): 1080p for this stream at 1080p's share of the rate, the switch stays on
    // trial, and the next stream asks for 1440p again.
    setup(0x13600000u, 0, 0, 1, 0, 0, 5);
    fake_decide = large_refused;
    assert(open_and_close() == 0);
    assert(ps_capture_video_resolution() == 4 && ps_capture_opening_average() == 11250000u);
    assert(logged("trial=large codec=h264 bits=8 mode=2560x1440") && !logged("disabled"));
    assert(ps_capture_large_modes_allowed());
    ps_capture_set_video_mode(5, 1);
    ps_capture_set_opening_bitrate(20000000u, 22400000u);
    fake_log[0] = 0;
    fake_decide = NULL;
    assert(open_and_close() == 0);
    assert(ps_capture_video_resolution() == 5 && logged("trial=large") && logged("result=ok"));

    // A smaller mode that fails gives 1080p in the same stream, and stays off. 10.40 has them unverified.
    setup_small(0x10400000u, 1, 0, 2);
    assert(ps_capture_small_modes_allowed() && !ps_capture_large_modes_allowed());
    fake_decide = small_fails;
    assert(open_and_close() == 0);
    assert(fake_opens == 3 && ps_capture_video_resolution() == 4 && record_u32(0x18) == 4);
    assert(logged("trial=small codec=h264 bits=8 mode=960x540") && logged("experimental disabled feature=small\n"));
    assert(!ps_capture_small_modes_allowed());
    // It runs: the record asks for the mode, with the resource 1080p uses.
    setup_small(0x10400000u, 1, 0, 1);
    assert(open_and_close() == 0);
    assert(fake_opens == 1 && record_u32(0x18) == 1 && record_u32(0x28) == 180);
    assert(logged("trial=small codec=h264 bits=8 mode=640x360") && logged("result=ok") && ps_capture_small_modes_allowed());
    // Once it ran, a refusal (a title starting, say) gives 1080p for that stream, and the switch stays for the next one.
    fake_opens = 0;
    fake_log[0] = 0;
    fake_decide = small_fails;
    assert(open_and_close() == 0);
    assert(fake_opens == 3 && ps_capture_video_resolution() == 4 && record_u32(0x18) == 4);
    assert(logged("trial=none") && !logged("disabled"));
    assert(ps_capture_small_modes_allowed());
    // 10.20 has them verified: no switch, no trial and no frame read; a refusal gives 1080p, as on every firmware.
    setup_small(0x10200006u, 0, 0, 3);
    assert(ps_capture_small_modes_allowed());
    reads = 0;
    fake_decide = count_reads;
    assert(open_and_close() == 0);
    assert(reads == 1 && fake_opens == 1 && record_u32(0x18) == 3 && !logged("experimental"));
    setup_small(0x10200006u, 0, 0, 3);
    fake_decide = small_fails;
    assert(open_and_close() == 0);
    assert(fake_opens == 3 && ps_capture_video_resolution() == 4 && record_u32(0x18) == 4);
    assert(!logged("experimental") && ps_capture_small_modes_allowed());
    // A reopen while the output has no mode, as while a title starts or closes: below 1080p it keeps the mode, and the
    // next reopen, once the title's mode is there, gets it.
    setup_small(0x10200006u, 0, 0, 2);
    fake_output = 0;
    fake_decide = refused_without_output;
    ps_capture_set_reopening(1);
    assert(open_and_close() != 0);
    assert(fake_opens == 6 && ps_capture_video_resolution() == 2 && logged("output not settled"));
    fake_output = 0x44;
    fake_opens = 0;
    assert(open_and_close() == 0);
    assert(fake_opens == 1 && record_u32(0x18) == 2);
    // The first open of a stream falls back as before.
    setup_small(0x10200006u, 0, 0, 2);
    fake_output = 0;
    fake_decide = refused_without_output;
    assert(open_and_close() != 0);
    assert(ps_capture_video_resolution() == 4 && !logged("output not settled") && !ps_capture_size_refused());
    // A reopen keeps the stream's size (issue 39): refused by a settled output, it fails and says the stream can't go on.
    setup_small(0x10200006u, 0, 0, 2);
    fake_decide = small_fails;
    ps_capture_set_reopening(1);
    assert(open_and_close() != 0);
    assert(ps_capture_video_resolution() == 2 && logged("output refuses resolution 2 now") && ps_capture_size_refused());
    // Above 1080p alike: while the output has no mode the next reopen may get it, ...
    setup(0x13000040u, 1, 0, 0, 1, 0, 6);
    fake_output = 0;
    fake_decide = refused_without_output;
    ps_capture_set_reopening(1);
    assert(open_and_close() != 0);
    assert(ps_capture_video_resolution() == 6 && logged("output not settled") && !ps_capture_size_refused());
    fake_output = 0x44;
    fake_log[0] = 0;
    assert(open_and_close() == 0);
    assert(ps_capture_video_resolution() == 6 && !ps_capture_size_refused());
    // ... and an output that can't hold it, as with a 1080p video app in front, ends the stream instead of a picture
    // the client can't decode.
    setup(0x13000040u, 0, 0, 0, 0, 0, 6);
    fake_decide = large_refused;
    ps_capture_set_reopening(1);
    assert(open_and_close() != 0);
    assert(ps_capture_video_resolution() == 6 && logged("output refuses resolution 6 now") && ps_capture_size_refused());
    // At 1080p there is nothing to fall back to: the reopen fails and is tried again, as before.
    setup(0x13000040u, 0, 0, 0, 0, 0, 4);
    fake_output = 0;
    fake_decide = refused_without_output;
    ps_capture_set_reopening(1);
    assert(open_and_close() != 0);
    assert(ps_capture_video_resolution() == 4 && !ps_capture_size_refused());
    // 13.00 has them verified too, with the same fallback.
    setup_small(0x13000040u, 0, 0, 3);
    assert(ps_capture_small_modes_allowed());
    fake_decide = small_fails;
    assert(open_and_close() == 0);
    assert(fake_opens == 2 && ps_capture_video_resolution() == 4 && !logged("experimental") && ps_capture_small_modes_allowed());
    // HEVC there needs a smaller frame budget per mode, the largest measured to start; 1080p keeps its own.
    {
        static const struct { uint32_t resolution, budget; } budgets[] = {{3, 0x40000}, {2, 0x20000}, {1, 0x20000}, {4, 0x60000}};
        for (size_t i = 0; i < sizeof budgets / sizeof budgets[0]; i++) {
            setup_small(0x13000040u, 0, 1, budgets[i].resolution);
            assert(open_and_close() == 0);
            assert(fake_opens == 1 && record_u32(0x18) == budgets[i].resolution && record_u32(0x10c) == budgets[i].budget);
        }
    }

    // From 1080p up the switch changes nothing: the same record, one open, no first-frame check (but for H.264 on 10.x, for
    // the slice fields), also where it counts (10.40).
    {
        static const uint32_t firmwares[] = {0x13000040u, 0x10200006u, 0x10400000u};
        for (size_t f = 0; f < sizeof firmwares / sizeof firmwares[0]; f++)
            for (uint32_t codec = 0; codec < 2; codec++)
                for (uint32_t resolution = 4; resolution <= 6; resolution++) {
                    setup_small(firmwares[f], 0, codec, resolution);
                    assert(open_and_close() == 0);
                    uint8_t without[sizeof fake_record];
                    memcpy(without, fake_record, sizeof without);
                    setup_small(firmwares[f], 1, codec, resolution);
                    reads = 0;
                    fake_decide = count_reads;
                    assert(open_and_close() == 0);
                    assert(fake_opens == 1 && reads == (firmwares[f] < 0x13000000u && codec == 0) && !memcmp(without, fake_record, sizeof without));
                    assert(!logged("trial=small") && ps_capture_video_resolution() == resolution);
                }
    }

    // H.264 on 10.x asks for 4 slices per frame. Where the firmware refuses that, the same open goes on without the fields,
    // and they stay off; a failure that has another cause keeps them. 13.00 and HEVC never get them.
    setup(0x10200006u, 0, 0, 0, 0, 0, 4);
    assert(open_and_close() == 0);
    assert(fake_opens == 1 && record_u32(0x40) == 1 && record_u32(0x44) == 4 && record_u32(0x48) == 1);
    assert(logged("first frame has"));
    setup(0x10200006u, 0, 0, 0, 0, 0, 4);
    fake_decide = open_always_fails;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(fake_opens == 2 && logged("failed without them too, kept") && !atomic_load(&slices_refused));
    setup(0x10200006u, 0, 0, 0, 0, 0, 4);
    fake_decide = start_refuses_slices;
    assert(open_and_close() == 0);
    assert(fake_opens == 2 && record_u32(0x44) == 0 && logged("refused, left out from now on"));
    fake_opens = 0;
    assert(open_and_close() == 0);
    assert(fake_opens == 1 && record_u32(0x40) == 0 && record_u32(0x44) == 0);
    atomic_store(&slices_refused, 0);
    setup(0x13000040u, 0, 0, 0, 0, 0, 4);
    assert(open_and_close() == 0 && record_u32(0x44) == 0);
    setup(0x10200006u, 0, 0, 0, 1, 0, 4);
    assert(open_and_close() == 0 && record_u32(0x40) == 0 && record_u32(0x44) == 0);

    // HEVC at 4K fails everywhere: HEVC is off, and 1440p and 4K are still unproven with H.264.
    setup(0x13600000u, 1, 0, 1, 1, 0, 6);
    fake_decide = open_always_fails;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(fake_opens == 4 && !ps_capture_hevc_allowed() && ps_capture_large_modes_allowed() && !logged("feature=large"));

    // Other 10.x: 13.00's fields are left out there anyway, so there is one variant to try.
    setup(0x10400000u, 1, 0, 0, 1, 0, 4);
    fake_decide = open_always_fails;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(fake_opens == 1 && !ps_capture_hevc_allowed());

    // A capture in use says nothing about the function.
    setup(0x13600000u, 1, 0, 0, 1, 0, 4);
    ps_capture *held = NULL;
    assert(ps_capture_open(&held) == 0);
    assert(open_and_close() == PS_CAPTURE_BUSY);
    assert(ps_capture_hevc_allowed() && !logged("disabled") && logged("step=in use"));
    ps_capture_close(held);

    // The attempt under way is on file, and gone once it ended.
    setup(0x13600000u, 1, 0, 0, 1, 0, 4);
    marker_seen = 0;
    fake_decide = look_for_marker;
    assert(open_and_close() == 0);
    assert(marker_seen && access(marker, F_OK) != 0);

    // A host that went down inside an attempt: the next one says so and keeps the function off for its run.
    setup(0x13600000u, 1, 0, 1, 1, 0, 4);
    FILE *left = fopen(marker, "w");
    fputs("switches=hevc,large firmware=0x13600000 trial=hevc,large codec=hevc bits=8 mode=3840x2160 fps=60 kbit=20000 variant=minimal", left);
    fclose(left);
    fake_log[0] = 0;
    set_switches(1, 0, 1, 0);
    assert(logged("experimental result=failed switches=hevc,large firmware=0x13600000 trial=hevc,large codec=hevc bits=8 "
                  "mode=3840x2160 fps=60 kbit=20000 variant=minimal error=0 rc=0 step=host went down\n"));
    assert(logged("feature=hevc") && logged("feature=large"));
    assert(!ps_capture_hevc_allowed() && !ps_capture_large_modes_allowed() && access(marker, F_OK) != 0);

    // Verified firmware: the switches change nothing, not even the first frame's check.
    setup(0x13000040u, 1, 1, 1, 1, 1, 6);
    assert(ps_capture_hevc_allowed() && ps_capture_hdr_allowed() && ps_capture_large_modes_allowed());
    reads = 0;
    fake_decide = count_reads;
    assert(open_and_close() == 0);
    assert(reads == 0 && fake_log[0] && !logged("experimental"));
    uint8_t with_switches[sizeof fake_record];
    memcpy(with_switches, fake_record, sizeof with_switches);
    setup(0x13000040u, 0, 0, 0, 1, 1, 6);
    assert(open_and_close() == 0);
    assert(!memcmp(with_switches, fake_record, sizeof with_switches));
    fake_log[0] = 0;
    set_switches(1, 1, 1, 1);
    assert(logged("experimental switches=none set=hevc,hdr,large,bitrate firmware=0x13000040\n"));

    // 10.20: HEVC and HDR are verified, 1440p and 4K are not.
    setup(0x10200006u, 1, 1, 1, 1, 0, 4);
    assert(ps_capture_hevc_allowed() && ps_capture_hdr_allowed() && ps_capture_large_modes_allowed());
    reads = 0;
    fake_decide = count_reads;
    assert(open_and_close() == 0);
    assert(reads == 0 && logged("switches=large") && logged("trial=none") && logged("variant=standard"));
    setup(0x10200006u, 0, 0, 0, 1, 0, 6);
    assert(!ps_capture_large_modes_allowed());

    // All off on untested firmware: what the host did before, without a line more, and a failure stays a plain failure.
    setup(0x13600000u, 0, 0, 0, 1, 0, 4);
    assert(!ps_capture_hevc_allowed() && !ps_capture_large_modes_allowed());
    fake_decide = open_always_fails;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(fake_opens == 1 && !logged("experimental"));
    setup(0x13600000u, 0, 0, 0, 0, 0, 6);
    fake_decide = large_fails;
    assert(open_and_close() == 0);
    assert(fake_opens == 2 && ps_capture_video_resolution() == 4 && !logged("experimental"));

    // High bitrate: it fails at 100 Mbit/s, the stream runs at 50, and 50 stays until the host restarts.
    setup_high(0x13600000u, 0, 0, 100000000u);
    assert(ps_capture_high_bitrate_allowed());
    fake_decide = high_rate_fails;
    assert(open_and_close() == 0);
    assert(fake_opens == 2 && record_u32(0xc8) == 50000000u && record_u32(0xcc) == 56000000u);
    assert(ps_capture_opening_average() == 50000000u);
    assert(logged("experimental result=failed switches=bitrate firmware=0x13600000 trial=bitrate codec=h264 bits=8 mode=1920x1080 fps=60 "
                  "kbit=100000 variant=standard error=-1 rc=0x80c78000 step=Start\n"));
    assert(logged("experimental result=ok switches=bitrate firmware=0x13600000 trial=none codec=h264 bits=8 mode=1920x1080 fps=60 "
                  "kbit=50000 variant=standard error=0 rc=0 step=none\n"));
    assert(logged("experimental disabled feature=bitrate\n") && !ps_capture_high_bitrate_allowed());

    // It runs: the record is the one the same rate gets without the switch, and the rate stays allowed.
    setup_high(0x13600000u, 0, 0, 100000000u);
    assert(open_and_close() == 0);
    assert(fake_opens == 1 && logged("trial=bitrate") && logged("result=ok") && ps_capture_high_bitrate_allowed());
    uint8_t high_record[sizeof fake_record];
    memcpy(high_record, fake_record, sizeof high_record);
    setup(0x13600000u, 0, 0, 0, 0, 0, 4);
    ps_capture_set_opening_bitrate(100000000u, 112000000u);
    assert(open_and_close() == 0);
    assert(!memcmp(high_record, fake_record, sizeof high_record));

    // At 50 or below the switch has nothing to show: no trial, no frame read.
    setup_high(0x13600000u, 0, 0, 50000000u);
    reads = 0;
    fake_decide = count_reads;
    assert(open_and_close() == 0);
    assert(reads == 0 && logged("trial=none") && logged("kbit=50000"));

    // It fails at 50 too: the rate wasn't at fault, and nothing goes off.
    setup_high(0x13600000u, 0, 0, 100000000u);
    fake_decide = open_always_fails;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(fake_opens == 2 && !logged("disabled") && ps_capture_high_bitrate_allowed());

    // HEVC at a high rate: both variants, then 50 from the first variant on, where the rate turns out to be the cause.
    setup_high(0x13600000u, 1, 1, 100000000u);
    fake_decide = full_and_high_rate_fail;
    assert(open_and_close() == 0);
    assert(fake_opens == 3 && record_u32(0xc8) == 50000000u && !full_fields(fake_record));
    assert(logged("feature=bitrate") && !logged("feature=hevc") && ps_capture_hevc_allowed() && !ps_capture_high_bitrate_allowed());

    // A host that went down at a high rate: 50 for its successor's run.
    setup_high(0x13600000u, 0, 0, 100000000u);
    left = fopen(marker, "w");
    fputs("switches=bitrate firmware=0x13600000 trial=bitrate codec=h264 bits=8 mode=1920x1080 fps=60 kbit=100000 variant=standard", left);
    fclose(left);
    fake_log[0] = 0;
    set_switches(0, 0, 0, 1);
    assert(logged("kbit=100000 variant=standard error=0 rc=0 step=host went down\n") && logged("feature=bitrate"));
    assert(!ps_capture_high_bitrate_allowed());

    // 13.00 has it verified: the switch changes nothing, and a failure lowers nothing.
    setup_high(0x13000040u, 0, 0, 100000000u);
    assert(ps_capture_high_bitrate_allowed());
    fake_decide = high_rate_fails;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(fake_opens == 1 && ps_capture_opening_average() == 100000000u && !logged("experimental"));
    // On 10.x the record has no rate fields: the switch is ignored, and the rate reaches the record only as the QP.
    setup_high(0x10400000u, 0, 0, 100000000u);
    assert(!ps_capture_high_bitrate_allowed());
    assert(open_and_close() == 0);
    assert(!logged("experimental"));
    assert(record_u32(0xc4) == 21 && record_u32(0xc8) == 25);
    memcpy(high_record, fake_record, sizeof high_record);
    setup(0x10400000u, 0, 0, 0, 0, 0, 4);
    ps_capture_set_opening_bitrate(10000000, 11000000);
    assert(open_and_close() == 0);
    assert(record_u32(0xc4) == 27 && record_u32(0xc8) == 31);
    memcpy(high_record + 0xc4, fake_record + 0xc4, 8);
    assert(!memcmp(high_record, fake_record, sizeof high_record));
    fake_log[0] = 0;
    set_switches(0, 0, 0, 1);
    assert(logged("experimental switches=none set=bitrate firmware=0x10400000\n"));
    // HEVC 8 bit stops at 24/28 (issue 50); HEVC 10 bit goes on to 21/25.
    setup(0x10200000u, 1, 1, 0, 1, 0, 4);
    ps_capture_set_opening_bitrate(22988000, 25746000);
    assert(open_and_close() == 0);
    assert(record_u32(0xc4) == 24 && record_u32(0xc8) == 28);
    setup(0x10200000u, 1, 1, 0, 1, 1, 4);
    ps_capture_set_opening_bitrate(22988000, 25746000);
    assert(open_and_close() == 0);
    assert(record_u32(0xc4) == 21 && record_u32(0xc8) == 25);

    // The wait for the resource: which of three endings it was, how long it took, and who else is active.
    fake_active[0] = 175;
    fake_active[1] = 177;
    setup(0x13600000u, 0, 0, 0, 0, 0, 4);
    fake_decide = refuse_resource;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(logged("capture RUNNING outcome=refused value=3 waited_ms=") && logged("list_rc=0 active=175,177\n"));
    assert(logged("capture open failed step=RUNNING"));
    setup(0x13600000u, 0, 0, 0, 0, 0, 4);
    fake_decide = revoke_resource;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(logged("capture RUNNING outcome=revoked value=-1 waited_ms="));
    setup(0x13600000u, 0, 0, 0, 0, 0, 4);
    fake_decide = no_answer;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(logged("capture RUNNING outcome=timeout value=-1 waited_ms=6000 list_rc=0 active=175,177\n"));
    fake_active[0] = fake_active[1] = 0;
    setup(0x13600000u, 0, 0, 0, 0, 0, 4);
    fake_decide = no_answer;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(logged("active=none\n"));
    setup(0x13000040u, 0, 0, 0, 0, 0, 4);
    assert(open_and_close() == 0 && !logged("outcome="));

    // Where 13.00's layout holds, the full variant is the record 13.00 has always sent; on 10.x the minimal one is.
    uint8_t standard[0x2e8], variant[0x2e8];
    setup(0x12400000u, 0, 0, 0, 1, 0, 4);
    memset(standard, 0, sizeof standard);
    memset(variant, 0, sizeof variant);
    configure(0, standard, 0x13c, PS_EXP_STANDARD);
    configure(0, variant, 0x13c, PS_EXP_FULL);
    assert(!memcmp(standard, variant, sizeof standard));
    setup(0x10400000u, 0, 0, 0, 1, 0, 4);
    memset(standard, 0, sizeof standard);
    memset(variant, 0, sizeof variant);
    configure(0, standard, 0x13c, PS_EXP_STANDARD);
    configure(0, variant, 0x13c, PS_EXP_MINIMAL);
    assert(!memcmp(standard, variant, sizeof standard));
    unlink(marker);

    puts("PASS: experimental switches through the open path (variants, first frame, fallback, off until restart, verified firmware)");
    return 0;
}
