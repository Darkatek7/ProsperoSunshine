// SPDX-License-Identifier: GPL-3.0-only
// Read-only check of the ResourceArbitrator lookup (docs/firmware-porting.md): loads libSceIpmi and
// libSceResourceArbitrator, resolves the four calls by NID and logs the offsets. It never calls into
// the ResourceArbitrator, does not open Avcap2 and does not touch the pad, so it can run next to a
// streaming host. PASS means every call resolved by NID and, on a known firmware, matches the table.
#include "platform/ps5/ra_resolve.h"
#include <cstdio>
extern "C" {
#include <ps5/kernel.h>
#include <ps5/klog.h>
}

#define LOG(...) klog_printf("ProsperoSunshine: ra_nid " __VA_ARGS__)
using load_fn = int (*)(const char *, size_t, const void *, uint32_t, void *, int *);

namespace {
uint32_t module(load_fn load, const char *name) {
    uint32_t handle = 0;
    if (kernel_dynlib_handle(-1, name, &handle) == 0 && handle) return handle;
    char path[128];
    int result = -1;
    std::snprintf(path, sizeof path, "/system/common/lib/%s", name);
    const int rc = load(path, 0, nullptr, 0, nullptr, &result);
    LOG("load %s rc=%#x result=%#x\n", name, rc, result);
    if (kernel_dynlib_handle(-1, name, &handle)) return 0;
    return handle;
}
}  // namespace

int main() {
    static const char *const kernels[] = {"libkernel_web.sprx", "libkernel_sys.sprx", "libkernel.sprx"};
    load_fn load = nullptr;
    for (const char *k : kernels) {
        uint32_t h = 0;
        if (!kernel_dynlib_handle(-1, k, &h) && h) load = (load_fn)kernel_dynlib_dlsym(-1, h, "sceKernelLoadStartModule");
        if (load) break;
    }
    const uint32_t ra = load && module(load, "libSceIpmi.sprx") ? module(load, "libSceResourceArbitrator.sprx") : 0;
    if (!ra) {
        LOG("result=FAIL modules not loaded\n");
        return 1;
    }
    ps_ra_functions found{};
    const int rc = ps_ra_resolve(ra, &found);
    // The table only stands in for a missing NID; the probe wants every call found by NID.
    bool pass = rc == 0;
    for (int f = 0; f < PS_RA_COUNT; ++f) pass &= found.nid[f] != 0 && found.nid[f] == found.offset[f];
    LOG("result=%s rc=%d firmware=%#x known=%d register=%#x unregister=%#x request=%#x list=%#x\n",
        pass ? "PASS" : "FAIL", rc, found.firmware, found.known_firmware, found.nid[PS_RA_REGISTER],
        found.nid[PS_RA_UNREGISTER], found.nid[PS_RA_REQUEST], found.nid[PS_RA_LIST]);
    return pass ? 0 : 1;
}
