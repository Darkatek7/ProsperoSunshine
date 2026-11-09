// SPDX-License-Identifier: GPL-3.0-only
// The second capture channel (slot 1, ResourceArbitrator profile 175) through capture.c's real open path, against
// fake_avcap.h: its records, both channels open at once, and slot 0 untouched by what slot 1 does.
#include "fake_avcap.h"
#include <assert.h>

#define INVALID_MODE ((int)0x81950001)

static int logged(const char *text) { return strstr(fake_log, text) != NULL; }

static uint32_t record_u32(const uint8_t *record, size_t at)
{
    uint32_t value;
    memcpy(&value, record + at, sizeof value);
    return value;
}

static void set_slot(int slot, uint32_t codec, uint32_t ten_bit, uint32_t resolution, uint32_t timing, uint32_t average)
{
    assert(ps_capture_slot_set_codec(slot, codec) == 0);
    ps_capture_slot_set_ten_bit(slot, ten_bit);
    assert(ps_capture_slot_set_video_mode(slot, resolution, timing) == 0);
    ps_capture_slot_set_opening_bitrate(slot, average, average / 100 * (codec ? 110 : 112));
}

static int small_fails(const char *step, const uint8_t *record)
{
    return !strcmp(step, "OpenVideo") && record_u32(record, 0x18) < 4 ? INVALID_MODE : 0;
}

static int read_one(ps_capture *s)
{
    uint8_t buffer[64], descriptor[0x88];
    return s->mode[2] == 1 ? ps_capture_read2(s, buffer, sizeof buffer, descriptor) : ps_capture_read(s, buffer, sizeof buffer, descriptor);
}

int main(void)
{
    static const uint32_t firmwares[] = {0x13000040u, 0x13600000u, 0x10200006u};
    int records = 0;
    const int switches[PS_EXP_SWITCH_COUNT] = {0};
    ps_capture_set_experimental(switches, NULL);

    // Slot 1 writes the record slot 0 writes for the same mode, with its own resource; up to 1080p, on every layout.
    for (size_t f = 0; f < sizeof firmwares / sizeof firmwares[0]; f++)
        for (uint32_t codec = 0; codec < 2; codec++)
            for (uint32_t resolution = 1; resolution <= 4; resolution++)
                for (uint32_t timing = 0; timing < 2; timing++)
                    for (uint32_t ten_bit = 0; ten_bit < 2; ten_bit++) {
                        fake_firmware = firmwares[f];
                        set_slot(0, codec, ten_bit, resolution, timing, 20000000);
                        set_slot(1, codec, ten_bit, resolution, timing, 20000000);
                        assert(atomic_load(&resource_id[0]) == 180 && atomic_load(&resource_id[1]) == 175);
                        ps_capture *first = NULL, *second = NULL;
                        assert(ps_capture_slot_open(0, &first) == 0);
                        uint8_t on_first[sizeof fake_record];
                        memcpy(on_first, fake_record, sizeof on_first);
                        assert(ps_capture_slot_open(1, &second) == 0);
                        assert(record_u32(on_first, 0x28) == 180 && record_u32(fake_record, 0x28) == 175);
                        memcpy(on_first + 0x28, fake_record + 0x28, 4);
                        assert(!memcmp(on_first, fake_record, sizeof on_first));
                        // configure() agrees with what OpenVideo got, and with the probes' builder.
                        uint8_t configured[0x2e8] = {0}, built[0x2e8] = {0};
                        configure(1, configured, 0x13c, PS_EXP_STANDARD);
                        const ps_capture_video_config values = {resolution, timing, codec, ten_bit, 20000000, 20000000 / 100 * (codec ? 110 : 112), 175, 0, 0};
                        ps_capture_build_config(built, &values, 0x13c, PS_EXP_STANDARD);
                        assert(!memcmp(configured, built, sizeof built));
                        if (!(codec == 1 && record_shift() >= 0)) assert(!memcmp(configured, fake_record, sizeof configured));
                        assert(ps_capture_close(second) == 0 && ps_capture_close(first) == 0);
                        records++;
                    }
    fake_firmware = 0x13000040u;

    // Slot 1 refuses the modes above 1080p, and keeps what it had; slot 0 still takes them, on 182.
    set_slot(1, 0, 0, 3, 1, 10000000);
    assert(ps_capture_slot_set_video_mode(1, 5, 1) == PS_CAPTURE_ERROR && ps_capture_slot_set_video_mode(1, 6, 1) == PS_CAPTURE_ERROR);
    assert(ps_capture_slot_video_resolution(1) == 3 && atomic_load(&resource_id[1]) == 175);
    assert(ps_capture_slot_set_video_mode(0, 6, 1) == 0 && atomic_load(&resource_id[0]) == 182 && ps_capture_video_resolution() == 6);
    // No third slot.
    ps_capture *none = NULL;
    assert(ps_capture_slot_set_video_mode(2, 4, 1) == PS_CAPTURE_ERROR && ps_capture_slot_set_codec(-1, 0) == PS_CAPTURE_ERROR);
    assert(ps_capture_slot_open(2, &none) == PS_CAPTURE_ERROR && ps_capture_slot_open_or_fall_back(-1, &none) == PS_CAPTURE_ERROR);
    assert(!none);

    // Both at once: HEVC 1080p on slot 0, H.264 720p on slot 1. Avcap2 is initialized once and terminated with the last.
    set_slot(0, 1, 0, 4, 1, 20000000);
    set_slot(1, 0, 0, 3, 1, 10000000);
    fake_initialized = fake_terminated = 0;
    fake_log[0] = 0;
    ps_capture *first = NULL, *second = NULL;
    assert(ps_capture_slot_open_or_fall_back(0, &first) == 0 && ps_capture_slot_open_or_fall_back(1, &second) == 0);
    assert(fake_initialized == 1 && fake_terminated == 0);
    assert(logged("ProsperoSunshine: capture open start codec=1 resource=180\n"));
    assert(logged("ProsperoSunshine: capture2 open start codec=0 resource=175\n"));
    assert(logged("capture2 Initialize done by the other channel\n") && logged("capture2 open ok\n"));
    assert(record_u32(fake_record, 0x18) == 3 && record_u32(fake_record, 0x24) == 0 && record_u32(fake_record, 0x28) == 175);
    // Each slot holds one session.
    ps_capture *third = NULL;
    assert(ps_capture_slot_open(0, &third) == PS_CAPTURE_BUSY && ps_capture_slot_open(1, &third) == PS_CAPTURE_BUSY && !third);
    // Each session is current with its own slot's settings only.
    assert(ps_capture_mode_current(first) && ps_capture_mode_current(second));
    ps_capture_slot_set_opening_bitrate(1, 8000000, 8960000);
    assert(ps_capture_mode_current(first) && !ps_capture_mode_current(second));
    ps_capture_slot_set_opening_bitrate(1, 10000000, 11200000);
    assert(ps_capture_mode_current(second));
    assert(read_one(first) == 16 && read_one(second) == 16);
    // The arbitrator takes slot 0's resource away: slot 1 reads on.
    fake_callback(180, 2, NULL);
    assert(read_one(first) == PS_CAPTURE_REVOKED && read_one(second) == 16);
    fake_log[0] = 0;
    assert(ps_capture_close(first) == 0);
    assert(fake_terminated == 0 && logged("capture Terminate left to the other channel\n") && !logged("capture2"));
    assert(read_one(second) == 16);
    // Slot 0 opens again next to it, without a second Initialize; slot 1's answer goes to slot 1 only.
    assert(ps_capture_slot_open(0, &first) == 0 && fake_initialized == 1);
    fake_callback(175, 2, NULL);
    assert(read_one(first) == 16 && read_one(second) == PS_CAPTURE_REVOKED);
    fake_log[0] = 0;
    assert(ps_capture_close(second) == 0 && fake_terminated == 0 && logged("capture2 Terminate left to the other channel\n"));
    assert(ps_capture_close(first) == 0 && fake_terminated == 1 && logged("capture Terminate rc=0\n"));
    // Slot 1 first, slot 0 next to it.
    assert(ps_capture_slot_open(1, &second) == 0 && ps_capture_slot_open(0, &first) == 0 && fake_initialized == 2);
    assert(ps_capture_close(second) == 0 && ps_capture_close(first) == 0 && fake_terminated == 2);

    // A refused smaller mode falls back to 1080p on slot 1 alone; slot 0 keeps its settings.
    set_slot(0, 0, 0, 3, 1, 10000000);
    set_slot(1, 0, 0, 2, 1, 10000000);
    fake_decide = small_fails;
    fake_log[0] = 0;
    assert(ps_capture_slot_open_or_fall_back(1, &second) == 0);
    fake_decide = NULL;
    assert(ps_capture_slot_video_resolution(1) == 4 && record_u32(fake_record, 0x18) == 4 && record_u32(fake_record, 0x28) == 175);
    assert(ps_capture_video_resolution() == 3 && atomic_load(&resource_id[0]) == 180);
    assert(logged("capture2 OpenVideo rc=0x81950001\n") && !logged("ProsperoSunshine: capture OpenVideo"));
    assert(ps_capture_close(second) == 0);

    printf("PASS: second capture channel (%d records on profile 175, both open at once, slots apart)\n", records);
    return 0;
}
