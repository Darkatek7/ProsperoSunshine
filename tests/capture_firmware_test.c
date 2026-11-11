// SPDX-License-Identifier: GPL-3.0-only
// What each firmware gets, one row per version that someone measured or reported (docs/firmware-matrix.md), checked field by
// field in what configure() writes: the record's size and its block at 0xb8, the inner headers and rate fields or the QP in
// their place, the fields after the block, and the words the validator refused in the firmware probe. A change meant for one
// firmware that moves another one's record fails here with that firmware's name. tests/capture_records.golden pins the same
// records byte for byte for fewer versions; this test says what the bytes mean. Rows marked as a guess are what the code
// does today without a measurement on that version, kept so a change to them is a decision and not an accident.
// Also: the lines capture.c writes when a step fails before OpenVideo, which name what a port has to look at.
#include "fake_avcap.h"
#include <assert.h>

struct facts {
    uint32_t firmware;
    const char *name, *evidence;
    uint64_t total, block;  // the sizes at cfg+0 and cfg+0xb8
    ptrdiff_t shift;        // where 13.00's fields from 0x198 on sit
    int rate_fields;        // average, peak and bits per frame at 0xc8; otherwise the QP at qp_at
    size_t qp_at;
    size_t guards[4];       // words the validator refused 180 in (firmware probe, phase guards); 0 = none known
    int hevc, large, bitrate, small;  // verified: offered without a switch
};

static const struct facts rows[] = {
    {0x04000042u, "4.00", "guess: 9.60's record; 4.00 answers it with 0x81950009 (issue 40)", 0x2c0, 0xc0, -0x20, 0, 0xac, {0}, 0, 0, 0, 0},
    {0x08400005u, "8.40", "guess: 9.60's record; untested on 8.40 (issue 23)", 0x2c0, 0xc0, -0x20, 0, 0xac, {0}, 0, 0, 0, 0},
    {0x09600004u, "9.60", "firmware probe by a contributor (issue 30)", 0x2c0, 0xc0, -0x20, 0, 0xac, {0x80, 0x168, 0x1d8, 0x2a8}, 0, 0, 0, 0},
    {0x10200006u, "10.20", "verified", 0x2c8, 0xc8, -0x18, 0, 0xc4, {0x80, 0x188, 0x1fc, 0x2b0}, 1, 0, 0, 1},
    {0x10400007u, "10.40", "firmware probe by a contributor", 0x2c8, 0xc8, -0x18, 0, 0xc4, {0x80, 0x188, 0x1fc, 0x2b0}, 0, 0, 0, 0},
    {0x11200000u, "11.20", "report: video streams with 13.00's record (issue 37)", 0x2e0, 0xe0, 0, 1, 0, {0}, 0, 0, 0, 0},
    {0x11400000u, "11.40", "report: streams with 13.00's record (issue 24)", 0x2e0, 0xe0, 0, 1, 0, {0}, 0, 0, 0, 0},
    {0x12400000u, "12.40", "log of another project: 0x2e0", 0x2e0, 0xe0, 0, 1, 0, {0}, 0, 0, 0, 0},
    {0x12700001u, "12.70", "report: OpenVideo 0 and HEVC with 13.00's record (issue 17)", 0x2e0, 0xe0, 0, 1, 0, {0}, 0, 0, 0, 0},
    {0x13000040u, "13.00", "verified", 0x2e0, 0xe0, 0, 1, 0, {0}, 1, 1, 1, 1},
    {0x13400002u, "13.40", "report: OpenVideo 0 with 13.60's record (issue 14)", 0x2e8, 0xe8, 8, 1, 0, {0}, 0, 0, 0, 0},
    {0x13600007u, "13.60", "reports: streams with this record (issue 1)", 0x2e8, 0xe8, 8, 1, 0, {0}, 0, 0, 0, 0},
};

static uint64_t at64(const uint8_t *p, size_t at) { uint64_t v; memcpy(&v, p + at, sizeof v); return v; }
static uint32_t at32(const uint8_t *p, size_t at) { uint32_t v; memcpy(&v, p + at, sizeof v); return v; }
static int all_zero(const uint8_t *p, size_t from, size_t to)
{
    for (size_t i = from; i < to; i++)
        if (p[i]) return 0;
    return 1;
}

static int failures;
#define CHECK(row, what, condition) do { \
    if (!(condition)) { printf("FAIL: firmware %s (%s): %s\n", (row)->name, (row)->evidence, what); failures++; } \
} while (0)

static void configure_for(const struct facts *row, uint32_t codec, uint32_t ten_bit, uint32_t resolution, uint32_t average,
                          int variant, uint8_t *record)
{
    fake_firmware = row->firmware;
    ps_capture_set_codec(codec);
    ps_capture_set_ten_bit(ten_bit);
    ps_capture_set_video_mode(resolution, 1);
    ps_capture_set_opening_bitrate(average, average / 100 * 112);
    atomic_store(&session_resource[0], atomic_load(&resource_id[0]));
    memset(record, 0, PS_CAPTURE_RECORD_CAPACITY);
    configure(0, record, 0x13c, variant);
}

static void check_row(const struct facts *row)
{
    fake_firmware = row->firmware;
    CHECK(row, "record_shift", ps_capture_record_shift() == row->shift);
    CHECK(row, "no rate fields exactly where the record is short", ps_exp_short_record(row->firmware) == !row->rate_fields);
    CHECK(row, "HEVC verified", ps_exp_hevc_verified(row->firmware) == row->hevc);
    CHECK(row, "1440p and 4K verified", ps_exp_large_verified(row->firmware) == row->large);
    CHECK(row, "150 Mbit/s verified", ps_exp_bitrate_verified(row->firmware) == row->bitrate);
    CHECK(row, "below 1080p verified", ps_exp_small_verified(row->firmware) == row->small);
    CHECK(row, "ResourceArbitrator table only where verified", (ps_ra_find_known(row->firmware) != NULL) == (row->hevc && row->small));

    // Every mode the host can ask for, both codecs, both bit depths. HEVC with the records HEVC goes out with: the standard
    // one where 13.00's fields fit (13.00 and the versions with its size; 10.x and 9.60 leave them out), elsewhere only
    // the Experimental switch offers it, with the variants minimal, then full (experimental.h). The standard HEVC record
    // on 13.40 and 13.60 keeps 13.00's offsets and overwrites fields there; no stream gets it (capture_records.golden
    // pins it as it was).
    for (uint32_t codec = 0; codec < 2; codec++)
        for (int variant = PS_EXP_STANDARD; variant <= PS_EXP_FULL; variant++) {
            if (variant != PS_EXP_STANDARD && (codec == 0 || row->shift <= 0)) continue;
            if (variant == PS_EXP_STANDARD && codec == 1 && row->shift > 0) continue;
            for (uint32_t ten_bit = 0; ten_bit < 2; ten_bit++)
            for (uint32_t resolution = 1; resolution <= 6; resolution++) {
                uint8_t p[PS_CAPTURE_RECORD_CAPACITY];
                configure_for(row, codec, ten_bit, resolution, 20000000u, variant, p);
                const ptrdiff_t s = row->shift;
                CHECK(row, "size at cfg+0", at64(p, 0) == row->total);
                CHECK(row, "size of the block at 0xb8", at64(p, 0xb8) == row->block);
                CHECK(row, "nothing written past the record", all_zero(p, row->total, PS_CAPTURE_RECORD_CAPACITY));
                CHECK(row, "inner headers at 0xc0..0x120 only with rate fields",
                      (at64(p, 0xc0) == 0x20 && at64(p, 0x120) == 0x18) == (row->rate_fields != 0));
                if (row->rate_fields) {
                    CHECK(row, "average at 0xc8", at32(p, 0xc8) == 20000000u);
                    CHECK(row, "peak at 0xcc", at32(p, 0xcc) == 22400000u);
                } else {
                    // The QP follows the bitrate (encoder_qp.h): 20 Mbit/s is 21/25, HEVC 8 bit stops at 24/28 (issue 50).
                    const uint32_t idr = codec == 1 && !ten_bit ? 24 : 21;
                    CHECK(row, "QP for IDR frames", at32(p, row->qp_at) == idr);
                    CHECK(row, "QP for the other frames", at32(p, row->qp_at + 4) == idr + 4);
                    // The other short record's QP spot stays empty: on 9.60 0xc4 changed nothing (phase ratesearch).
                    const size_t other = row->qp_at == 0xac ? 0xc4 : 0xac;
                    CHECK(row, "nothing at the other record's QP spot", at32(p, other) == 0 && at32(p, other + 4) == 0);
                    CHECK(row, "H.264 slice fields only for H.264", (at32(p, 0x44) == PS_H264_SLICES) == (codec == 0));
                }
                CHECK(row, "codec at 0x24", at32(p, 0x24) == codec);
                CHECK(row, "resolution at 0x18", at32(p, 0x18) == resolution);
                CHECK(row, "resource at 0x28: 180, 182 above 1080p", at32(p, 0x28) == (resolution >= 5 ? 182u : 180u));
                CHECK(row, "headers after the block", at64(p, 0x1e8 + s) == 0xf8 && at64(p, 0x1f0 + s) == 0x28 &&
                                                          at64(p, 0x218 + s) == 0x88 && at64(p, 0x2a8 + s) == 0x10);
                CHECK(row, "video out mode", at32(p, 0x2a0 + s) == 0x13c);
                CHECK(row, "composited screen", at32(p, 0x2a4 + s) == 2);
                CHECK(row, "the 1 and the byte 1 at the end", at32(p, 0x2b0 + s) == 1 && p[0x2b4 + s] == 1);
                for (size_t g = 0; g < 4 && row->guards[g]; g++) CHECK(row, "a word the validator guards stays 0", at32(p, row->guards[g]) == 0);
                // HEVC: 13.00's Remote Play fields only where the record has rate fields; 10.x and 9.60 take cfg+0x24 alone.
                if (codec == 1)
                    CHECK(row, "HEVC fields of 13.00", (p[0x5c] == 9) == (row->rate_fields && variant != PS_EXP_MINIMAL));
            }
        }
}

// A step before OpenVideo that fails names itself and what it missed, on any firmware.
static int fails_at_open(void)
{
    ps_capture *session = NULL;
    const int rc = ps_capture_open(&session);
    ps_capture_close(session);
    return rc;
}

static void check_failure_lines(void)
{
    fake_firmware = 0x11200000u;
    ps_capture_set_codec(0);
    ps_capture_set_video_mode(4, 1);

    fake_log[0] = 0;
    fake_missing = "sceAvcap2Start sceAvcap2GetNonVclNalUnitStream";
    assert(fails_at_open() == PS_CAPTURE_ERROR);
    assert(strstr(fake_log, "capture Avcap2 exports missing on firmware 0x11200000: sceAvcap2Start sceAvcap2GetNonVclNalUnitStream\n"));
    assert(strstr(fake_log, "capture open failed step=Avcap2 functions "));
    // ReadVideo2 is only for HEVC: missing alone it stops nothing here.
    fake_missing = "sceAvcap2ReadVideo2";
    fake_log[0] = 0;
    assert(fails_at_open() == 0 && !strstr(fake_log, "missing"));

    fake_missing = "sceKernelLoadStartModule";
    fake_log[0] = 0;
    assert(fails_at_open() == PS_CAPTURE_ERROR);
    assert(strstr(fake_log, "capture no sceKernelLoadStartModule in libkernel_web, libkernel_sys or libkernel\n"));
    assert(strstr(fake_log, "capture open failed step=system modules "));
    fake_missing = "";

    fake_list_rc = (int)0x80020003;
    fake_log[0] = 0;
    assert(fails_at_open() == PS_CAPTURE_ERROR);
    assert(strstr(fake_log, "capture ResourceArbitrator list rc=0x80020003\n"));
    assert(strstr(fake_log, "capture open failed step=resource list "));
    fake_list_rc = 0;

    fake_mode_rc = (int)0x81950001;
    fake_log[0] = 0;
    assert(fails_at_open() == PS_CAPTURE_ERROR);
    assert(strstr(fake_log, "capture GetVideoOutMode rc=0x81950001\n"));
    assert(strstr(fake_log, "capture open failed step=video out mode "));
    fake_mode_rc = 0;

    fake_log[0] = 0;
    assert(fails_at_open() == 0 && strstr(fake_log, "capture open ok\n"));
}

int main(void)
{
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) check_row(&rows[i]);
    // The boundaries between the records, so a new version lands where the rule says: 10.x's below 11, 13.60's from 13.40.
    static const struct { uint32_t firmware; ptrdiff_t shift; } edges[] = {
        {0x09ffffffu, -0x20}, {0x10000000u, -0x18}, {0x10ffffffu, -0x18}, {0x11000000u, 0},
        {0x133fffffu, 0}, {0x13400000u, 8}, {0x13420000u, 8}, {0x13ffffffu, 8}, {0x14000000u, 0},
    };
    for (size_t i = 0; i < sizeof edges / sizeof edges[0]; i++) {
        fake_firmware = edges[i].firmware;
        if (ps_capture_record_shift() != edges[i].shift) {
            printf("FAIL: firmware %#x gets shift %td, expected %td\n", edges[i].firmware, ps_capture_record_shift(), edges[i].shift);
            failures++;
        }
    }
    if (failures) return 1;
    check_failure_lines();
    printf("PASS: capture records per firmware (%zu versions, the boundaries between records) and the lines of a failed open\n",
           sizeof rows / sizeof rows[0]);
    return 0;
}
