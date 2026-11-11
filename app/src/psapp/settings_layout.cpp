// SPDX-License-Identifier: GPL-3.0-only
#include "app/psapp/settings_layout.hpp"

#include <algorithm>
#include <cmath>

namespace psapp::settings_layout
{

namespace
{

// The groups in the page's order. Groups with the same band share its rows side by side, each with its tile width in
// proportion to its weight; `columns` 0 puts a group in one row, else it wraps after that many tiles. Host's tiles are
// a little narrower than Playing's: their labels are short, Playing's the longest of the page ("Quit closes the game").
struct GroupPlan
{
    OptionGroup group;
    const char *caption;
    int band;
    int columns;
    float weight;
};

constexpr GroupPlan kPlan[] = {
    {OptionGroup::host, "HOST", 0, 0, 280.0f},
    {OptionGroup::playing, "PLAYING", 0, 0, 360.0f},
    {OptionGroup::network, "NETWORK", 1, 3, 1.0f},
    {OptionGroup::experimental, "EXPERIMENTAL", 2, 0, 1.0f},
};
constexpr int kBands = 3;

const GroupPlan *plan_of(OptionGroup group)
{
    for (const GroupPlan &plan : kPlan)
    {
        if (plan.group == group)
            return &plan;
    }
    return nullptr;
}

} // namespace

const char *caption(OptionGroup group)
{
    const GroupPlan *plan = plan_of(group);
    return plan != nullptr ? plan->caption : "";
}

Layout build(const std::vector<OptionSpec> &specs)
{
    Layout layout;
    layout.tiles.resize(specs.size());
    for (const OptionSpec &spec : specs)
    {
        if (plan_of(spec.group) == nullptr && layout.error.empty())
            layout.error = std::string(spec.key) + " has no group on the settings page";
    }

    float y = kTop;
    for (int band = 0; band < kBands; ++band)
    {
        // The band's groups with their tiles' places in the list.
        struct Part
        {
            const GroupPlan *plan;
            std::vector<std::size_t> members;
            int columns = 0;
            float width = 0.0f;
            float x = 0.0f;
        };
        std::vector<Part> parts;
        for (const GroupPlan &plan : kPlan)
        {
            if (plan.band != band)
                continue;
            Part part{&plan, {}};
            for (std::size_t i = 0; i < specs.size(); ++i)
            {
                if (specs[i].group == plan.group)
                    part.members.push_back(i);
            }
            if (part.members.empty())
                continue;
            const int count = static_cast<int>(part.members.size());
            part.columns = plan.columns > 0 ? std::min(plan.columns, count) : count;
            parts.push_back(std::move(part));
        }
        if (parts.empty())
            continue;

        // One scale for the band's tile widths, so that its groups fill the area's width.
        float weights = 0.0f, gaps = kGroupGap * static_cast<float>(parts.size() - 1);
        int rows = 1;
        for (const Part &part : parts)
        {
            weights += part.plan->weight * static_cast<float>(part.columns);
            gaps += kTileGap * static_cast<float>(part.columns - 1);
            rows = std::max(rows, (static_cast<int>(part.members.size()) + part.columns - 1) / part.columns);
        }
        const float scale = (kRight - kLeft - gaps) / weights;
        float x = kLeft;
        for (Part &part : parts)
        {
            part.x = x;
            part.width = part.plan->weight * scale;
            const float span = part.width * static_cast<float>(part.columns) + kTileGap * static_cast<float>(part.columns - 1);
            // The last group ends at the area's edge exactly, whatever the rounding.
            const float end = &part == &parts.back() ? kRight : x + span;
            layout.captions.push_back({part.plan->group, part.plan->caption, {x, y, end - x, kCaptionHeight - 14.0f}});
            for (std::size_t k = 0; k < part.members.size(); ++k)
            {
                const int row = static_cast<int>(k) / part.columns, column = static_cast<int>(k) % part.columns;
                layout.tiles[part.members[k]] = {x + static_cast<float>(column) * (part.width + kTileGap),
                                                 y + kCaptionHeight + static_cast<float>(row) * (kTileHeight + kRowGap),
                                                 part.width, kTileHeight};
            }
            x = end + kGroupGap;
        }
        y += kCaptionHeight + static_cast<float>(rows) * kTileHeight + static_cast<float>(rows - 1) * kRowGap + kBandGap;
    }

    // The three about sunshine.conf from the left, the web page apart at the right end.
    static const float widths[3] = {250.0f, 400.0f, 340.0f};
    float x = kLeft;
    for (float w : widths)
    {
        layout.actions.push_back({x, kActionsTop, w, 80.0f});
        x += w + 20.0f;
    }
    layout.actions.push_back({kRight - 360.0f, kActionsTop, 360.0f, 80.0f});
    return layout;
}

const Layout &page()
{
    static const Layout layout = build(option_specs());
    return layout;
}

int next(const Layout &layout, int item, Move move)
{
    if (item < 0 || item >= layout.items())
        return -1;
    const Box &from = layout.item(item);
    const bool sideways = move == Move::left || move == Move::right;
    int best = -1;
    float best_rows = 0.0f, best_dx = 0.0f;
    for (int i = 0; i < layout.items(); ++i)
    {
        const Box &to = layout.item(i);
        if (i == item || to.w <= 0.0f)
            continue;
        const float dy = to.y - from.y, dx = to.cx() - from.cx();
        const bool same_row = std::fabs(dy) < 1.0f;
        if (sideways)
        {
            if (!same_row || (move == Move::left ? dx >= 0.0f : dx <= 0.0f))
                continue;
        }
        else if (same_row || (move == Move::up) != (dy < 0.0f))
        {
            continue;
        }
        const float rows = sideways ? 0.0f : std::fabs(dy);
        // The nearest row first, then the nearest centre in it; on a tie the one further left.
        const bool same_rows = best >= 0 && std::fabs(rows - best_rows) < 0.5f;
        const bool same_dx = same_rows && std::fabs(std::fabs(dx) - best_dx) < 0.5f;
        if (best < 0 || rows < best_rows - 0.5f || (same_rows && std::fabs(dx) < best_dx - 0.5f) ||
            (same_dx && to.cx() < layout.item(best).cx()))
        {
            best = i;
            best_rows = rows;
            best_dx = std::fabs(dx);
        }
    }
    return best;
}

} // namespace psapp::settings_layout
