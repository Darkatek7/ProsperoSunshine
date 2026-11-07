// SPDX-License-Identifier: GPL-3.0-only
// Runs a call into the haptic channel so that a fault in it ends the call, not the host: the call was measured on one firmware,
// and its form may differ on others (docs/re/haptics.md). Only one thread may use it.
#ifndef PROSPERO_HAPTIC_GUARD_H
#define PROSPERO_HAPTIC_GUARD_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PS_HAPTIC_FAULT (-1000)
typedef struct {
    int signal;
    uint64_t address, instruction;
} ps_haptic_fault;
// fn(arg)'s result, or PS_HAPTIC_FAULT with fault filled in.
int ps_haptic_guarded(int (*fn)(void *), void *arg, ps_haptic_fault *fault);
#ifdef __cplusplus
}
#endif
#endif
