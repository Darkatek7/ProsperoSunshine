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
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>

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
int sceKernelOpenEventFlag(int *, const char *);
int sceKernelPollEventFlag(int, uint64_t, uint32_t, uint64_t *);

// sceKernelGetAppInfo's record, 0x60 bytes: the app id first and the title id at offset 16, as ps5upload's app_info.h
// measured. The title id field isn't always terminated.
typedef struct {
    uint32_t app_id;
    uint64_t unknown1;
    char title_id[14];
    char unknown2[0x3c];
} app_info;
_Static_assert(offsetof(app_info, title_id) == 16, "the title id is at offset 16");
int sceKernelGetAppInfo(pid_t pid, app_info *info);

int ps_title_running(char title_id[16])
{
    memset(title_id, 0, 16);
    const int app = sceLncUtilGetAppIdOfRunningBigApp();
    return app > 0 && sceLncUtilGetAppTitleId((uint32_t)app, title_id) == 0;
}

int ps_title_focused(void)
{
    // Opened once and kept: the flag belongs to the system and outlives the host.
    static int flag = -1, failed = 0;
    if (flag < 0) {
        if (failed) return -1;
        if (sceKernelOpenEventFlag(&flag, "SceShellCoreUtilAppFocus") != 0 || flag < 0) {
            failed = 1;
            flag = -1;
            return -1;
        }
    }
    const int app = sceLncUtilGetAppIdOfRunningBigApp();
    if (app <= 0) return 0;
    // Any bit, as a poll: the pattern comes back whether or not one matched.
    uint64_t focused = 0;
    sceKernelPollEventFlag(flag, ~(uint64_t)0, 0x02 /* SCE_KERNEL_EVF_WAITMODE_OR */, &focused);
    if (!focused) return -1;
    return (uint32_t)focused == (uint32_t)app;
}

int ps_title_open(const char *title_id)
{
    const size_t len = strlen(title_id);
    if (!len || len >= sizeof(((app_info *)0)->title_id)) return 0;
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t size = 0;
    if (sysctl(mib, 4, NULL, &size, NULL, 0) != 0 || !size) return -1;
    size += size / 4;  // the list may grow before the second call
    char *list = malloc(size);
    if (!list) return -1;
    if (sysctl(mib, 4, list, &size, NULL, 0) != 0) {
        free(list);
        return -1;
    }
    int open = 0;
    for (size_t at = 0; !open && at + sizeof(struct kinfo_proc) <= size;) {
        const struct kinfo_proc *proc = (const struct kinfo_proc *)(list + at);
        if (proc->ki_structsize <= 0) break;
        app_info info;
        memset(&info, 0, sizeof info);
        // Non-zero for a process that isn't an app: its record means nothing then.
        if (sceKernelGetAppInfo(proc->ki_pid, &info) == 0)
            open = memcmp(info.title_id, title_id, len) == 0 && !info.title_id[len];
        at += (size_t)proc->ki_structsize;
    }
    free(list);
    return open;
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

int ps_title_launch(const char *title_id, uint32_t user)
{
    // The host's one user service (user_service.c): never terminated here, another thread may be using it.
    int rc = ps_user_service_ready();
    if (rc) return rc;
    launch_context context = {.size = sizeof context, .user_id = user};
    if (!user) rc = sceUserServiceGetForegroundUser(&context.user_id);
    char *args[] = {NULL};
    if (rc == 0) rc = sceSystemServiceLaunchApp(title_id, args, &context);
    return rc;
}
