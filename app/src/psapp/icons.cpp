// SPDX-License-Identifier: GPL-3.0-only
#include "app/psapp/icons.hpp"

#include <cmath>

namespace psapp::icons
{

using hui::gfx::Color;
using hui::gfx::Rect;

namespace
{

constexpr float kPi = 3.14159265f;

// Arcs start at 12 o'clock and run clockwise (the kit's convention).
void stroke_arc(hui::gfx::DrawList &l, float cx, float cy, float r, float w, float start, float sweep, Color c)
{
    l.arc(cx, cy, r + w / 2.0f, w, start, sweep, c);
}

} // namespace

void draw(hui::gfx::DrawList &l, Icon icon, float cx, float cy, float size, Color c)
{
    const float s = size / 24.0f; // drawn on a 24-unit grid
    const float w = 2.0f * s;     // stroke
    auto X = [&](float u) { return cx + (u - 12.0f) * s; };
    auto Y = [&](float v) { return cy + (v - 12.0f) * s; };
    auto line = [&](float x1, float y1, float x2, float y2) { l.line(X(x1), Y(y1), X(x2), Y(y2), w, c); };
    auto box = [&](float x, float y, float bw, float bh, float r) {
        const Rect outer{X(x), Y(y), bw * s, bh * s};
        l.bordered_rect(outer, r * s, Color::rgb(0, 0.0f), w, c);
    };

    switch (icon)
    {
    case Icon::home:
        line(3.5f, 11.0f, 12.0f, 3.5f);
        line(12.0f, 3.5f, 20.5f, 11.0f);
        line(6.0f, 9.5f, 6.0f, 20.0f);
        line(18.0f, 9.5f, 18.0f, 20.0f);
        line(6.0f, 20.0f, 18.0f, 20.0f);
        line(10.0f, 20.0f, 10.0f, 14.5f);
        line(14.0f, 20.0f, 14.0f, 14.5f);
        line(10.0f, 14.5f, 14.0f, 14.5f);
        break;
    case Icon::sliders:
        line(4.0f, 7.0f, 20.0f, 7.0f);
        line(4.0f, 17.0f, 20.0f, 17.0f);
        l.circle(X(9.0f), Y(7.0f), 2.8f * s, c);
        l.circle(X(15.0f), Y(17.0f), 2.8f * s, c);
        break;
    case Icon::power:
        stroke_arc(l, cx, cy + 1.0f * s, 7.5f * s, w, 0.75f, 2.0f * kPi - 1.5f, c);
        line(12.0f, 3.0f, 12.0f, 12.0f);
        break;
    case Icon::restart:
        stroke_arc(l, cx, cy, 7.5f * s, w, 0.9f, 2.0f * kPi - 1.4f, c);
        l.triangle({X(13.5f), Y(1.8f), 6.0f * s, 6.0f * s}, c, 0.0f, 1.5708f);
        break;
    case Icon::boot:
        line(12.0f, 15.0f, 12.0f, 3.5f);
        line(12.0f, 3.5f, 7.5f, 8.0f);
        line(12.0f, 3.5f, 16.5f, 8.0f);
        line(4.0f, 14.0f, 4.0f, 20.0f);
        line(20.0f, 14.0f, 20.0f, 20.0f);
        line(4.0f, 20.0f, 20.0f, 20.0f);
        break;
    case Icon::link:
        stroke_arc(l, X(8.0f), Y(16.0f), 3.8f * s, w, 0.0f, 2.0f * kPi, c);
        stroke_arc(l, X(16.0f), Y(8.0f), 3.8f * s, w, 0.0f, 2.0f * kPi, c);
        line(10.5f, 13.5f, 13.5f, 10.5f);
        break;
    case Icon::monitor:
        box(3.0f, 4.0f, 18.0f, 12.0f, 2.0f);
        line(12.0f, 16.0f, 12.0f, 20.0f);
        line(8.0f, 20.0f, 16.0f, 20.0f);
        break;
    case Icon::tag:
        box(3.0f, 5.0f, 18.0f, 14.0f, 3.0f);
        line(7.0f, 10.0f, 17.0f, 10.0f);
        line(7.0f, 14.0f, 13.0f, 14.0f);
        break;
    case Icon::log:
        for (float y : {6.0f, 12.0f, 18.0f})
        {
            l.circle(X(5.0f), Y(y), 1.4f * s, c);
            line(9.0f, y, 20.0f, y);
        }
        break;
    case Icon::timer:
        l.ring(cx, Y(13.0f), 8.0f * s + w / 2.0f, w, c);
        line(12.0f, 13.0f, 12.0f, 8.5f);
        line(12.0f, 13.0f, 15.0f, 15.0f);
        line(10.0f, 2.5f, 14.0f, 2.5f);
        break;
    case Icon::lock:
        box(4.5f, 10.5f, 15.0f, 10.0f, 2.5f);
        stroke_arc(l, cx, Y(10.0f), 4.5f * s, w, -1.5708f, kPi, c);
        line(7.5f, 10.0f, 7.5f, 10.5f);
        line(16.5f, 10.0f, 16.5f, 10.5f);
        l.circle(cx, Y(15.5f), 1.5f * s, c);
        break;
    case Icon::globe:
        l.ring(cx, cy, 9.0f * s + w / 2.0f, w, c);
        line(3.0f, 12.0f, 21.0f, 12.0f);
        stroke_arc(l, X(19.0f), cy, 10.0f * s, w, 3.85f, 1.86f, c);
        stroke_arc(l, X(5.0f), cy, 10.0f * s, w, 0.57f, 1.86f, c);
        break;
    case Icon::plug:
        box(6.0f, 9.0f, 12.0f, 7.0f, 2.0f);
        line(9.5f, 4.0f, 9.5f, 9.0f);
        line(14.5f, 4.0f, 14.5f, 9.0f);
        line(12.0f, 16.0f, 12.0f, 21.0f);
        break;
    case Icon::check:
        line(4.5f, 12.5f, 9.5f, 17.5f);
        line(9.5f, 17.5f, 19.5f, 6.5f);
        break;
    case Icon::save:
        box(4.0f, 4.0f, 16.0f, 16.0f, 2.5f);
        line(8.0f, 4.0f, 8.0f, 9.0f);
        line(8.0f, 9.0f, 15.0f, 9.0f);
        line(15.0f, 9.0f, 15.0f, 4.0f);
        l.circle(cx, Y(14.5f), 2.2f * s, c);
        break;
    case Icon::undo:
        stroke_arc(l, X(13.0f), Y(13.5f), 6.5f * s, w, -2.6f, 3.9f, c);
        l.triangle({X(2.5f), Y(4.5f), 6.5f * s, 6.5f * s}, c, 0.0f, -1.5708f);
        break;
    case Icon::wifi:
        stroke_arc(l, cx, Y(19.0f), 14.0f * s, w, -0.75f, 1.5f, c);
        stroke_arc(l, cx, Y(19.0f), 9.0f * s, w, -0.75f, 1.5f, c);
        stroke_arc(l, cx, Y(19.0f), 4.0f * s, w, -0.75f, 1.5f, c);
        l.circle(cx, Y(19.0f), 1.6f * s, c);
        break;
    case Icon::trash:
        line(4.0f, 6.5f, 20.0f, 6.5f);
        line(9.5f, 6.5f, 9.5f, 3.5f);
        line(9.5f, 3.5f, 14.5f, 3.5f);
        line(14.5f, 3.5f, 14.5f, 6.5f);
        line(6.5f, 6.5f, 7.5f, 20.5f);
        line(7.5f, 20.5f, 16.5f, 20.5f);
        line(16.5f, 20.5f, 17.5f, 6.5f);
        line(10.5f, 10.0f, 10.5f, 17.0f);
        line(13.5f, 10.0f, 13.5f, 17.0f);
        break;
    }
}

void sun(hui::gfx::DrawList &l, float cx, float cy, float r, float spin, float glow, float dim)
{
    auto blend = [&](Color a) {
        const float g = 0.45f * (a.r + a.g + a.b) / 3.0f + 0.12f;
        return Color{a.r + (g - a.r) * dim, a.g + (g - a.g) * dim, a.b + (g - a.b) * dim, a.a};
    };
    const Color hot = blend(Color::rgb(0xffd166));
    const Color warm = blend(Color::rgb(0xff8a1f));
    if (glow > 0.0f)
    {
        l.glow({cx - r, cy - r, 2 * r, 2 * r}, r, r * 1.4f, blend(Color::rgb(0xff9a2e)).with_alpha(0.28f * glow));
        l.glow({cx - r, cy - r, 2 * r, 2 * r}, r, r * 0.5f, blend(Color::rgb(0xffb347)).with_alpha(0.45f * glow));
    }
    // Rays: slim rounded bars around the disc, alternating long and short.
    const int rays = 12;
    for (int i = 0; i < rays; ++i)
    {
        const float a = spin + static_cast<float>(i) * 2.0f * kPi / rays;
        const float length = (i % 2 == 0 ? 0.42f : 0.28f) * r;
        const float distance = r * 1.22f + length / 2.0f;
        const float x = cx + std::sin(a) * distance;
        const float y = cy - std::cos(a) * distance;
        const float thickness = r * 0.09f;
        l.rotated_rect({x - thickness / 2.0f, y - length / 2.0f, thickness, length}, thickness / 2.0f, a,
                       (i % 2 == 0 ? hot : warm).with_alpha(0.95f));
    }
    l.gradient_rect({cx - r, cy - r, 2 * r, 2 * r}, r, hot, warm);
    // A soft highlight on the upper left of the disc.
    l.circle(cx - r * 0.32f, cy - r * 0.34f, r * 0.42f, Color::rgb(0xffffff, 0.12f * (1.0f - dim)));
}

} // namespace psapp::icons
