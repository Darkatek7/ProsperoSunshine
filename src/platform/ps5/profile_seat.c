// SPDX-License-Identifier: GPL-3.0-only
#include "profile_seat.h"
#include "user_service.h"
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ps5/kernel.h>

int sceUserServiceGetLoginUserIdList(int32_t *);
int sceUserServiceGetUserName(int32_t, char *, size_t);
// No public prototypes. Called with the user id and zeros after it, as measured on 10.20.
int sceUserServiceLogin(int32_t, uint64_t, uint64_t, uint64_t);
int sceUserServiceLogout(int32_t, uint64_t, uint64_t, uint64_t);
int sceUserServiceGetForegroundUser(int32_t *);
int sceKernelLoadStartModule(const char *, size_t, const void *, uint32_t, void *, int *);
// The request the payload SDK's hello_world sample hands to the system's notifications: the text starts at byte 45.
typedef struct {
    char reserved[45];
    char message[3075];
} notify_request;
int sceKernelSendNotificationRequest(int, notify_request *, size_t, int);

// libSceMbus (/system/common/lib), by NID. Its client has to be set up before any call: without it, bind reads a null
// pointer.
#define MBUS_INIT "wRPXMGtkOq0"
#define MBUS_BIND "Cpo6sQmnZgU"
#define MBUS_INFO "RsKVKhSWt28"
typedef int (*mbus_init_fn)(uint64_t, uint64_t);
typedef int (*mbus_bind_fn)(uint64_t device, int32_t user);
typedef int (*mbus_info_fn)(uint64_t device, void *info, uint32_t size);
static pthread_mutex_t setup_lock = PTHREAD_MUTEX_INITIALIZER;
static mbus_bind_fn mbus_bind;    // set once libSceMbus is up; read under setup_lock
static mbus_info_fn mbus_info;    // set with it
static int64_t mbus_tried_ms = -1;  // the last failed try

static int64_t now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

// libSceMbus, set up until it is up: right after the host restarts in the middle of a stream, its setup can fail for a
// while (2 of 4 restarts, 2026-11-07), and binds must not stay off until the next restart. A failed try is repeated at
// most once a second. Returns the bind, or NULL while libSceMbus isn't up. The user service is user_service.c's.
static mbus_bind_fn setup(void)
{
    pthread_mutex_lock(&setup_lock);
    if (!mbus_bind && (mbus_tried_ms < 0 || now_ms() - mbus_tried_ms >= 1000)) {
        mbus_tried_ms = now_ms();
        uint32_t module = 0;
        bool loaded = kernel_dynlib_handle(-1, "libSceMbus.sprx", &module) == 0;
        if (!loaded) {
            const int rc = sceKernelLoadStartModule("/system/common/lib/libSceMbus.sprx", 0, NULL, 0, NULL, NULL);
            if (rc > 0) {
                module = (uint32_t)rc;
                loaded = true;
            }
        }
        const mbus_init_fn init = loaded ? (mbus_init_fn)kernel_dynlib_resolve(-1, module, MBUS_INIT) : NULL;
        if (init && init(0, 0) == 0) {
            mbus_info = (mbus_info_fn)kernel_dynlib_resolve(-1, module, MBUS_INFO);
            mbus_bind = (mbus_bind_fn)kernel_dynlib_resolve(-1, module, MBUS_BIND);
        }
    }
    const mbus_bind_fn bind = mbus_bind;
    pthread_mutex_unlock(&setup_lock);
    return bind;
}

// The system's "no user" in its user id lists and answers.
#define NO_USER 0xffffffffu

int ps_profile_signed_in(uint32_t user)
{
    {
        const int rc = ps_user_service_ready();
        if (rc) return rc < 0 ? rc : -1;
    }
    int32_t users[32];
    for (int i = 0; i < 32; i++) users[i] = -1;
    {
        const int rc = sceUserServiceGetLoginUserIdList(users);
        if (rc) return rc < 0 ? rc : -1;
    }
    for (int i = 0; i < 32; i++)
        if ((uint32_t)users[i] == user) return 1;
    return 0;
}

static int restore_failures;  // under as_shellcore's lock

// SceShellCore's authid and every capability, for one call only: kept for good, a borrowed authid has made the system
// restart in other payloads. One call at a time, as the swap is the whole process's. Nothing is borrowed unless the
// process's own authid and capabilities were read, so that they can be put back.
static int as_shellcore(int (*call)(int32_t, uint64_t, uint64_t, uint64_t), uint32_t user)
{
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    const pid_t pid = getpid();
    const uint64_t authid = kernel_get_ucred_authid(pid);
    uint8_t caps[16], all[16];
    if (!authid || kernel_get_ucred_caps(pid, caps) != 0) {
        pthread_mutex_unlock(&lock);
        return PS_PROFILE_NO_CREDENTIALS;
    }
    memset(all, 0xff, sizeof all);
    int rc = PS_PROFILE_NO_CREDENTIALS;
    if (kernel_set_ucred_caps(pid, all) == 0 && kernel_set_ucred_authid(pid, 0x3800000000000010ULL) == 0)
        rc = call((int32_t)user, 0, 0, 0);
    const int authid_back = kernel_set_ucred_authid(pid, authid);
    const int caps_back = kernel_set_ucred_caps(pid, caps);
    if (authid_back != 0 || caps_back != 0) restore_failures++;
    pthread_mutex_unlock(&lock);
    return rc;
}

int ps_profile_credential_restore_failures(void)
{
    return restore_failures;
}

int ps_profile_sign_in(uint32_t user)
{
    {
        const int rc = ps_user_service_ready();
        if (rc) return rc;
    }
    return as_shellcore(sceUserServiceLogin, user);
}

int ps_profile_sign_out(uint32_t user)
{
    {
        const int rc = ps_user_service_ready();
        if (rc) return rc;
    }
    return as_shellcore(sceUserServiceLogout, user);
}

int ps_profile_foreground(uint32_t *user)
{
    if (!user) return -1;
    *user = 0;
    {
        const int rc = ps_user_service_ready();
        if (rc) return rc < 0 ? rc : -1;
    }
    int32_t front = -1;
    {
        const int rc = sceUserServiceGetForegroundUser(&front);
        if (rc) return rc < 0 ? rc : -1;
    }
    // Profile ids may be "negative" as int32; only the system's "no user" means nobody is in front.
    *user = (uint32_t)front == NO_USER ? 0 : (uint32_t)front;
    return 0;
}

// The add ioctl can return before the system bus knows the pad: a bind then gets "no such device" (seen on 10.20,
// 2026-11-06, the console's question came up). The login manager only looks at a new pad about 10 ms after the bus
// announces it, so the bind tries again every millisecond for a while.
#define MBUS_NO_DEVICE ((int)0x809b0005)
#define BIND_TRIES 30

int ps_profile_bind(uint64_t device, uint32_t user)
{
    const mbus_bind_fn bind = setup();
    if (!bind) return -1;
    int rc = MBUS_NO_DEVICE;
    for (int i = 0; i < BIND_TRIES && rc == MBUS_NO_DEVICE; i++) {
        if (i) usleep(1000);
        rc = bind(device, (int32_t)user);
    }
    return rc;
}

// sceMbusGetDeviceInfo_ takes exactly 0x100 bytes and has the device's owner at +0xC8, 0 for none (10.20, 2026-11-06).
#define MBUS_INFO_SIZE 0x100
#define MBUS_INFO_OWNER 0xC8

int ps_profile_owner(uint64_t device, uint32_t *user)
{
    if (!user || !setup()) return -1;
    pthread_mutex_lock(&setup_lock);
    const mbus_info_fn info_of = mbus_info;
    pthread_mutex_unlock(&setup_lock);
    if (!info_of) return -1;
    uint8_t info[MBUS_INFO_SIZE];
    memset(info, 0, sizeof info);
    const int rc = info_of(device, info, sizeof info);
    if (rc) return rc;
    memcpy(user, info + MBUS_INFO_OWNER, sizeof *user);
    return 0;
}

void ps_profile_name(uint32_t user, char *name, size_t size)
{
    if (!name || !size) return;
    memset(name, 0, size);
    if (ps_user_service_ready() != 0 || sceUserServiceGetUserName((int32_t)user, name, size - 1) != 0) name[0] = '\0';
}

void ps_notify(const char *text)
{
    notify_request request;
    memset(&request, 0, sizeof request);
    strncpy(request.message, text ? text : "", sizeof request.message - 1);
    sceKernelSendNotificationRequest(0, &request, sizeof request, 0);
}
