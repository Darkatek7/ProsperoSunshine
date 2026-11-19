// SPDX-License-Identifier: GPL-3.0-only
// The C half of shared_capture_test.cpp: capture.c with tests/fake_avcap.h, whose fakes are static, and the few calls the
// C++ test needs to steer them and read them back.
#include "fake_avcap.h"

// While on, OpenVideo returns -1 as on a console whose capture is stuck (issue 56).
static int stuck_now;
// How many of the next GetNonVclNalUnitStream calls fail.
static int headers_failing;

static int decide(const char *step, const uint8_t *record)
{
    (void)record;
    if (!strcmp(step, "OpenVideo") && stuck_now) return (int)0xffffffff;
    if (!strcmp(step, "headers") && headers_failing > 0) {
        headers_failing--;
        return (int)0x80c78000;
    }
    return 0;
}

void fake_test_reset(void)
{
    fake_decide = decide;
    stuck_now = 0;
    headers_failing = 0;
    fake_opens = 0;
    fake_log[0] = 0;
}
void fake_test_stuck(int on) { stuck_now = on; }
void fake_test_fail_headers(int times) { headers_failing = times; }
int fake_test_opens(void) { return fake_opens; }
int fake_test_logged(const char *text) { return strstr(fake_log, text) != NULL; }
