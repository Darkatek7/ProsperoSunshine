// SPDX-License-Identifier: GPL-3.0-only
// capture/start_phase.h with the numbers measured on 13.00, 2026-11-18 (60 Hz output, video out mode 0x138): the vsync
// period fitted from stamps, the wait before Start at every phase, the opens measured there, and the stamps and grids it
// leaves alone. Then capture.c's use of it, against fake_avcap.h: reads feed it, an open waits and logs its line, and
// start_phase = 0 turns the wait off.
#define PS_FIRST_FRAME_MS 20
#include "fake_avcap.h"
#include <assert.h>
#include <time.h>

#define TPM 1596276.109      // TSC ticks per ms of the console measured
#define PERIOD_MS 16.683585  // its vsync period on the TSC, fitted over 21 opens in 16 minutes
#define MODE 0x138u          // its video out mode at 60 Hz
#define ORIGIN 1000000000000ull  // a vsync about ten minutes after boot
static const double period = PERIOD_MS * TPM;

static double absolute(double x) { return x < 0 ? -x : x; }

// The stamp of vsync `vsyncs` after ORIGIN, `lag_ms` behind it (negative: before it).
static uint64_t stamp_at(uint64_t vsyncs, double lag_ms) { return ORIGIN + (uint64_t)(int64_t)((double)vsyncs * period + lag_ms * TPM); }

// Ms from `at` to the next true vsync.
static double true_to_next_ms(uint64_t at)
{
    const double since = (double)(at - ORIGIN);
    return (period - (since - (double)(uint64_t)(since / period) * period)) / TPM;
}

static uint64_t rng = 0x9e3779b97f4a7c15ull;
// Uniform in [-most, most] (xorshift64).
static double jitter_ms(double most)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return ((double)(rng >> 11) / 9007199254740992.0 * 2 - 1) * most;
}

// `frames` stream frames of `every` vsyncs from vsync `first`, every 50th lost, each stamp off by up to 30 µs either way.
// Returns the vsync after the last frame.
static uint64_t feed(ps_phase_grid *g, uint64_t first, int frames, int every, uint32_t mode)
{
    uint64_t vsync = first;
    for (int i = 0; i < frames; i++, vsync += (uint64_t)every)
        if (i % 50 != 17) ps_phase_update(g, stamp_at(vsync, jitter_ms(0.03)), mode, TPM);
    return vsync;
}

static void fit(int every)
{
    ps_phase_grid g = {0};
    const uint64_t end = feed(&g, 0, 600 / every, every, MODE);  // 10 s
    assert(absolute(g.period / TPM - PERIOD_MS) < 0.002);
    assert(absolute(g.period / TPM - PERIOD_MS) < 0.0002);  // two stamps 10 s apart, 30 µs off each
    assert(absolute((double)g.anchor - (double)stamp_at(0, 0)) < 0.031 * TPM);
    assert(g.span == end - (uint64_t)every && g.misses == 0 && g.mode == MODE);
    assert(ps_phase_check(&g, stamp_at(end, 1), MODE, TPM) == PS_PHASE_ALIGNED);
}

// The fitted 10 s run at 60 fps, and the vsync after it.
static ps_phase_grid fitted(uint64_t *end)
{
    ps_phase_grid g = {0};
    *end = feed(&g, 0, 600, 1, MODE);
    return g;
}

// The longest Start measured (2.64 ms), and the moment before a vsync from which a return was measured good (4.15 ms).
#define LONGEST_START_MS 2.64
#define GOOD_MS 4.15

static void every_phase(void)
{
    uint64_t end;
    const ps_phase_grid g = fitted(&end);
    const uint64_t cost = (uint64_t)(PS_PHASE_START_COST_MS * TPM);
    int waits = 0;
    for (int tenth = 0; tenth <= 166; tenth++) {
        const uint64_t now = stamp_at(end + 30, tenth / 10.0);  // half a second after the last frame
        const uint64_t wait = ps_phase_wait(&g, now, MODE, TPM);
        assert((double)wait < period);
        const double to_next = true_to_next_ms(now + wait + cost);
        // Out of the late window measured (3.56 ms before a vsync to 0.01 ms after it), with room for the stamps' lag, and
        // still when Start takes as long as the longest measured.
        assert(to_next > PS_PHASE_LATE_MS - 0.05 && to_next < PERIOD_MS - PS_PHASE_AFTER_MS + 0.05);
        assert(to_next - (LONGEST_START_MS - PS_PHASE_START_COST_MS) > GOOD_MS);
        if (wait) {
            assert(absolute(to_next - PS_PHASE_TARGET_MS) < 0.05);
            waits++;
        }
    }
    // The window is 7.5 ms of the 16.68, and the line's error by now (0.1 ms) on either edge.
    assert(waits >= 74 && waits <= 80);
}

// The opens measured: Start returned to_next ms before the next vsync, and the late ones carried one frame more for their
// whole life. Each late one would have waited; each other one inside the window this leaves alone would not; and each
// one that waits returns about PS_PHASE_TARGET_MS before a vsync.
static void measured_opens(void)
{
    static const struct {
        double to_next;
        int late;
    } opens[] = {
        {6.906, 0}, {4.722, 0}, {5.805, 0}, {2.922, 1}, {12.454, 0}, {1.425, 1}, {-0.01, 1}, {5.005, 0}, {3.555, 1}, {12.82, 0},
        {2.655, 1}, {13.745, 0}, {4.152, 0}, {9.422, 0}, {9.253, 0}, {16.507, 0}, {2.906, 1}, {16.193, 0}, {11.097, 0}, {5.028, 0},
        {13.792, 0},
    };
    uint64_t end;
    const ps_phase_grid g = fitted(&end);
    int late = 0, left = 0, waits = 0;
    for (size_t i = 0; i < sizeof opens / sizeof opens[0]; i++) {
        const uint64_t returned = stamp_at(end + 60, -opens[i].to_next);
        const uint64_t wait = ps_phase_wait(&g, returned - (uint64_t)(PS_PHASE_START_COST_MS * TPM), MODE, TPM);
        if (opens[i].late) {
            assert(wait > 0);
            late++;
        } else if (opens[i].to_next > PS_PHASE_LATE_MS + 0.2 && opens[i].to_next < PERIOD_MS - PS_PHASE_AFTER_MS - 0.2) {
            assert(wait == 0);
            left++;
        }
        if (wait) {
            assert(absolute(true_to_next_ms(returned + wait) - PS_PHASE_TARGET_MS) < 0.05);
            waits++;
        }
    }
    // The good ones from 4.15 to 6.91 ms before a vsync and those up to 0.49 ms after one wait too, for a Start that takes
    // longer than measured there.
    assert(late == 6 && left == 7 && waits == 14);
}

static void no_wait_without_grid(void)
{
    uint64_t end;
    const ps_phase_grid g = fitted(&end);
    // Start, called now, would return 2 ms before a vsync: an aligned grid waits.
    const uint64_t now = stamp_at(end + 30, -2 - PS_PHASE_START_COST_MS);
    assert(ps_phase_wait(&g, now, MODE, TPM) > 0);
    const ps_phase_grid empty = {0};
    assert(ps_phase_check(&empty, now, MODE, TPM) == PS_PHASE_NONE && ps_phase_wait(&empty, now, MODE, TPM) == 0);
    assert(ps_phase_check(&g, now, 0x13c, TPM) == PS_PHASE_OTHER_MODE && ps_phase_wait(&g, now, 0x13c, TPM) == 0);
    assert(ps_phase_check(&g, now, 0x140, TPM) == PS_PHASE_NOT_60HZ && ps_phase_wait(&g, now, 0x140, TPM) == 0);
    assert(ps_phase_check(&g, now, MODE & ~0x10u, TPM) == PS_PHASE_NOT_60HZ && ps_phase_wait(&g, now, MODE & ~0x10u, TPM) == 0);
    assert(ps_phase_check(&g, now, MODE, 0) == PS_PHASE_NONE && ps_phase_wait(&g, now, MODE, 0) == 0);
    assert(ps_phase_check(&g, stamp_at(0, -1), MODE, TPM) == PS_PHASE_OLD);  // a moment before the last stamp
    // One second of stamps is too short a run.
    ps_phase_grid short_run = {0};
    const uint64_t short_end = feed(&short_run, 0, 60, 1, MODE);
    assert(ps_phase_wait(&short_run, stamp_at(short_end + 30, -2 - PS_PHASE_START_COST_MS), MODE, TPM) == 0);
    // A 10 s run reaches 20 times its length: still aligned after 150 s, old after 250 s.
    assert(ps_phase_wait(&g, stamp_at(end + 9000, -2.4), MODE, TPM) > 0);
    assert(ps_phase_check(&g, stamp_at(end + 15000, -2.4), MODE, TPM) == PS_PHASE_OLD);
    assert(ps_phase_wait(&g, stamp_at(end + 15000, -2.4), MODE, TPM) == 0);
    // A 2 min run reaches farther, but not past 30 minutes after its last stamp.
    ps_phase_grid long_run = {0};
    const uint64_t long_end = feed(&long_run, 0, 7200, 1, MODE);
    const uint64_t minute = (uint64_t)(60000 / PERIOD_MS);
    assert(ps_phase_wait(&long_run, stamp_at(long_end + 29 * minute, -2.4), MODE, TPM) > 0);
    assert(ps_phase_check(&long_run, stamp_at(long_end + 31 * minute, -2.4), MODE, TPM) == PS_PHASE_OLD);
}

static int same(const ps_phase_grid *a, const ps_phase_grid *b)
{
    return a->mode == b->mode && a->anchor == b->anchor && a->last == b->last && a->span == b->span && a->period == b->period &&
           a->misses == b->misses;
}

static void outliers(void)
{
    uint64_t end;
    ps_phase_grid g = fitted(&end);
    const ps_phase_grid before = g;
    ps_phase_update(&g, 0, MODE, TPM);                                    // no stamp
    ps_phase_update(&g, g.last - (uint64_t)period, MODE, TPM);            // an older frame
    ps_phase_update(&g, g.last, MODE, TPM);                               // the same again
    ps_phase_update(&g, g.last + (uint64_t)(0.2 * TPM), MODE, TPM);       // its vsync from the other slot
    ps_phase_update(&g, stamp_at(end, 1), MODE, 0);                       // no TSC rate
    assert(same(&g, &before));
    ps_phase_update(&g, stamp_at(end, 3.0), MODE, TPM);                   // 3 ms off: no whole number of vsyncs
    ps_phase_update(&g, stamp_at(end + 1, -0.8), MODE, TPM);              // 0.8 ms early
    ps_phase_update(&g, stamp_at(end + 2, 8.0), MODE, TPM);               // half a vsync off
    assert(g.misses == 3 && g.last == before.last && g.span == before.span && g.period == before.period);
    ps_phase_update(&g, stamp_at(end + 3, 0.01), MODE, TPM);              // on the grid again
    assert(g.misses == 0 && g.span == end + 3 && absolute(g.period / TPM - PERIOD_MS) < 0.0002);

    // The output's timing moved by 5 ms: PS_PHASE_MISSES stamps in a row off the grid, and a run starts at the last.
    g = before;
    uint64_t vsync = end;
    for (int i = 1; i < PS_PHASE_MISSES; i++) ps_phase_update(&g, stamp_at(vsync++, 5), MODE, TPM);
    assert(g.misses == PS_PHASE_MISSES - 1 && g.anchor == before.anchor);
    ps_phase_update(&g, stamp_at(vsync, 5), MODE, TPM);
    assert(g.anchor == stamp_at(vsync, 5) && g.span == 0 && g.period == before.period && g.misses == 0);
    assert(ps_phase_check(&g, stamp_at(vsync + 1, 5), MODE, TPM) == PS_PHASE_NONE);
    for (int i = 1; i <= PS_PHASE_MIN_SPAN; i++) ps_phase_update(&g, stamp_at(vsync + (uint64_t)i, 5 + jitter_ms(0.03)), MODE, TPM);
    const uint64_t now = stamp_at(vsync + PS_PHASE_MIN_SPAN + 30, 5 + 7);
    assert(ps_phase_check(&g, now, MODE, TPM) == PS_PHASE_ALIGNED);
    assert(absolute(ps_phase_to_next(&g, now) / TPM - (PERIOD_MS - 7)) < 0.1);  // 7 ms after the moved vsync

    // The TSC started anew, a minute ago: stamps far below the last. Fewer than PS_PHASE_MISSES of them change nothing but
    // the count, the PS_PHASE_MISSES-th starts a run there, keeping the period, and that run times opens again.
    g = before;
    const uint64_t reborn = (uint64_t)(60000 * TPM);
    for (int i = 0; i < PS_PHASE_MISSES - 1; i++) ps_phase_update(&g, reborn + (uint64_t)((double)i * period), MODE, TPM);
    assert(g.anchor == before.anchor && g.last == before.last && g.misses == PS_PHASE_MISSES - 1);
    assert(ps_phase_check(&g, reborn + (uint64_t)(10 * period), MODE, TPM) == PS_PHASE_OLD);
    const uint64_t anew = reborn + (uint64_t)((double)(PS_PHASE_MISSES - 1) * period);
    ps_phase_update(&g, anew, MODE, TPM);
    assert(g.anchor == anew && g.span == 0 && g.period == before.period && g.misses == 0);
    for (int i = 1; i <= PS_PHASE_MIN_SPAN; i++)
        ps_phase_update(&g, anew + (uint64_t)((double)i * period + jitter_ms(0.03) * TPM), MODE, TPM);
    assert(g.span == PS_PHASE_MIN_SPAN);
    assert(ps_phase_check(&g, anew + (uint64_t)((double)(PS_PHASE_MIN_SPAN + 30) * period), MODE, TPM) == PS_PHASE_ALIGNED);

    // A pause between streams: 3 minutes on are still counted, 10 minutes on a run starts anew, keeping the period.
    g = before;
    ps_phase_update(&g, stamp_at(end + 3 * 3600, 0.01), MODE, TPM);
    assert(g.span == end + 3 * 3600 && g.anchor == before.anchor);
    g = before;
    ps_phase_update(&g, stamp_at(end + 10 * 3600, 0.01), MODE, TPM);
    assert(g.span == 0 && g.anchor == stamp_at(end + 10 * 3600, 0.01) && g.period == before.period);
    // A run's first gap counts at most PS_PHASE_FIRST_GAP vsyncs.
    ps_phase_update(&g, stamp_at(end + 10 * 3600 + PS_PHASE_FIRST_GAP + 1, 0), MODE, TPM);
    assert(g.span == 0 && g.anchor == stamp_at(end + 10 * 3600 + PS_PHASE_FIRST_GAP + 1, 0));
    ps_phase_update(&g, stamp_at(end + 10 * 3600 + PS_PHASE_FIRST_GAP + 3, 0), MODE, TPM);
    assert(g.span == 2);

    // A first gap of a lost frame, counted with the nominal period: two vsyncs.
    ps_phase_grid fresh = {0};
    ps_phase_update(&fresh, stamp_at(0, 0), MODE, TPM);
    ps_phase_update(&fresh, stamp_at(2, 0.02), MODE, TPM);
    assert(fresh.span == 2 && absolute(fresh.period / TPM - PERIOD_MS) < 0.011);
}

static void mode_change(void)
{
    uint64_t end;
    ps_phase_grid g = fitted(&end);
    // Another output mode at 60 Hz: the grid starts there, without the old period.
    ps_phase_update(&g, stamp_at(end, 0), 0x13c, TPM);
    assert(g.mode == 0x13c && g.anchor == stamp_at(end, 0) && g.span == 0 && g.period == 0);
    assert(ps_phase_check(&g, stamp_at(end + 1, 0), MODE, TPM) == PS_PHASE_OTHER_MODE);
    // 120 Hz: emptied and not fitted.
    ps_phase_update(&g, stamp_at(end + 1, 0), 0x140, TPM);
    ps_phase_update(&g, stamp_at(end + 2, 0), 0x140, TPM);
    assert(g.mode == 0x140 && g.anchor == 0 && g.span == 0 && g.period == 0);
    assert(ps_phase_check(&g, stamp_at(end + 3, 0), 0x140, TPM) == PS_PHASE_NOT_60HZ);
    assert(ps_phase_check(&g, stamp_at(end + 3, 0), MODE, TPM) == PS_PHASE_OTHER_MODE);
    // Neither 60 nor 120 Hz (50 Hz, say): its stamps 20 ms apart would fit a 16.67 ms grid on every 5th frame, so nothing is
    // fitted there either.
    ps_phase_grid fifty = {0};
    for (int i = 0; i < 600; i++) ps_phase_update(&fifty, ORIGIN + (uint64_t)((double)i * 20 * TPM), 0x128, TPM);
    assert(fifty.mode == 0x128 && fifty.anchor == 0 && fifty.period == 0);
    assert(ps_phase_check(&fifty, ORIGIN + (uint64_t)(12010 * TPM), 0x128, TPM) == PS_PHASE_NOT_60HZ);
    // Back at 60 Hz: a new run.
    const uint64_t back = feed(&g, end + 3, 200, 1, MODE);
    assert(g.mode == MODE && ps_phase_check(&g, stamp_at(back, 1), MODE, TPM) == PS_PHASE_ALIGNED);
    assert(!strcmp(ps_phase_state_name(PS_PHASE_NOT_60HZ), "not 60 Hz") && !strcmp(ps_phase_state_name(PS_PHASE_OTHER_MODE), "other mode"));
}

// capture.c against fake_avcap.h, on this machine's TSC.
static double machine_period;  // ticks
static uint64_t first_stamp, stamps;
static uint64_t synthetic_stamp(void) { return first_stamp + (uint64_t)((double)stamps++ * machine_period); }

static double calibrate(void)
{
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    const uint64_t t0 = __rdtsc();
    usleep(30000);
    const uint64_t t1 = __rdtsc();
    clock_gettime(CLOCK_MONOTONIC, &b);
    return (double)(t1 - t0) / ((double)(b.tv_sec - a.tv_sec) * 1e3 + (double)(b.tv_nsec - a.tv_nsec) / 1e6);
}

// The open's "start phase" line: what it says, or 0 when there is none.
static int phase_line(double *to_next, double *waited, char grid[16])
{
    const char *line = strstr(fake_log, "capture start phase ");
    if (!line) return 0;
    *to_next = -1;
    if (sscanf(line, "capture start phase to_next_vsync=%lf ms waited=%lf ms grid=%15[^\n]", to_next, waited, grid) == 3) return 1;
    return sscanf(line, "capture start phase to_next_vsync=unknown waited=%lf ms grid=%15[^\n]", waited, grid) == 2;
}

static ps_capture *open_slot0(void)
{
    fake_log[0] = 0;
    ps_capture *session = NULL;
    assert(ps_capture_open(&session) == 0);
    return session;
}

static void read_frames(ps_capture *s, int frames)
{
    uint8_t buffer[64], descriptor[0x88];
    for (int i = 0; i < frames; i++)
        assert((s->mode[2] == 1 ? ps_capture_read2(s, buffer, sizeof buffer, descriptor) : ps_capture_read(s, buffer, sizeof buffer, descriptor)) == 16);
}

// A grid whose next vsync comes `ahead_ms` from now, from 200 frames read on s.
static void read_grid(ps_capture *s, double ahead_ms, double ticks_per_ms)
{
    pthread_mutex_lock(&phase_lock);
    memset(&phase_grid, 0, sizeof phase_grid);
    pthread_mutex_unlock(&phase_lock);
    first_stamp = __rdtsc() + (uint64_t)(ahead_ms * ticks_per_ms) - (uint64_t)(200 * machine_period);
    stamps = 0;
    fake_stamp = synthetic_stamp;
    read_frames(s, 200);
    fake_stamp = NULL;
}

static void in_capture(void)
{
    const int switches[PS_EXP_SWITCH_COUNT] = {0};
    ps_capture_set_experimental(switches, NULL);
    fake_output = 0x13c;
    ps_capture_set_video_mode(4, 1);
    ps_capture_set_opening_bitrate(20000000, 22400000);
    double to_next, waited;
    char grid[16];

    // Without the host's TSC rate, as in the probes: stamps fit nothing and no open waits.
    ps_capture *s = open_slot0();
    assert(phase_line(&to_next, &waited, grid) && to_next < 0 && waited == 0 && !strcmp(grid, "none"));
    machine_period = PERIOD_MS * 1000.0;  // any rate: nothing is fitted without one
    read_grid(s, 2, 0);
    assert(phase_grid.anchor == 0 && phase_grid.mode == 0);
    assert(ps_capture_close(s) == 0);

    const double ticks_per_ms = calibrate();
    machine_period = PERIOD_MS * ticks_per_ms;
    ps_capture_set_ticks_per_ms(ticks_per_ms);
    for (uint32_t codec = 0; codec < 2; codec++) {
        // H.264 reads ReadVideo's stamp at 0x48, HEVC ReadVideo2's at 0x58.
        ps_capture_set_codec(codec);
        s = open_slot0();
        read_grid(s, 2, ticks_per_ms);
        assert(phase_grid.mode == 0x13c && phase_grid.span == 199 && absolute(phase_grid.period - machine_period) < 2);
        assert(ps_capture_close(s) == 0);
    }
    ps_capture_set_codec(0);

    // Start, called now, would return 2.5 ms before a vsync: the open waits, and Start returns about 10 ms before one. The
    // open itself takes time here, so it is tried a few times.
    int timed = 0;
    for (int attempt = 0; attempt < 20 && !timed; attempt++) {
        s = open_slot0();
        read_grid(s, 2.5 + PS_PHASE_START_COST_MS, ticks_per_ms);
        assert(ps_capture_close(s) == 0);
        s = open_slot0();
        assert(phase_line(&to_next, &waited, grid) && !strcmp(grid, "aligned"));
        // waited is wall time, sleep and all: on a busy machine it may run long, so only to_next is held to the period.
        assert(waited < 100 && to_next > 0 && to_next <= PERIOD_MS + 0.01);
        timed = waited > 0;
        assert(ps_capture_close(s) == 0);
    }
    assert(timed);

    // start_phase = 0: no wait, and the line says so.
    ps_capture_set_start_phase(0);
    s = open_slot0();
    read_grid(s, 2.5 + PS_PHASE_START_COST_MS, ticks_per_ms);
    assert(ps_capture_close(s) == 0);
    s = open_slot0();
    assert(phase_line(&to_next, &waited, grid) && waited == 0 && !strcmp(grid, "off"));
    assert(ps_capture_close(s) == 0);
    ps_capture_set_start_phase(1);

    // At 120 Hz output nothing is fitted or timed.
    fake_output = 0x140;
    s = open_slot0();
    assert(phase_line(&to_next, &waited, grid) && to_next < 0 && waited == 0 && !strcmp(grid, "not 60 Hz"));
    read_grid(s, 2.5, ticks_per_ms);
    assert(phase_grid.mode == 0x140 && phase_grid.anchor == 0);
    assert(ps_capture_close(s) == 0);
    s = open_slot0();
    assert(phase_line(&to_next, &waited, grid) && waited == 0 && !strcmp(grid, "not 60 Hz"));
    assert(ps_capture_close(s) == 0);
    fake_output = 0x13c;

    // The self-check's open only stops before Start: no line.
    ps_capture_set_open_only(1);
    fake_log[0] = 0;
    ps_capture *none = NULL;
    assert(ps_capture_open(&none) == PS_CAPTURE_ERROR && !none && !strstr(fake_log, "start phase"));
    ps_capture_set_open_only(0);
}

int main(void)
{
    fit(1);
    fit(2);
    every_phase();
    measured_opens();
    no_wait_without_grid();
    outliers();
    mode_change();
    in_capture();
    puts("start_phase_test: ok");
    return 0;
}
