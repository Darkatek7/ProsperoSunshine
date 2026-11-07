// SPDX-License-Identifier: GPL-3.0-only
// The console's user service, initialized once for the whole host and never terminated. Every part of the host that
// asks it something (profile_seat.c, console_users.c, title_launch.c) goes through here: one part terminating it after
// its own call would end it under another part's call on another thread.
#ifndef PROSPERO_USER_SERVICE_H
#define PROSPERO_USER_SERVICE_H
#ifdef __cplusplus
extern "C" {
#endif

// 0 once the user service is up (initialized here, or already by something else in the process), else the system's
// error. Safe from any thread; a failed try is repeated by the next call.
int ps_user_service_ready(void);

#ifdef __cplusplus
}
#endif
#endif
