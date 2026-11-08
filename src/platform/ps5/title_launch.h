// SPDX-License-Identifier: GPL-3.0-only
#ifndef PROSPERO_TITLE_LAUNCH_H
#define PROSPERO_TITLE_LAUNCH_H
#ifdef __cplusplus
extern "C" {
#endif

// The title in front into title_id: 1, 0 on the home screen, negative if the system calls are missing.
int ps_title_running(char title_id[16]);
// Whether the running title has the screen: 1, 0 while another app has it (the home screen over a game left running), or
// negative if the system can't tell. The control center over a game leaves it the screen. Goes by the event flag the console keeps for the
// focused app, "SceShellCoreUtilAppFocus" (low 32 bits: its app id), which ShadowMountPlus and ps5upload read too.
int ps_title_focused(void);
// Starts a title for the user in front. The new app's ID, or a negative error code from the system.
int ps_title_launch(const char *title_id);
// Closes title_id through the system, as from the home screen, if it is the title in front: 1 once it has ended, 0 if it
// isn't in front, or a negative error code from the system. Its unsaved progress is lost.
int ps_title_close(const char *title_id);

#ifdef __cplusplus
}
#endif
#endif
