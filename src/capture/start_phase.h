// SPDX-License-Identifier: GPL-3.0-only
// When to call sceAvcap2Start. Measured on 13.00, 2026-11-18, with the console's output at 60 Hz (video out mode 0x138):
// every open keeps one latency for its whole life, for the game in front about 23.6 ms or one output frame (16.68 ms)
// more, at 60 and 30 fps, H.264 and HEVC alike. Which one follows only from when Start returns against the output's vsync:
// less than about 3.6 to 4.1 ms before the next one, or right on it, and the open carries the extra frame (6 of 17 game
// opens there). The frames' capture stamps (TSC ticks) sit on the vsyncs: a grid of 16.683585 ms that held within 0.04 ms
// over 16 minutes, across opens, codecs, the demo and the home screen. The first frame after Start is the one of the vsync
// before it. So the stamps of the frames read give the grid, and an open that could come near that window waits until
// Start returns about 10 ms before a vsync. At 120 Hz output (mode bit 0x40) the steps were 8.34 ms and the rule is unknown:
// nothing is fitted or timed there, nor at any output without the 60 Hz bit 0x10 (docs/re/hdr.md). Stamps at 50 Hz would
// still fit a 16.67 ms grid (every 5th frame is 6 of its vsyncs), with vsyncs that aren't there.
// Pure functions on TSC ticks, conversions by the caller's ticks per ms; capture.c feeds and uses them, tested in
// tests/start_phase_test.c.
#ifndef PROSPERO_START_PHASE_H
#define PROSPERO_START_PHASE_H
#include <stdint.h>

#define PS_PHASE_NOMINAL_MS 16.6833    // one vsync at 59.94 Hz; only counts the vsyncs of a run's first gaps
#define PS_PHASE_FIRST_GAP 8           // vsyncs a run's first gap may span, counted with that or the last run's period
#define PS_PHASE_TOLERANCE_MS 0.5      // a stamp further off the grid is not taken
// How far a stamp may lie behind its vsync, for how far the line through two of them reaches: 99 % of the measured ones
// within 0.04 ms, the worst 0.29 ms, none more than 0.015 ms early.
#define PS_PHASE_JITTER_MS 0.1
#define PS_PHASE_MISSES 10             // stamps off the grid in a row, and the grid starts anew at the last one
#define PS_PHASE_MIN_SPAN 120          // vsyncs (2 s) a run spans before it times an open
#define PS_PHASE_MAX_AGE_MS (30.0 * 60 * 1000)  // since the last stamp; the grid was measured stable over 16 minutes
#define PS_PHASE_MAX_ERROR_MS 2.0      // how far off the grid may have drifted by now and still time an open
#define PS_PHASE_START_COST_MS 0.4     // what Start itself takes (measured 0.4 ms, at most 2.6)
#define PS_PHASE_TARGET_MS 10.0        // after a wait, Start returns this long before the next vsync
// No wait while Start would return at least PS_PHASE_LATE_MS before the next vsync and PS_PHASE_AFTER_MS after the last,
// each widened by how far the grid may be off by now. Measured, 3.56 ms before it and 0.01 ms after it were late, 4.15 ms
// before it and 0.17 ms after it were not. Start took 0.4 ms in most opens but 1.6 to 2.6 ms in 3 of 21, which moves its
// return that much closer to the next vsync: 7 ms keeps such an open out of the late window too.
#define PS_PHASE_LATE_MS 7.0
#define PS_PHASE_AFTER_MS 0.5

// The output's vsync grid: the line through the first stamp of a run and the latest, which fits the period from the stamps.
typedef struct {
    uint32_t mode;    // the output mode (sceAvcap2GetVideoOutMode at the open) the stamps belong to; 0: none yet
    uint64_t anchor;  // the run's first stamp; 0: no run
    uint64_t last;    // its latest stamp
    uint64_t span;    // vsyncs from anchor to last
    double period;    // ticks per vsync: fitted once span > 0, kept when a run starts anew in the same mode; 0: none
    uint32_t misses;  // stamps off the grid in a row
} ps_phase_grid;

enum ps_phase_state { PS_PHASE_NONE, PS_PHASE_ALIGNED, PS_PHASE_OLD, PS_PHASE_OTHER_MODE, PS_PHASE_NOT_60HZ };

static inline const char *ps_phase_state_name(enum ps_phase_state state)
{
    static const char *const names[] = {"none", "aligned", "old", "other mode", "not 60 Hz"};
    return names[state];
}

// The output runs at 60 Hz (59.94 measured): bit 0x10 set, and not 0x40 (120 Hz). 0x138 and 0x13c are, 0x140 and 0x144 not.
static inline int ps_phase_60hz(uint32_t mode) { return (mode & 0x50) == 0x10; }

// How far off the line may be `vsyncs` from the anchor, in ticks: the two stamps' lag apart, stretched beyond them.
static inline double ps_phase_reach(const ps_phase_grid *g, double vsyncs, double ticks_per_ms)
{
    return PS_PHASE_JITTER_MS * ticks_per_ms * (vsyncs - (double)g->span) / (double)g->span;
}

static inline void ps_phase_restart(ps_phase_grid *g, uint64_t stamp)
{
    g->anchor = g->last = stamp;
    g->span = 0;
    g->misses = 0;
}

// A frame's capture stamp, from a session opened with output `mode`. Stamp 0 (none), an older stamp and the same vsync
// again (the other slot's frame) change nothing; a stamp off the grid is left out, and PS_PHASE_MISSES of them in a row
// start the grid anew there. So do stamps more than a second behind the last: a TSC that started anew (whether rest mode
// does that, which the host lives through, is not measured) would otherwise leave the grid behind for good. Another mode
// empties the grid; one not at 60 Hz is not fitted.
static inline void ps_phase_update(ps_phase_grid *g, uint64_t stamp, uint32_t mode, double ticks_per_ms)
{
    if (!stamp || ticks_per_ms <= 0) return;
    if (mode != g->mode) {
        g->mode = mode;
        g->period = 0;
        g->anchor = g->last = 0;
        g->span = 0;
        g->misses = 0;
    }
    if (!ps_phase_60hz(mode)) return;
    if (!g->anchor) {
        ps_phase_restart(g, stamp);
        return;
    }
    if (stamp <= g->last) {
        if ((double)(g->last - stamp) > 1000 * ticks_per_ms && ++g->misses >= PS_PHASE_MISSES) ps_phase_restart(g, stamp);
        return;
    }
    const double period = g->period > 0 ? g->period : PS_PHASE_NOMINAL_MS * ticks_per_ms;
    const double since = (double)(stamp - g->anchor);
    const uint64_t vsyncs = (uint64_t)(since / period + 0.5);
    if (vsyncs <= g->span) return;
    // A gap the line can't count the vsyncs of, after a pause between streams: a run starts at this stamp.
    const double reach = g->span ? ps_phase_reach(g, (double)vsyncs, ticks_per_ms) : 0;
    if ((double)(stamp - g->last) > PS_PHASE_MAX_AGE_MS * ticks_per_ms || (g->span ? reach > period / 4 : vsyncs > PS_PHASE_FIRST_GAP)) {
        ps_phase_restart(g, stamp);
        return;
    }
    const double off = since - (double)vsyncs * period;
    if ((off < 0 ? -off : off) > PS_PHASE_TOLERANCE_MS * ticks_per_ms + reach) {
        if (++g->misses >= PS_PHASE_MISSES) ps_phase_restart(g, stamp);
        return;
    }
    g->misses = 0;
    g->period = since / (double)vsyncs;
    g->span = vsyncs;
    g->last = stamp;
}

// Whether the grid can time an open with output `mode` at `now`: aligned; none (no run of PS_PHASE_MIN_SPAN yet); old (the
// last stamp is older than PS_PHASE_MAX_AGE_MS, or the line may be off by more than PS_PHASE_MAX_ERROR_MS by now); other
// mode; or not 60 Hz.
static inline enum ps_phase_state ps_phase_check(const ps_phase_grid *g, uint64_t now, uint32_t mode, double ticks_per_ms)
{
    if (!ps_phase_60hz(mode)) return PS_PHASE_NOT_60HZ;
    if (g->mode && g->mode != mode) return PS_PHASE_OTHER_MODE;
    if (ticks_per_ms <= 0 || !g->anchor || g->span < PS_PHASE_MIN_SPAN) return PS_PHASE_NONE;
    if (now < g->last || (double)(now - g->last) > PS_PHASE_MAX_AGE_MS * ticks_per_ms ||
        ps_phase_reach(g, (double)(now - g->anchor) / g->period, ticks_per_ms) > PS_PHASE_MAX_ERROR_MS * ticks_per_ms)
        return PS_PHASE_OLD;
    return PS_PHASE_ALIGNED;
}

// Ticks from `at` to the grid's next vsync, in (0, period]: a moment right on a vsync is a full period before the next.
// The grid needs a period (a run with span > 0).
static inline double ps_phase_to_next(const ps_phase_grid *g, uint64_t at)
{
    const double since = (double)(int64_t)(at - g->anchor);
    double vsyncs = (double)(int64_t)(since / g->period);
    if (vsyncs * g->period > since) vsyncs -= 1;  // floor, for a moment before the anchor
    double phase = since - vsyncs * g->period;
    if (phase < 0) phase = 0;
    return phase >= g->period ? g->period : g->period - phase;
}

// Ticks to wait from `now` before calling Start: 0 unless the grid is aligned for `mode` and Start, called now, could return
// inside the late window (less than PS_PHASE_LATE_MS before the next vsync or less than PS_PHASE_AFTER_MS after the last,
// give or take how far the line may be off); then until it returns PS_PHASE_TARGET_MS before one. Less than a period.
static inline uint64_t ps_phase_wait(const ps_phase_grid *g, uint64_t now, uint32_t mode, double ticks_per_ms)
{
    if (ps_phase_check(g, now, mode, ticks_per_ms) != PS_PHASE_ALIGNED) return 0;
    // The line's error now: the stamps' lag, and more the farther it reaches past its run (at most PS_PHASE_MAX_ERROR_MS).
    const double error =
        PS_PHASE_JITTER_MS * ticks_per_ms + ps_phase_reach(g, (double)(now - g->anchor) / g->period, ticks_per_ms);
    const double to_next = ps_phase_to_next(g, now + (uint64_t)(PS_PHASE_START_COST_MS * ticks_per_ms));
    if (to_next >= PS_PHASE_LATE_MS * ticks_per_ms + error && to_next <= g->period - PS_PHASE_AFTER_MS * ticks_per_ms - error)
        return 0;
    double wait = to_next - PS_PHASE_TARGET_MS * ticks_per_ms;
    if (wait < 0) wait += g->period;
    return (uint64_t)wait;
}

#endif
