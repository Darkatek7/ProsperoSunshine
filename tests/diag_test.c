// SPDX-License-Identifier: GPL-3.0-only
// host_diag.log (src/platform/ps5/diag.c): the header with its start time, a time stamp on each line, the last run kept
// as .1, a full file going to .1 instead of starting over, the filter and the tap, and the "->" trace of the debug build
// (scripts/test.sh builds this twice, with and without PROSPERO_DEBUG).
#include "platform/ps5/diag.c"
#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>

static char klog[1 << 14];
int klog_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const size_t used = strlen(klog);
    vsnprintf(klog + used, sizeof klog - used, fmt, args);
    va_end(args);
    return 0;
}

static char *slurp(const char *path)
{
    static char text[2 << 20];
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    const size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    return text;
}

static size_t upper(char *line, size_t length, size_t capacity)
{
    assert(length < capacity);
    for (size_t i = 0; i < length; i++)
        if (line[i] == 'x') line[i] = 'X';
    return length;
}

static char tapped[4096];
static void tap(const char *line, size_t length) { strncat(tapped, line, length < sizeof tapped - strlen(tapped) - 1 ? length : 0); }

int main(void)
{
    char dir[] = "/tmp/diag_test_XXXXXX";
    assert(mkdtemp(dir));
    char path[256], older[260];
    snprintf(path, sizeof path, "%s/host_diag.log", dir);
    snprintf(older, sizeof older, "%s.1", path);

    // Before ps_diag_open (the probes): the kernel log only, no time stamp.
    ps_diag_log("ProsperoSunshine: early\n");
    assert(strcmp(klog, "ProsperoSunshine: early\n") == 0);

    ps_diag_open(path, "ProsperoSunshine: host version=1.2.3 commit=abc firmware=0x10200006 known=1");
    ps_diag_log("ProsperoSunshine: capture open start codec=0\n");
    char *text = slurp(path);
    const char *header = "ProsperoSunshine: host version=1.2.3 commit=abc firmware=0x10200006 known=1 started=";
    assert(strncmp(text, header, strlen(header)) == 0);
    char *second = strchr(text, '\n') + 1;
    assert(second[0] == '[' && strstr(second, "] ProsperoSunshine: capture open start codec=0\n") == second + 8);
    // The kernel log gets the line without the time stamp.
    assert(strstr(klog, "\nProsperoSunshine: capture open start codec=0\n"));

    // Off: no time stamp.
    ps_diag_set_timestamps(0);
    ps_diag_log("ProsperoSunshine: plain\n");
    assert(strstr(slurp(path), "\nProsperoSunshine: plain\n"));
    ps_diag_set_timestamps(1);

    // The filter rewrites the line for the file and the kernel log; the tap gets what the file got.
    ps_diag_set_filter(upper);
    ps_diag_set_tap(tap);
    ps_diag_log("ProsperoSunshine: xx\n");
    assert(strstr(slurp(path), "] ProsperoSunshine: XX\n"));
    assert(tapped[0] == '[' && strstr(tapped, "] ProsperoSunshine: XX\n"));
    ps_diag_set_filter(NULL);
    ps_diag_set_tap(NULL);

    // The trace: a "->" line in the debug build, nothing in the normal build.
    ps_diag_trace("capture OpenVideo attempt=%d\n", 1);
    text = slurp(path);
#ifdef PROSPERO_DEBUG
    assert(strstr(text, "] ProsperoSunshine: -> capture OpenVideo attempt=1\n"));
#else
    assert(!strstr(text, "OpenVideo"));
#endif

    // A new run keeps the last one as .1.
    ps_diag_open(path, "ProsperoSunshine: host version=1.2.4 commit=def firmware=0x10200006 known=1");
    assert(strstr(slurp(older), "version=1.2.3") && strstr(slurp(older), "] ProsperoSunshine: XX\n"));
    assert(strstr(slurp(path), "version=1.2.4") && !strstr(slurp(path), "XX"));

    // A full file goes to .1 and the new one starts with the header; the size stays below the cap.
    char line[200];
    memset(line, 'a', sizeof line - 2);
    line[sizeof line - 2] = '\n';
    line[sizeof line - 1] = 0;
    for (int i = 0; i < PS_DIAG_MAX_BYTES / 150; i++) ps_diag_log("%s", line);
    ps_diag_log("ProsperoSunshine: after the turn\n");
    struct stat st;
    assert(stat(path, &st) == 0 && st.st_size < PS_DIAG_MAX_BYTES);
    assert(stat(older, &st) == 0 && st.st_size <= PS_DIAG_MAX_BYTES && st.st_size > PS_DIAG_MAX_BYTES - 300);
    text = slurp(path);
    assert(strncmp(text, "ProsperoSunshine: host version=1.2.4", 36) == 0);
    assert(strstr(text, " continued, older lines in host_diag.log.1\n"));
    assert(strstr(text, "] ProsperoSunshine: after the turn\n"));
    assert(strstr(slurp(older), "version=1.2.4"));

    remove(path);
    remove(older);
    rmdir(dir);
#ifdef PROSPERO_DEBUG
    puts("PASS: host_diag.log, debug build (time stamps, rotation, last run kept, filter and tap, the -> trace)");
#else
    puts("PASS: host_diag.log (time stamps, rotation, last run kept, filter and tap, no trace)");
#endif
    return 0;
}
