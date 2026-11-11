// SPDX-License-Identifier: GPL-3.0-only
// Line icons drawn from the kit's shapes, so they stay sharp at any size and take any colour.
// Each fits a square of `size` centred on (cx, cy).
#pragma once

#include "app/psapp/icon_id.hpp"
#include "gfx/draw_list.hpp"

namespace psapp::icons
{

void draw(hui::gfx::DrawList &list, Icon icon, float cx, float cy, float size, hui::gfx::Color color);

// The ProsperoSunshine sun: a disc with rays. `spin` turns the rays (radians), `glow` 0..1 adds the
// halo, `dim` 0..1 greys it out (host stopped).
void sun(hui::gfx::DrawList &list, float cx, float cy, float radius, float spin, float glow, float dim);

} // namespace psapp::icons
