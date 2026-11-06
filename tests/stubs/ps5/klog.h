// SPDX-License-Identifier: GPL-3.0-only
// Stands in for the Payload SDK's header, so that diag.c compiles on the PC (tests/diag_test.c defines it).
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int klog_printf(const char *fmt, ...);
#ifdef __cplusplus
}
#endif
