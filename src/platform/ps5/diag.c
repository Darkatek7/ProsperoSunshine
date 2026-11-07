// SPDX-License-Identifier: GPL-3.0-only
#include "diag.h"
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ps5/klog.h>

// A capture that keeps failing logs a few lines four times a second (the reopen loop in video_ps5.cpp); with 1 MiB and
// one older file the lines that show how a problem started are still there when someone opens the report.
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int fd = -1;
static size_t written;
static char first_line[320];
static char file_path[256];
static struct timespec started;
static atomic_int timestamps = 1;
static size_t (*_Atomic line_filter)(char *, size_t, size_t);
static void (*_Atomic line_tap)(const char *, size_t);

static void write_all(const char *line, size_t length)
{
    if (write(fd, line, length) == (ssize_t)length) written += length;
    fsync(fd);
}

// Caller holds lock. The current file becomes path.1 (replacing an older one) and a new one starts with the header, and
// with " continued, older lines in <name>.1" when the file was full.
static void rotate(int full)
{
    char older[sizeof file_path + 2];
    snprintf(older, sizeof older, "%s.1", file_path);
    if (fd >= 0) close(fd);
    rename(file_path, older);
    fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0666);
    written = 0;
    if (fd < 0) return;
    const char *name = strrchr(file_path, '/') ? strrchr(file_path, '/') + 1 : file_path;
    char header[sizeof first_line + sizeof file_path + 32];
    const int length = snprintf(header, sizeof header, "%.*s%s%s%s\n", (int)strcspn(first_line, "\n"), first_line,
                                full ? " continued, older lines in " : "", full ? name : "", full ? ".1" : "");
    if (length > 0) write_all(header, (size_t)length < sizeof header ? (size_t)length : sizeof header - 1);
}

static void append(const char *line, size_t length)
{
    if (fd < 0) return;
    if (written + length > PS_DIAG_MAX_BYTES) rotate(1);
    if (fd < 0) return;
    write_all(line, length);
}

static long long elapsed_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)(now.tv_sec - started.tv_sec) * 1000 + (now.tv_nsec - started.tv_nsec) / 1000000;
}

void ps_diag_open(const char *path, const char *header)
{
    pthread_mutex_lock(&lock);
    clock_gettime(CLOCK_MONOTONIC, &started);
    snprintf(file_path, sizeof file_path, "%s", path);
    // The wall-clock time the line timestamps count from, local like sunshine.log's.
    const time_t now = time(NULL);
    struct tm local;
    char since[32] = "";
    if (localtime_r(&now, &local)) strftime(since, sizeof since, " started=%Y-%m-%dT%H:%M:%S", &local);
    snprintf(first_line, sizeof first_line, "%s%s\n", header, since);
    // The last run's file is kept as path.1.
    if (access(file_path, F_OK) == 0) {
        rotate(0);
    } else {
        fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0666);
        written = 0;
        append(first_line, strlen(first_line));
    }
    pthread_mutex_unlock(&lock);
    klog_printf("%s", first_line);
}

void ps_diag_reopen(void)
{
    pthread_mutex_lock(&lock);
    if (fd >= 0) close(fd);
    fd = open(file_path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    const off_t size = fd < 0 ? 0 : lseek(fd, 0, SEEK_END);
    written = size > 0 ? (size_t)size : 0;
    pthread_mutex_unlock(&lock);
}

void ps_diag_set_timestamps(int on) { atomic_store(&timestamps, on != 0); }
void ps_diag_set_filter(size_t (*filter)(char *, size_t, size_t)) { atomic_store(&line_filter, filter); }
void ps_diag_set_tap(void (*tap)(const char *, size_t)) { atomic_store(&line_tap, tap); }

static void vlog(const char *mark, const char *format, va_list args)
{
    char line[600];
    size_t prefix = 0;
    if (atomic_load(&timestamps) && file_path[0]) {
        const int n = snprintf(line, sizeof line, "[%7lld] ", elapsed_ms());
        prefix = n > 0 ? (size_t)n : 0;
    }
    const size_t stamp = prefix;
    const int mark_length = snprintf(line + prefix, sizeof line - prefix, "%s", mark);
    if (mark_length > 0) prefix += (size_t)mark_length;
    const int n = vsnprintf(line + prefix, sizeof line - prefix, format, args);
    if (n <= 0) return;
    size_t length = prefix + ((size_t)n < sizeof line - prefix ? (size_t)n : sizeof line - prefix - 1);
    size_t (*filter)(char *, size_t, size_t) = atomic_load(&line_filter);
    if (filter) length = filter(line, length, sizeof line);
    line[length] = 0;
    // The kernel log stamps its lines itself.
    klog_printf("%s", line + (stamp <= length ? stamp : length));
    pthread_mutex_lock(&lock);
    append(line, length);
    pthread_mutex_unlock(&lock);
    void (*tap)(const char *, size_t) = atomic_load(&line_tap);
    if (tap) tap(line, length);
}

void ps_diag_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vlog("", format, args);
    va_end(args);
}

void ps_diag_trace(const char *format, ...)
{
#ifdef PROSPERO_DEBUG
    va_list args;
    va_start(args, format);
    vlog("ProsperoSunshine: -> ", format, args);
    va_end(args);
#else
    (void)format;
#endif
}
