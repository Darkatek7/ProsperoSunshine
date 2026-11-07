// SPDX-License-Identifier: GPL-3.0-only
// The calls follow launch_app() in shsrv and sys_launch_title() in websrv by John Törnblom (GPL-3.0-or-later,
// https://github.com/ps5-payload-dev); the close follows websrv's use of sceSystemServiceKillApp. The two libraries have to be
// linked. Loaded with sceKernelLoadStartModule, a process stopped for good, past SIGKILL, until the console restarted: a probe
// doing that didn't get as far as its first log line, and the host stopped right after the title came up. A linked probe got
// the title's app id back and kept running.
#include "title_launch.h"
#include "user_service.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t size, user_id, options;
    uint64_t crash_report;
    uint32_t flags;
} launch_context;

int sceUserServiceGetForegroundUser(uint32_t *);
int sceSystemServiceLaunchApp(const char *, char **, launch_context *);
int sceLncUtilGetAppIdOfRunningBigApp(void);
int sceLncUtilGetAppTitleId(uint32_t, char *);
int sceSystemServiceKillApp(int app_id, int how, int reason, int core_dump);

int ps_title_running(char title_id[16])
{
    memset(title_id, 0, 16);
    const int app = sceLncUtilGetAppIdOfRunningBigApp();
    return app > 0 && sceLncUtilGetAppTitleId((uint32_t)app, title_id) == 0;
}

int ps_title_close(const char *title_id)
{
    char running[16] = {0};
    const int app = sceLncUtilGetAppIdOfRunningBigApp();
    if (app <= 0 || sceLncUtilGetAppTitleId((uint32_t)app, running) != 0 || strcmp(running, title_id) != 0) return 0;
    // The system's own close, as "Close Game" on the home screen, not a signal to the process. It returns once the title has
    // ended: about 1.2 s on 10.20.
    const int rc = sceSystemServiceKillApp(app, -1, 0, 0);
    return rc < 0 ? rc : 1;
}

int ps_title_launch(const char *title_id)
{
    // The host's one user service (user_service.c): never terminated here, another thread may be using it.
    int rc = ps_user_service_ready();
    if (rc) return rc;
    launch_context context = {.size = sizeof context};
    rc = sceUserServiceGetForegroundUser(&context.user_id);
    char *args[] = {NULL};
    if (rc == 0) rc = sceSystemServiceLaunchApp(title_id, args, &context);
    return rc;
}
