// SPDX-License-Identifier: GPL-3.0-only
// Drawing the screen's sheets over the page: the editors, the PIN and the keyboard, the device and stream lists and
// the web page's sheet. Their input is in screen_sheets.cpp.
#include "app/psapp/screen.hpp"

#include "app/psapp/icons.hpp"
#include "app/psapp/screen_internal.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace psapp
{

// ---- drawing: the sheet -----------------------------------------------------------------------

void Screen::draw_web(hui::gfx::DrawList &list, const Rect &card) const
{
    // Whether it is on and for whom.
    const bool on = snap_.web_known && snap_.web_enabled;
    std::string state;
    Color dot = kFaint;
    if (!snap_.polled)
        state = "Checking the host...";
    else if (!snap_.running)
        state = "The host is stopped. Start it to set up the web page.";
    else if (!snap_.web_known)
        state = "The running host has no web page. Restart it to use this app's host.";
    else if (!on && snap_.web_user.empty())
    {
        state = "Off. Set a login to turn it on.";
        dot = kMuted;
    }
    else if (!on)
    {
        state = "Off. The login " + snap_.web_user + " is kept for Turn on.";
        dot = kMuted;
    }
    else
    {
        state = "On. Log in as " + snap_.web_user + ".";
        dot = kGood;
    }
    const float state_w = fonts_.semibold.measure(state, 26.0f);
    const float sx = card.cx() - (state_w + 30.0f) / 2.0f;
    if (on)
        list.glow({sx - 8.0f, card.y + 245.0f, 16.0f, 16.0f}, 8.0f, 10.0f, dot.with_alpha(0.5f));
    list.circle(sx, card.y + 253.0f, 8.0f, dot);
    hui::ui::text(list, fonts_.semibold, state, sx + 30.0f, card.y + 262.0f, 26.0f, kInk);

    // The address to open.
    const Rect field{card.x + 90.0f, card.y + 292.0f, card.w - 180.0f, 92.0f};
    list.rounded_rect(field, 22.0f, Color::rgb(0x0a0f22, 0.8f));
    list.bordered_rect(field, 22.0f, kClear, 1.5f, on ? kAccent.with_alpha(0.8f) : Color::rgb(0xffffff, 0.12f));
    hui::ui::text(list, fonts_.semibold, shorten(fonts_.semibold, web_address(), 38.0f, field.w - 60.0f), field.cx(),
                  field.y + 60.0f, 38.0f, on ? kInk : kMuted, Align::center);
    hui::ui::text(list, fonts_.regular, "Open it in a browser on a phone or PC in this network.", card.cx(), card.y + 418.0f,
                  22.0f, kMuted, Align::center);
    hui::ui::text(list, fonts_.regular, "The browser warns about the host's own certificate the first time.", card.cx(),
                  card.y + 448.0f, 22.0f, kFaint, Align::center);

    // The rows, at the same place as in handle_web.
    const Rect h = sheet_highlight_.rect(time_);
    list.rounded_rect(h, 18.0f, kAccent.with_alpha(0.18f));
    list.bordered_rect(h, 18.0f, kClear, 2.5f, kAccent);
    const char *labels[kWebRows] = {snap_.web_enabled ? "Turn off" : "Turn on",
                                    snap_.web_user.empty() ? "Set login" : "Change login", "Forget login", "Close"};
    const Icon row_icons[kWebRows] = {Icon::power, Icon::lock, Icon::trash, Icon::undo};
    for (int i = 0; i < kWebRows; ++i)
    {
        const Rect row = web_row_rect(i);
        const bool enabled = web_row_enabled(static_cast<WebRow>(i));
        const Color ink = !enabled ? kFaint : i == web_row_ ? kInk : kMuted;
        icons::draw(list, row_icons[i], row.x + 40.0f, row.cy(), 28.0f, enabled ? kAccent : kFaint);
        hui::ui::text(list, fonts_.semibold, labels[i], row.x + 76.0f, row.cy() + 9.0f, 26.0f, ink);
    }
}

void Screen::draw_sheet(hui::gfx::DrawList &list, std::uint32_t glass) const
{
    if (sheet_drawn_ == Sheet::none)
        return;
    const float a = ease_out(sheet_amount_);
    list.push_opacity(a);
    list.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, Color::rgb(0x04060e, 0.55f));
    const bool keys = sheet_drawn_ == Sheet::pin || sheet_drawn_ == Sheet::text;
    const OptionSpec *spec = find_option(target_);
    float height = keys ? 780.0f : 640.0f;
    if (sheet_drawn_ == Sheet::choice && spec != nullptr)
        height = 300.0f + static_cast<float>(spec->choices.size() + 1) * 76.0f;
    if (sheet_drawn_ == Sheet::remove_device || sheet_drawn_ == Sheet::end_stream || sheet_drawn_ == Sheet::deny_pairing ||
        sheet_drawn_ == Sheet::forget_web)
        height = 270.0f;
    if (sheet_drawn_ == Sheet::web)
        height = kWebSheetHeight;
    const float width = sheet_drawn_ == Sheet::text ? 1120.0f : 880.0f;
    const Rect card = sheet_drawn_ == Sheet::devices || sheet_drawn_ == Sheet::streams
                          ? devices_card()
                          : Rect{960.0f - width / 2.0f, 540.0f - height / 2.0f + 10.0f, width, height};
    list.push_transform(0.94f + 0.06f * a, card.cx(), card.cy(), 0.0f, 30.0f * (1.0f - a));
    list.shadow(card, 34.0f, 60.0f, Color::rgb(0x000000, 0.55f));
    list.glass(glass, card, 34.0f, Color::rgb(0xffffff));
    list.rounded_rect(card, 34.0f, Color::rgb(0x121a36, 0.82f));
    list.bordered_rect(card, 34.0f, kClear, 1.5f, Color::rgb(0xffffff, 0.14f));
    list.gradient_rect({card.x, card.y, card.w, 120.0f}, 34.0f, kAccent.with_alpha(0.14f), kAccent.with_alpha(0.0f));

    const bool device_sheet = sheet_drawn_ == Sheet::devices || sheet_drawn_ == Sheet::remove_device || renaming_ ||
                              sheet_drawn_ == Sheet::streams || sheet_drawn_ == Sheet::end_stream;
    const bool web_sheet = sheet_drawn_ == Sheet::web || sheet_drawn_ == Sheet::forget_web || web_step_ != WebStep::none;
    const Icon icon = sheet_drawn_ == Sheet::pin || sheet_drawn_ == Sheet::deny_pairing ? Icon::link
                      : web_sheet                ? Icon::globe
                      : device_sheet             ? Icon::monitor
                      : spec != nullptr          ? spec->icon
                                                 : Icon::link; // the device's name when pairing
    icons::draw(list, icon, card.cx(), card.y + 62.0f, 40.0f, kAccent);
    hui::ui::text(list, fonts_.display, sheet_title_, card.cx(), card.y + 140.0f, 46.0f, kInk, Align::center);
    hui::ui::text(list, fonts_.regular, sheet_help_, card.cx(), card.y + 184.0f, 24.0f, kMuted, Align::center);

    if (sheet_drawn_ == Sheet::pin)
    {
        const float size = 104.0f, gap = 24.0f;
        const float x0 = card.cx() - (4 * size + 3 * gap) / 2.0f;
        for (int i = 0; i < 4; ++i)
        {
            const Rect cell{x0 + static_cast<float>(i) * (size + gap), card.y + 220.0f, size, 120.0f};
            const bool current = static_cast<int>(buffer_.size()) == i;
            const bool filled = i < static_cast<int>(buffer_.size());
            if (current)
                list.glow(cell, 20.0f, 18.0f, kAccent.with_alpha(0.3f));
            list.rounded_rect(cell, 20.0f, Color::rgb(0x0a0f22, 0.8f));
            list.bordered_rect(cell, 20.0f, kClear, current ? 3.0f : 1.5f,
                               current ? kAccent : filled ? Color::rgb(0xffffff, 0.3f) : Color::rgb(0xffffff, 0.12f));
            if (filled)
                hui::ui::text(list, fonts_.mono, std::string(1, buffer_[static_cast<std::size_t>(i)]), cell.cx(),
                              cell.y + 84.0f, 62.0f, kInk, Align::center);
            else if (current && std::fmod(time_, 1.0f) < 0.6f)
                list.rounded_rect({cell.cx() - 2.0f, cell.y + 34.0f, 4.0f, 52.0f}, 2.0f, kAccent);
        }
    }
    else if (sheet_drawn_ == Sheet::text)
    {
        const Rect field{card.x + 90.0f, card.y + 220.0f, card.w - 180.0f, 96.0f};
        list.rounded_rect(field, 22.0f, Color::rgb(0x0a0f22, 0.8f));
        list.bordered_rect(field, 22.0f, kClear, 3.0f, kAccent);
        float w = 0.0f;
        if (web_step_ == WebStep::password)
        {
            // A dot a character, as many as fit, and the count on the right; R3 shows the text itself (its end where
            // it is longer than the field), which is only drawn, never logged.
            const float count_w = buffer_.empty() ? 0.0f
                                                  : hui::ui::text(list, fonts_.regular, std::to_string(buffer_.size()),
                                                                  field.x + field.w - 30.0f, field.y + 60.0f, 24.0f, kMuted,
                                                                  Align::right);
            const float room = field.w - 90.0f - count_w;
            if (show_password_)
            {
                std::size_t from = 0;
                while (from < buffer_.size() && fonts_.semibold.measure(buffer_.substr(from), 38.0f) > room)
                    ++from;
                // The ellipsis takes about one character's room.
                const std::string shown =
                    from == 0 ? buffer_ : "\xE2\x80\xA6" + buffer_.substr(std::min(from + 1, buffer_.size()));
                w = hui::ui::text(list, fonts_.semibold, shown, field.x + 30.0f, field.y + 62.0f, 38.0f, kInk);
            }
            else
            {
                const int fit = static_cast<int>(room / 22.0f);
                const int dots = std::min(static_cast<int>(buffer_.size()), std::max(0, fit));
                for (int i = 0; i < dots; ++i)
                    list.circle(field.x + 40.0f + static_cast<float>(i) * 22.0f, field.cy(), 7.0f, kInk);
                w = static_cast<float>(dots) * 22.0f;
            }
        }
        else
        {
            w = hui::ui::text(list, fonts_.semibold, buffer_, field.x + 30.0f, field.y + 62.0f, 38.0f, kInk);
        }
        if (buffer_.empty())
            hui::ui::text(list, fonts_.regular,
                          spec != nullptr                  ? std::string("Default (network name)")
                          : web_step_ == WebStep::user     ? std::string("User name")
                          : web_step_ == WebStep::password ? std::string("Password")
                                                           : pair_suggestion_,
                          field.x + 30.0f, field.y + 62.0f, 32.0f, kFaint);
        if (std::fmod(time_, 1.0f) < 0.6f)
            list.rounded_rect({field.x + 34.0f + (buffer_.empty() ? 0.0f : w), field.y + 24.0f, 3.0f, 50.0f}, 1.5f, kAccent);
    }
    if (keys)
    {
        if (!sheet_error_.empty())
            hui::ui::text(list, fonts_.semibold, sheet_error_, card.cx(), card.y + 380.0f, 24.0f, kBad, Align::center);
        hui::ui::Canvas canvas{list, fonts_, glass, time_};
        keyboard_.draw(canvas);
    }

    if (sheet_drawn_ == Sheet::choice && spec != nullptr)
    {
        // The rows sit at the same place as in handle_sheet.
        const float shift = card.y + 220.0f - 400.0f;
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, shift);
        const Rect h = sheet_highlight_.rect(time_);
        list.rounded_rect(h, 18.0f, kAccent.with_alpha(0.18f));
        list.bordered_rect(h, 18.0f, kClear, 2.5f, kAccent);
        const auto it = edits_.find(spec->key);
        int selected = 0;
        for (std::size_t i = 0; it != edits_.end() && it->second && i < spec->choices.size(); ++i)
        {
            if (*it->second == spec->choices[i].value)
                selected = static_cast<int>(i) + 1;
        }
        for (int i = 0; i <= static_cast<int>(spec->choices.size()); ++i)
        {
            const Rect row{660.0f, 400.0f + static_cast<float>(i) * 76.0f, 600.0f, 64.0f};
            const std::string label = i == 0 ? std::string("Default (") + spec->default_label + ")"
                                             : spec->choices[static_cast<std::size_t>(i - 1)].label;
            list.ring(row.x + 40.0f, row.cy(), 13.0f, 2.5f, i == selected ? kAccent : kMuted);
            if (i == selected)
                list.circle(row.x + 40.0f, row.cy(), 6.5f, kAccent);
            hui::ui::text(list, fonts_.semibold, label, row.x + 72.0f, row.cy() + 9.0f, 26.0f, i == choice_ ? kInk : kMuted);
        }
        list.pop_transform();
    }
    if (sheet_drawn_ == Sheet::web)
        draw_web(list, card);
    if (sheet_drawn_ == Sheet::devices)
    {
        const int count = static_cast<int>(snap_.devices.size());
        if (count == 0)
        {
            const std::string empty = !snap_.running       ? "Start the host to see and change the paired devices."
                                      : !snap_.devices_known ? "Restart the host to change the paired devices here."
                                                             : "No device is paired yet.";
            hui::ui::text(list, fonts_.regular, empty, card.cx(), device_row_rect(0).cy() + 8.0f, 26.0f, kMuted, Align::center);
        }
        else
        {
            const Rect h = sheet_highlight_.rect(time_);
            list.rounded_rect(h, 18.0f, kAccent.with_alpha(0.18f));
            list.bordered_rect(h, 18.0f, kClear, 2.5f, kAccent);
        }
        for (int i = device_top_; i < count && i < device_top_ + kDeviceRows; ++i)
        {
            const diag::Device &device = snap_.devices[static_cast<std::size_t>(i)];
            const Rect row = device_row_rect(i - device_top_);
            const bool focused = i == device_row_;
            icons::draw(list, Icon::monitor, row.x + 44.0f, row.cy(), 30.0f, device.streaming ? kGood : kAccent);
            hui::ui::text(list, fonts_.semibold, shorten(fonts_.semibold, diag::device_label(device), 28.0f, row.w - 120.0f),
                          row.x + 86.0f, row.y + 36.0f, 28.0f, focused ? kInk : kMuted);
            hui::ui::text(list, fonts_.regular, diag::device_info(device), row.x + 86.0f, row.y + 66.0f, 21.0f,
                          device.streaming ? kGood : focused ? kMuted : kFaint);
        }
        if (count > kDeviceRows)
        {
            // Where the rows in view sit in the list.
            const Rect first = device_row_rect(0);
            const float span = 90.0f * static_cast<float>(kDeviceRows) - 10.0f;
            const float x = first.x + first.w + 16.0f;
            list.rounded_rect({x, first.y, 5.0f, span}, 2.5f, Color::rgb(0xffffff, 0.1f));
            list.rounded_rect({x, first.y + span * static_cast<float>(device_top_) / static_cast<float>(count), 5.0f,
                               span * static_cast<float>(kDeviceRows) / static_cast<float>(count)},
                              2.5f, kAccent);
        }
    }
    if (sheet_drawn_ == Sheet::streams)
    {
        const int count = static_cast<int>(snap_.streams.size());
        if (count == 0)
        {
            hui::ui::text(list, fonts_.regular, "No one is streaming now.", card.cx(), device_row_rect(0).cy() + 8.0f, 26.0f, kMuted,
                          Align::center);
        }
        else
        {
            const Rect h = sheet_highlight_.rect(time_);
            list.rounded_rect(h, 18.0f, kAccent.with_alpha(0.18f));
            list.bordered_rect(h, 18.0f, kClear, 2.5f, kAccent);
        }
        for (int i = device_top_; i < count && i < device_top_ + kDeviceRows; ++i)
        {
            const diag::Stream &stream = snap_.streams[static_cast<std::size_t>(i)];
            const Rect row = device_row_rect(i - device_top_);
            const bool focused = i == device_row_;
            icons::draw(list, Icon::monitor, row.x + 44.0f, row.cy(), 30.0f, kGood);
            // The time on the right; the address after the name, where there is room.
            const std::string right = stream.role == "starting" ? std::string("Starting") : "for " + diag::duration_text(stream.seconds);
            const float right_w = hui::ui::text(list, fonts_.regular, right, row.x + row.w - 24.0f, row.y + 36.0f, 21.0f,
                                                focused ? kMuted : kFaint, Align::right);
            const float room = row.w - 86.0f - 24.0f - right_w - 24.0f;
            const float name_w = hui::ui::text(list, fonts_.semibold, shorten(fonts_.semibold, diag::stream_label(stream), 28.0f, room * 0.7f),
                                               row.x + 86.0f, row.y + 36.0f, 28.0f, focused ? kInk : kMuted);
            if (!stream.address.empty() && room - name_w - 14.0f > 80.0f)
                hui::ui::text(list, fonts_.regular, shorten(fonts_.regular, stream.address, 21.0f, room - name_w - 14.0f),
                              row.x + 86.0f + name_w + 14.0f, row.y + 36.0f, 21.0f, kFaint);
            // What really reaches the device on the right, under the time: the rate it gets, the round trip and its losses.
            const std::string network = diag::stream_network(stream);
            const float network_w = network.empty() ? 0.0f
                                                    : hui::ui::text(list, fonts_.regular, network, row.x + row.w - 24.0f, row.y + 66.0f,
                                                                    21.0f, focused ? kMuted : kFaint, Align::right) +
                                                          24.0f;
            const std::string info = (stream.role == "joined" ? "Joined, " : "") + diag::stream_info(stream);
            hui::ui::text(list, fonts_.regular, shorten(fonts_.regular, info, 21.0f, row.w - 110.0f - network_w), row.x + 86.0f,
                          row.y + 66.0f, 21.0f, kGood);
        }
        if (count > kDeviceRows)
        {
            const Rect first = device_row_rect(0);
            const float span = 90.0f * static_cast<float>(kDeviceRows) - 10.0f;
            const float x = first.x + first.w + 16.0f;
            list.rounded_rect({x, first.y, 5.0f, span}, 2.5f, Color::rgb(0xffffff, 0.1f));
            list.rounded_rect({x, first.y + span * static_cast<float>(device_top_) / static_cast<float>(count), 5.0f,
                               span * static_cast<float>(kDeviceRows) / static_cast<float>(count)},
                              2.5f, kAccent);
        }
    }
    if (sheet_drawn_ == Sheet::number && spec != nullptr)
    {
        const std::string text = number_default_ ? std::string("Default") : std::to_string(number_);
        const float cy = card.y + 360.0f;
        hui::ui::text(list, fonts_.display, text, card.cx(), cy + 40.0f, number_default_ ? 72.0f : 110.0f, kInk, Align::center);
        if (number_default_)
            hui::ui::text(list, fonts_.regular, spec->default_label, card.cx(), cy + 90.0f, 26.0f, kMuted, Align::center);
        // Arrows and the range.
        for (int side = -1; side <= 1; side += 2)
        {
            const float x = card.cx() + static_cast<float>(side) * 330.0f;
            const Rect r{x - 36.0f, cy - 36.0f, 72.0f, 72.0f};
            list.rounded_rect(r, 36.0f, Color::rgb(0xffffff, 0.08f));
            list.triangle({x - 14.0f, cy - 14.0f, 28.0f, 28.0f}, kAccent, 0.0f, side < 0 ? -1.5708f : 1.5708f);
        }
        const float t = static_cast<float>(number_ - spec->min) / static_cast<float>(std::max(1L, spec->max - spec->min));
        const Rect track{card.x + 140.0f, card.y + 520.0f, card.w - 280.0f, 8.0f};
        list.rounded_rect(track, 4.0f, Color::rgb(0xffffff, 0.1f));
        list.gradient_rect_h({track.x, track.y, std::max(8.0f, track.w * t), track.h}, 4.0f, kAccent, kAccentDeep);
        hui::ui::text(list, fonts_.regular, std::to_string(spec->min), track.x, track.y + 40.0f, 20.0f, kFaint);
        hui::ui::text(list, fonts_.regular, std::to_string(spec->max), track.x + track.w, track.y + 40.0f, 20.0f, kFaint,
                      Align::right);
    }
    list.pop_transform();
    list.pop_opacity();
    draw_footer(list, glass);
}

} // namespace psapp
