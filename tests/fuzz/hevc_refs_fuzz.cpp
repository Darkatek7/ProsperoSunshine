// SPDX-License-Identifier: GPL-3.0-only
// hevc_refs.hpp reads what the console's encoder wrote, on the video thread, for every frame after a loss: whatever
// the bytes are, it must end, stay inside them and give either nothing or references that can be acted on.
#include "fuzz_common.hpp"
#include "video/hevc_refs.hpp"

#include <algorithm>
#include <span>
#include <vector>

using namespace prospero;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider provider(data, size);
    // The first part as the parameter sets; when they are none this reads, a layout of the input's own choosing.
    const std::vector<uint8_t> sets = provider.ConsumeBytes<uint8_t>(provider.ConsumeIntegralInRange<size_t>(0, 200));
    hevc::Layout layout;
    if (const auto read = hevc::layout(sets)) {
        layout = *read;
        FUZZ_CHECK(layout.poc_bits >= 4 && layout.poc_bits <= 16);
        FUZZ_CHECK(layout.extra_bits >= 0 && layout.extra_bits <= 7);
        FUZZ_CHECK(layout.most_missing() >= 7 && layout.most_missing() <= 32767);
    } else {
        layout.poc_bits = provider.ConsumeIntegralInRange<int>(4, 16);
        layout.extra_bits = provider.ConsumeIntegralInRange<int>(0, 7);
        layout.output_flag = provider.ConsumeBool();
        layout.separate_colour_planes = provider.ConsumeBool();
    }
    const std::vector<uint8_t> picture = provider.ConsumeRemainingBytes<uint8_t>();
    const auto refs = hevc::refs(layout, picture);
    if (!refs) return 0;
    if (refs->keyframe) {
        FUZZ_CHECK(refs->used.empty() && refs->kept.empty());
        return 0;
    }
    FUZZ_CHECK(refs->order >= 0 && refs->order < (1 << layout.poc_bits));
    FUZZ_CHECK(refs->used.size() + refs->kept.size() <= 16);
    // Every reference is an earlier picture, and no picture is named twice.
    std::vector<int> all = refs->used;
    all.insert(all.end(), refs->kept.begin(), refs->kept.end());
    std::sort(all.begin(), all.end());
    FUZZ_CHECK(std::adjacent_find(all.begin(), all.end()) == all.end());
    for (const int distance : all) FUZZ_CHECK(distance < 0 && distance >= -16 * 1025);
    return 0;
}
