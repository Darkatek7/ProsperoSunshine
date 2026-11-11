// SPDX-License-Identifier: GPL-3.0-only
// The advance widths of Inter SemiBold and Inter Regular (the app's `semibold` and `regular` fonts, ps5-homebrew-ui's
// third_party/fonts at the commit app/build.sh pins), in thousandths of the font size, for ASCII from the space to '~'.
// The settings test measures labels and help texts with them, to find text the tiles or the help bar would cut, without
// the console. Read from the fonts with PIL's ImageFont.getlength at size 1000, one character at a time (no kerning);
// for the page's labels the sums come within half a pixel of the whole strings' widths.
#pragma once

#include <string_view>

namespace psapp_test
{

inline constexpr unsigned short kInterSemiBold[95] = {
    252, 321, 523, 644, 650, 1004, 663, 326, 373, 373, 540, 673, 319, 465, 319, 379,
    660, 423, 623, 636, 666, 612, 640, 576, 640, 640, 319, 329, 673, 673, 673, 543,
    999, 728, 659, 737, 722, 605, 588, 749, 746, 277, 580, 703, 565, 922, 759, 769,
    645, 773, 652, 650, 660, 736, 728, 1020, 720, 713, 652, 373, 379, 373, 481, 469,
    351, 574, 624, 583, 624, 591, 389, 625, 612, 262, 262, 569, 262, 900, 612, 609,
    624, 624, 397, 549, 353, 612, 587, 839, 569, 588, 566, 455, 359, 455, 673,
};
inline constexpr unsigned short kInterRegular[95] = {
    281, 288, 466, 633, 642, 982, 644, 300, 365, 365, 501, 662, 288, 460, 288, 360,
    631, 407, 610, 618, 646, 593, 620, 566, 619, 620, 288, 302, 662, 662, 662, 511,
    966, 690, 654, 730, 722, 601, 590, 746, 743, 269, 571, 672, 565, 903, 753, 765,
    639, 765, 644, 642, 646, 744, 690, 985, 682, 679, 629, 365, 360, 365, 471, 456,
    323, 562, 612, 571, 612, 583, 370, 613, 591, 242, 242, 549, 242, 876, 591, 600,
    612, 612, 376, 528, 327, 591, 562, 818, 546, 562, 552, 426, 333, 426, 662,
};

// The width in pixels of ASCII text at `size`; other characters count as the widest.
inline float text_width(const unsigned short (&font)[95], std::string_view text, float size, float spacing = 0.0f)
{
    float units = 0.0f;
    for (char c : text)
        units += c >= 32 && c < 127 ? font[c - 32] : 1020.0f;
    return units * size / 1000.0f + spacing * static_cast<float>(text.size());
}

} // namespace psapp_test
