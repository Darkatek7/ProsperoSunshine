// SPDX-License-Identifier: GPL-3.0-only
// capture.c on the PC: firmware, modules, ResourceArbitrator and Avcap2 are fakes that always succeed unless
// fake_decide says otherwise. Each test program includes this once; it pulls in capture.c itself, for configure().
#pragma once
#include "platform/ps5/capture.c"
#include <stdarg.h>

static uint32_t fake_firmware = 0x13000040u;
static uint8_t fake_record[0x2e8];  // what the last OpenVideo got
static int fake_opens;
// Result of a step: "OpenVideo", "Start" or "read" (0 = go on; for "read" a frame, AGAIN = none yet). Default all 0.
static int (*fake_decide)(const char *step, const uint8_t *record);
static char fake_log[1 << 16];

static int fake_rc(const char *step) { return fake_decide ? fake_decide(step, fake_record) : 0; }

uint32_t kernel_get_fw_version(void) { return fake_firmware; }

void ps_diag_log(const char *format, ...)
{
    const size_t used = strlen(fake_log);
    va_list args;
    va_start(args, format);
    vsnprintf(fake_log + used, sizeof fake_log - used, format, args);
    va_end(args);
}

// The debug build's "->" line before each risky call; nothing to check in these tests.
void ps_diag_trace(const char *format, ...) { (void)format; }

static int (*fake_callback)(int, int, const uint8_t *);
static int fake_register(int id, void *callback, void *user)
{
    (void)id;
    (void)user;
    fake_callback = (int (*)(int, int, const uint8_t *))callback;
    return 0;
}
static int fake_unregister(int id) { (void)id; return 0; }
// What the system answers to RUNNING, from fake_decide's "grant": 0 yes, >0 refused with that as data[0], or these.
#define FAKE_REVOKE (-1)
#define FAKE_SILENT (-2)
static int fake_request(int id, const char *state, void *data, int pid)
{
    (void)data;
    (void)pid;
    if (strcmp(state, "RUNNING")) return 0;
    const int answer = fake_rc("grant");
    const uint8_t value = answer > 0 ? (uint8_t)answer : 1;
    if (answer == FAKE_REVOKE) fake_callback(id, 2, NULL);
    else if (answer != FAKE_SILENT) fake_callback(id, 1, &value);
    return 0;
}
static int32_t fake_active[2];  // the others on the arbitrator's list
static int fake_list_rc;        // what list() returns; 0 = the list below
static int fake_list(int32_t *active)
{
    if (fake_list_rc) return fake_list_rc;
    memset(active, 0, 32 * sizeof *active);
    memcpy(active, fake_active, sizeof fake_active);
    return 0;
}

int ps_ra_resolve(uint32_t ra_handle, ps_ra_functions *out)
{
    (void)ra_handle;
    out->offset[PS_RA_REGISTER] = (uint32_t)(uintptr_t)fake_register;
    out->offset[PS_RA_UNREGISTER] = (uint32_t)(uintptr_t)fake_unregister;
    out->offset[PS_RA_REQUEST] = (uint32_t)(uintptr_t)fake_request;
    out->offset[PS_RA_LIST] = (uint32_t)(uintptr_t)fake_list;
    return 0;
}

static int fake_load(const char *name, size_t args, const void *argp, uint32_t flags, void *option, int *result)
{
    (void)name; (void)args; (void)argp; (void)flags; (void)option; (void)result;
    return 0;
}
// How often Avcap2 was initialized and terminated.
static int fake_initialized, fake_terminated;
static int fake_initialize(void) { ++fake_initialized; return 0; }
static int fake_terminate(void) { ++fake_terminated; return 0; }
// The output's mode; 0 = none, as while a title starts or closes.
static int fake_output = 0x13c;
static int fake_mode_rc;  // what GetVideoOutMode returns
static int fake_mode(int *mode) { *mode = fake_output; return fake_mode_rc; }
static int fake_open_video(intptr_t *handle, const void *config, uint8_t flag)
{
    (void)flag;
    *handle = 1;
    memcpy(fake_record, config, sizeof fake_record);
    ++fake_opens;
    return fake_rc("OpenVideo");
}
static int fake_start(intptr_t handle) { (void)handle; return fake_rc("Start"); }
static int fake_handle(intptr_t handle) { (void)handle; return 0; }
static int fake_headers(intptr_t handle, void *nal)
{
    (void)handle;
    const uint64_t size = 4;
    memcpy((uint8_t *)nal + 0x10, &size, sizeof size);
    return 0;
}
static int fake_read(intptr_t handle, void *destination, size_t capacity, void *descriptor, uint32_t count)
{
    (void)handle; (void)descriptor; (void)count;
    const int rc = fake_rc("read");
    if (rc) return rc;
    memset(destination, 0, 16 < capacity ? 16 : capacity);
    return 16;
}
static int fake_reserve(const void *request, void *first, void *second)
{
    (void)request;
    memset(first, 0x11, 0x20);
    memset(second, 0x22, 0x20);
    return 0;
}
static int fake_free(void *first, void *second) { (void)first; (void)second; return 0; }

int kernel_dynlib_handle(pid_t pid, const char *basename, uint32_t *handle)
{
    (void)pid; (void)basename;
    *handle = 1;
    return 0;
}
// Exports a firmware lacks: dlsym finds none of the names listed here, separated by spaces.
static const char *fake_missing = "";
intptr_t kernel_dynlib_dlsym(pid_t pid, uint32_t handle, const char *symbol)
{
    (void)pid; (void)handle;
    const size_t length = strlen(symbol);
    for (const char *at = strstr(fake_missing, symbol); at; at = strstr(at + 1, symbol))
        if ((at == fake_missing || at[-1] == ' ') && (at[length] == ' ' || !at[length])) return 0;
    const struct { const char *name; intptr_t address; } table[] = {
        {"sceKernelLoadStartModule", (intptr_t)fake_load},
        {"sceAvcap2Initialize", (intptr_t)fake_initialize},
        {"sceAvcap2Terminate", (intptr_t)fake_terminate},
        {"sceAvcap2GetVideoOutMode", (intptr_t)fake_mode},
        {"sceAvcap2OpenVideo", (intptr_t)fake_open_video},
        {"sceAvcap2Start", (intptr_t)fake_start},
        {"sceAvcap2Stop", (intptr_t)fake_handle},
        {"sceAvcap2Close", (intptr_t)fake_handle},
        {"sceAvcap2GetNonVclNalUnitStream", (intptr_t)fake_headers},
        {"sceAvcap2ReadVideo", (intptr_t)fake_read},
        {"sceAvcap2ReadVideo2", (intptr_t)fake_read},
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (!strcmp(table[i].name, symbol)) return table[i].address;
    return 0;
}
intptr_t kernel_dynlib_resolve(pid_t pid, uint32_t handle, const char *nid)
{
    (void)pid; (void)handle;
    if (!strcmp(nid, "yN0w4tWivqs")) return (intptr_t)fake_reserve;
    if (!strcmp(nid, "96UQhNnBlxc")) return (intptr_t)fake_free;
    return 0;
}
// The RA offsets are 32 bits: the upper half of the fakes' addresses is the "module base".
intptr_t kernel_dynlib_mapbase_addr(pid_t pid, uint32_t handle)
{
    (void)pid; (void)handle;
    return (intptr_t)((uintptr_t)fake_register & ~(uintptr_t)0xffffffffu);
}
