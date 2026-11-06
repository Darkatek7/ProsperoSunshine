// SPDX-License-Identifier: GPL-3.0-only
// The host's breadcrumbs for the app's diagnostic report: each line goes to the kernel log and, once
// ps_diag_open ran, to a small file. Every line is on disk before the next step, so the file still
// tells how far the host got when the step after it brings the process down.
#ifndef PROSPERO_DIAG_H
#define PROSPERO_DIAG_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

// Starts the file at `path` with `header` as its first line. A file left from the last run becomes `path`.1 first, so the
// lines before a crash survive the next start. Without this call (the probes) lines go to the kernel log only.
void ps_diag_open(const char *path, const char *header);
// Opens the file again without emptying it; after rest mode the old descriptor takes no more lines.
void ps_diag_reopen(void);
void ps_diag_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
// Before a call that may hang the console or the host (Avcap2, the ResourceArbitrator, binding a port, stopping the
// services): in the debug build (PROSPERO_DEBUG) a line "-> what", on disk before the call, so the last such line names
// the call a hang stopped at. Nothing in the normal build.
void ps_diag_trace(const char *format, ...) __attribute__((format(printf, 1, 2)));

// Milliseconds since ps_diag_open in front of each line, "[  12345] " (on unless sunshine.conf says diag_timestamps = 0);
// the header names the wall-clock time they count from.
void ps_diag_set_timestamps(int on);
// Rewrites each line in place before it is written (the debug build masks addresses); returns the new length, below
// `capacity`.
void ps_diag_set_filter(size_t (*filter)(char *line, size_t length, size_t capacity));
// Also hands each line, as written to the file, to `tap` (the debug build's remote log).
void ps_diag_set_tap(void (*tap)(const char *line, size_t length));

// The file is kept below this size: when a line would cross it, the file becomes `path`.1 and starts anew with the header.
#define PS_DIAG_MAX_BYTES (1024 * 1024)

#ifdef __cplusplus
}
#endif
#endif
