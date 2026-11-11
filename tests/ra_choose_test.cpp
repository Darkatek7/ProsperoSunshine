// SPDX-License-Identifier: GPL-3.0-only
// Firmware gate for the ResourceArbitrator calls (src/platform/ps5/ra_nids.h): NID result against the
// per-firmware table.
#include "platform/ps5/ra_nids.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    const ps_ra_known *fw1300 = ps_ra_find_known(0x13000040u);
    assert(fw1300 && fw1300->offset[PS_RA_REGISTER] == 0xdb0 && fw1300->offset[PS_RA_LIST] == 0x11f0);
    assert(ps_ra_find_known(0x13400000u) == nullptr);
    const ps_ra_known *fw1020 = ps_ra_find_known(0x10200006u);
    assert(fw1020 && fw1020->offset[PS_RA_REGISTER] == 0xde0 && fw1020->offset[PS_RA_LIST] == 0x1360);
    assert(ps_ra_find_known(0x10400000u) == nullptr);
    assert(ps_ra_find_known(0x12000000u) == nullptr);

    uint32_t offset = 1;
    // Known firmware: NID equal to the table is used; a missing NID falls back to the table.
    assert(ps_ra_choose(fw1300, PS_RA_REQUEST, 0x10b0, &offset) == PS_RA_BY_NID && offset == 0x10b0);
    assert(ps_ra_choose(fw1300, PS_RA_REQUEST, 0, &offset) == PS_RA_BY_TABLE && offset == 0x10b0);
    // Known firmware, NID elsewhere: refuse.
    assert(ps_ra_choose(fw1300, PS_RA_REQUEST, 0x1100, &offset) == PS_RA_CONFLICT && offset == 0);
    // Unknown firmware: the NID alone decides.
    assert(ps_ra_choose(nullptr, PS_RA_UNREGISTER, 0xf00, &offset) == PS_RA_BY_NID && offset == 0xf00);
    assert(ps_ra_choose(nullptr, PS_RA_UNREGISTER, 0, &offset) == PS_RA_MISSING && offset == 0);

    // Offsets the NIDs gave in reports from firmware outside the table (host_diag.log in issues 40, 23, 2, 17 and 1): none
    // of these is known, so the NID alone decides, as the host did there. 10.40's are 10.20's: if 10.40 joins the table,
    // with those.
    const struct {
        uint32_t firmware;
        uint32_t offset[PS_RA_COUNT];
    } reported[] = {
        {0x4000042u, {0xd20, 0xf20, 0x1160, 0x12a0}},   // 4.00
        {0x8400005u, {0xdd0, 0xfd0, 0x1210, 0x1350}},   // 8.40
        {0x10400007u, {0xde0, 0xfe0, 0x1220, 0x1360}},  // 10.40
        {0x12700001u, {0xdb0, 0xee0, 0x10b0, 0x11f0}},  // 12.70
        {0x13600007u, {0xdb0, 0xee0, 0x10b0, 0x11f0}},  // 13.60
    };
    for (const auto &r : reported) {
        assert(ps_ra_find_known(r.firmware) == nullptr);
        for (int f = 0; f < PS_RA_COUNT; ++f)
            assert(ps_ra_choose(nullptr, f, r.offset[f], &offset) == PS_RA_BY_NID && offset == r.offset[f]);
    }
    for (int f = 0; f < PS_RA_COUNT; ++f) assert(reported[2].offset[f] == fw1020->offset[f]);

    for (int f = 0; f < PS_RA_COUNT; ++f) assert(std::strlen(ps_ra_nids[f]) == 11 && ps_ra_names[f][0]);
    for (const auto &known : ps_ra_known_firmware) {
        assert((known.firmware & 0xffffu) == 0);
        for (int f = 0; f < PS_RA_COUNT; ++f) assert(known.offset[f] != 0);
    }
    std::cout << "PASS: ResourceArbitrator firmware gate (NID, table fallback, conflict, unknown firmware, offsets from reports)\n";
}
