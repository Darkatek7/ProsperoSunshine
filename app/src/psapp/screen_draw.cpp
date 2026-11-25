// SPDX-License-Identifier: GPL-3.0-only
// Drawing the screen's pages: the shared parts (glass cards, buttons, the light behind, the top bar, the footer), Home,
// Settings and Diagnostics. The sheets are drawn in screen_sheets_draw.cpp.
#include "app/psapp/screen.hpp"

#include "app/psapp/experimental_list.h"
#include "app/psapp/icons.hpp"
#include "app/psapp/screen_internal.hpp"
#include "app/psapp/settings_layout.hpp"
#include "ui/glyphs.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace psapp
{

namespace
{

constexpr float kMessageSeconds = 7.0f;
constexpr float kReportLine = 26.0f;

} // namespace

// ---- drawing: shared parts --------------------------------------------------------------------

void Screen::glass_card(hui::gfx::DrawList &list, std::uint32_t glass, const Rect &r, float radius, float focus) const
{
    list.shadow({r.x, r.y + 10.0f, r.w, r.h}, radius, 30.0f, Color::rgb(0x000000, 0.35f));
    list.glass(glass, r, radius, Color::rgb(0xffffff));
    list.rounded_rect(r, radius, mix(Color::rgb(0x18203f, 0.55f), Color::rgb(0x4a3215, 0.55f), focus));
    list.bordered_rect(r, radius, kClear, 1.5f, mix(Color::rgb(0xffffff, 0.10f), kAccent.with_alpha(0.9f), focus));
    // A thin light along the top edge, as on glass.
    list.gradient_rect({r.x + radius, r.y + 1.0f, r.w - 2.0f * radius, 2.0f}, 1.0f, Color::rgb(0xffffff, 0.16f),
                       Color::rgb(0xffffff, 0.0f));
}

void Screen::draw_button(hui::gfx::DrawList &list, std::uint32_t glass, const Rect &r, int icon,
                         const std::string &label, bool primary, float focus, bool enabled) const
{
    const float radius = r.h / 2.0f;
    const float grow = 4.0f * focus;
    const Rect b{r.x - grow, r.y - grow, r.w + 2 * grow, r.h + 2 * grow};
    Color ink = enabled ? kInk : kFaint;
    if (primary && enabled)
    {
        list.glow(b, radius, 26.0f, kAccent.with_alpha(0.25f + 0.3f * focus));
        list.gradient_rect_h(b, radius, kAccent, kAccentDeep);
        list.gradient_rect({b.x, b.y, b.w, b.h / 2.0f}, radius, Color::rgb(0xffffff, 0.22f), Color::rgb(0xffffff, 0.0f));
        ink = Color::rgb(0x2a1604);
    }
    else
    {
        glass_card(list, glass, b, radius, focus);
    }
    float x = b.x + 34.0f;
    if (focus > 0.5f && enabled)
    {
        // The Cross glyph on the focused button, as on the console's own buttons.
        hui::ui::draw_button(list, fonts_, primary ? hui::ui::GlyphStyle::mono(ink, Color::rgb(0xffffff, 0.0f))
                                                   : hui::ui::GlyphStyle::dark(),
                             hui::ui::Button::cross, x, b.cy(), 34.0f);
        x += 48.0f;
    }
    else if (icon >= 0)
    {
        icons::draw(list, static_cast<Icon>(icon), x + 14.0f, b.cy(), 30.0f, primary ? ink : kAccent);
        x += 48.0f;
    }
    hui::ui::text(list, fonts_.semibold, label, x, b.cy() + 10.0f, 29.0f, ink);
}

void Screen::draw_background(hui::gfx::DrawList &list) const
{
    // Warm light behind the emblem (home) or the tiles (settings), stronger while the host runs.
    const float home = std::max(0.0f, 1.0f - tab_x_);
    const float strength = 0.25f + 0.45f * sun_glow_;
    list.glow({1320.0f, 200.0f, 360.0f, 360.0f}, 180.0f, 520.0f, kAccentDeep.with_alpha(0.16f * strength * home));
    list.glow({1380.0f, 260.0f, 240.0f, 240.0f}, 120.0f, 260.0f, kAccent.with_alpha(0.22f * strength * home));
    list.glow({200.0f, 640.0f, 600.0f, 300.0f}, 150.0f, 420.0f, Color::rgb(0x5b3cc4, 0.14f));
    list.glow({900.0f, 300.0f, 600.0f, 300.0f}, 150.0f, 520.0f, kAccentDeep.with_alpha(0.10f * std::min(1.0f, tab_x_)));
    // Faint drifting embers.
    for (int i = 0; i < 18; ++i)
    {
        const float seed = static_cast<float>(i) * 12.9898f;
        const float x = std::fmod(std::fabs(std::sin(seed) * 43758.5f), 1.0f) * 1920.0f;
        const float speed = 8.0f + std::fmod(static_cast<float>(i) * 7.3f, 14.0f);
        const float y = 1080.0f - std::fmod(time_ * speed + static_cast<float>(i) * 97.0f, 1180.0f);
        const float alpha = 0.10f + 0.12f * (0.5f + 0.5f * std::sin(time_ * 1.3f + seed));
        list.circle(x + 18.0f * std::sin(time_ * 0.4f + seed), y, 2.0f + static_cast<float>(i % 3), kAccent.with_alpha(alpha));
    }
}

void Screen::draw_top_bar(hui::gfx::DrawList &list, std::uint32_t glass) const
{
    icons::sun(list, 112.0f, 78.0f, 15.0f, sun_spin_, 0.6f, sun_dim_ * 0.6f);
    hui::ui::text(list, fonts_.semibold, "PROSPEROSUNSHINE", 150.0f, 87.0f, 24.0f, kInk, Align::left, 6.0f);

    // Tabs between their L1 and R1 glyphs; the selected plate glides.
    const hui::ui::GlyphStyle glyphs = hui::ui::GlyphStyle::dark();
    const float tabs_x = 640.0f;
    hui::ui::draw_button(list, fonts_, glyphs, hui::ui::Button::l1, tabs_x, 78.0f, 30.0f);
    const float t0 = tabs_x + 64.0f, tw = 210.0f;
    const Rect plate{t0 + tab_x_ * (tw + 12.0f), 50.0f, tw, 56.0f};
    list.glass(glass, plate, 28.0f, Color::rgb(0xffffff));
    list.rounded_rect(plate, 28.0f, Color::rgb(0xffffff, 0.10f));
    list.bordered_rect(plate, 28.0f, kClear, 1.5f, kAccent.with_alpha(0.65f));
    const char *names[3] = {"Home", "Settings", "Diagnostics"};
    const Icon tab_icons[3] = {Icon::home, Icon::sliders, Icon::log};
    for (int i = 0; i < 3; ++i)
    {
        const float x = t0 + static_cast<float>(i) * (tw + 12.0f);
        const float selected = 1.0f - std::min(1.0f, std::fabs(tab_x_ - static_cast<float>(i)));
        const Color ink = mix(kMuted, kInk, selected);
        const float label_w = fonts_.semibold.measure(names[i], 26.0f);
        const float start = x + (tw - label_w - 40.0f) / 2.0f;
        icons::draw(list, tab_icons[i], start + 13.0f, 78.0f, 26.0f, mix(kMuted, kAccent, selected));
        hui::ui::text(list, fonts_.semibold, names[i], start + 40.0f, 87.0f, 26.0f, ink);
    }
    // A pairing request waits while the settings are shown.
    if (page_ != Page::home && !snap_.pairings.empty())
    {
        const float pulse = 0.6f + 0.4f * std::sin(time_ * 5.0f);
        list.glow({t0 + tw - 30.0f, 56.0f, 12.0f, 12.0f}, 6.0f, 10.0f, kAccent.with_alpha(0.5f * pulse));
        list.circle(t0 + tw - 24.0f, 62.0f, 7.0f, kAccent);
    }
    hui::ui::draw_button(list, fonts_, glyphs, hui::ui::Button::r1, t0 + 3.0f * tw + 24.0f + 18.0f, 78.0f, 30.0f);

    // The console's address (what Moonlight needs), and the version.
    float right = 1824.0f;
    if (!version_.empty())
    {
        right -= hui::ui::text(list, fonts_.regular, "v" + version_, right, 87.0f, 20.0f, kFaint, Align::right);
        right -= 28.0f;
    }
    if (!snap_.address.empty())
    {
        right -= hui::ui::text(list, fonts_.semibold, snap_.address, right, 87.0f, 24.0f, kInk, Align::right);
        icons::draw(list, Icon::wifi, right - 22.0f, 76.0f, 26.0f, kAccent);
    }
}

void Screen::draw_footer(hui::gfx::DrawList &list, std::uint32_t glass) const
{
    // The bar over the hints: an armed press, the request being worked on, or the last outcome.
    std::string text;
    Color dot = kAccent;
    float alpha = 0.0f;
    bool spinner = false;
    if (armed_ >= 0 && armed_age_ <= kArmSeconds && !busy())
    {
        text = "This ends the running stream. Press Cross again to go on.";
        alpha = 1.0f;
    }
    else if (busy())
    {
        text = snap_.busy;
        alpha = 1.0f;
        spinner = true;
    }
    else if (message_age_ < kMessageSeconds && !snap_.message.empty())
    {
        text = snap_.message;
        dot = snap_.message_ok ? kGood : kBad;
        alpha = std::min({1.0f, message_age_ / 0.25f, (kMessageSeconds - message_age_) / 0.6f});
    }
    if (alpha > 0.0f)
    {
        const float rise = (1.0f - ease_out(std::min(1.0f, message_age_ / 0.35f))) * (spinner ? 0.0f : 24.0f);
        const float w = std::min(1500.0f, fonts_.semibold.measure(text, 25.0f) + 130.0f);
        const Rect bar{960.0f - w / 2.0f, 940.0f + rise, w, 60.0f};
        list.push_opacity(alpha);
        glass_card(list, glass, bar, 30.0f, 0.0f);
        if (spinner)
        {
            list.arc(bar.x + 38.0f, bar.cy(), 13.0f, 4.0f, time_ * 6.0f, 4.2f, kAccent);
        }
        else
        {
            list.glow({bar.x + 30.0f, bar.cy() - 8.0f, 16.0f, 16.0f}, 8.0f, 10.0f, dot.with_alpha(0.5f));
            list.circle(bar.x + 38.0f, bar.cy(), 8.0f, dot);
        }
        hui::ui::text(list, fonts_.semibold, shorten(fonts_.semibold, text, 25.0f, w - 100.0f), bar.x + 68.0f,
                      bar.cy() + 9.0f, 25.0f, kInk);
        list.pop_opacity();
    }

    const hui::ui::GlyphStyle glyphs = hui::ui::GlyphStyle::dark();
    hui::ui::HintLayout layout;
    layout.cy = 1036.0f;
    layout.size = 34.0f;
    layout.text_size = 22.0f;
    if (sheet_ == Sheet::pin || sheet_ == Sheet::text)
    {
        // The password step adds R3 to show it; the other bound keys carry their button on the key.
        const bool password = web_step_ == WebStep::password;
        const hui::ui::Hint hints[] = {{hui::ui::Button::cross, "Type"},
                                       {hui::ui::Button::square, "Delete"},
                                       {hui::ui::Button::right_stick, show_password_ ? "Hide password" : "Show password"},
                                       {hui::ui::Button::r2, "Done"},
                                       {hui::ui::Button::circle, "Cancel"}};
        if (password)
        {
            hui::ui::draw_hints(list, fonts_, glyphs, hints, 5, 1824.0f, true, layout);
        }
        else
        {
            const hui::ui::Hint plain[] = {hints[0], hints[1], hints[3], hints[4]};
            hui::ui::draw_hints(list, fonts_, glyphs, plain, 4, 1824.0f, true, layout);
        }
    }
    else if (sheet_ == Sheet::deny_pairing)
    {
        const hui::ui::Hint hints[] = {{hui::ui::Button::cross, "Deny"}, {hui::ui::Button::circle, "Keep waiting"}};
        hui::ui::draw_hints(list, fonts_, glyphs, hints, 2, 1824.0f, true, layout);
    }
    else if (sheet_ == Sheet::forget_web)
    {
        const hui::ui::Hint hints[] = {{hui::ui::Button::cross, "Forget"}, {hui::ui::Button::circle, "Keep it"}};
        hui::ui::draw_hints(list, fonts_, glyphs, hints, 2, 1824.0f, true, layout);
    }
    else if (sheet_ == Sheet::number)
    {
        const hui::ui::Hint hints[] = {{hui::ui::Button::dpad, "Change"},
                                       {hui::ui::Button::l1, "Big steps", hui::ui::Button::r1},
                                       {hui::ui::Button::square, "Default"},
                                       {hui::ui::Button::cross, "OK"},
                                       {hui::ui::Button::circle, "Cancel"}};
        hui::ui::draw_hints(list, fonts_, glyphs, hints, 5, 1824.0f, true, layout);
    }
    else if (sheet_ == Sheet::choice)
    {
        const hui::ui::Hint hints[] = {{hui::ui::Button::cross, "Choose"}, {hui::ui::Button::circle, "Cancel"}};
        hui::ui::draw_hints(list, fonts_, glyphs, hints, 2, 1824.0f, true, layout);
    }
    else if (sheet_ == Sheet::devices)
    {
        // Without a device, only Close (the last hint) means anything.
        const hui::ui::Hint hints[] = {{hui::ui::Button::cross, "Rename"},
                                       {hui::ui::Button::square, "Remove"},
                                       {hui::ui::Button::circle, "Close"}};
        const bool any = !snap_.devices.empty();
        hui::ui::draw_hints(list, fonts_, glyphs, any ? hints : hints + 2, any ? 3 : 1, 1824.0f, true, layout);
    }
    else if (sheet_ == Sheet::remove_device)
    {
        const hui::ui::Hint hints[] = {{hui::ui::Button::cross, "Remove"}, {hui::ui::Button::circle, "Keep it"}};
        hui::ui::draw_hints(list, fonts_, glyphs, hints, 2, 1824.0f, true, layout);
    }
    else if (sheet_ == Sheet::streams)
    {
        // Without a stream, only Close (the last hint) means anything.
        const hui::ui::Hint hints[] = {{hui::ui::Button::cross, "End stream"}, {hui::ui::Button::circle, "Close"}};
        const bool any = !snap_.streams.empty();
        hui::ui::draw_hints(list, fonts_, glyphs, any ? hints : hints + 1, any ? 2 : 1, 1824.0f, true, layout);
    }
    else if (sheet_ == Sheet::end_stream)
    {
        const hui::ui::Hint hints[] = {{hui::ui::Button::cross, "End stream"}, {hui::ui::Button::circle, "Keep it"}};
        hui::ui::draw_hints(list, fonts_, glyphs, hints, 2, 1824.0f, true, layout);
    }
    else if (sheet_ == Sheet::web)
    {
        const hui::ui::Hint hints[] = {{hui::ui::Button::cross, "Select"}, {hui::ui::Button::circle, "Close"}};
        hui::ui::draw_hints(list, fonts_, glyphs, hints, 2, 1824.0f, true, layout);
    }
    else if (page_ == Page::home)
    {
        // On a pairing card, Square denies the request.
        const bool on_request = home_row_ == 1 && home_card_ < visible_pairings();
        const hui::ui::Hint hints[] = {{hui::ui::Button::cross, on_request ? "Enter PIN" : "Select"},
                                       {hui::ui::Button::square, "Deny"},
                                       {hui::ui::Button::triangle, "Reload"},
                                       {hui::ui::Button::l1, "Pages", hui::ui::Button::r1}};
        if (on_request)
        {
            hui::ui::draw_hints(list, fonts_, glyphs, hints, 4, 1824.0f, true, layout);
        }
        else
        {
            const hui::ui::Hint plain[] = {hints[0], hints[2], hints[3]};
            hui::ui::draw_hints(list, fonts_, glyphs, plain, 3, 1824.0f, true, layout);
        }
    }
    else if (page_ == Page::diagnostics)
    {
        const hui::ui::Hint hints[] = {{hui::ui::Button::dpad, "Scroll"},
                                       {hui::ui::Button::cross, "Collect again"},
                                       {hui::ui::Button::l1, "Pages", hui::ui::Button::r1}};
        hui::ui::draw_hints(list, fonts_, glyphs, hints, 3, 1824.0f, true, layout);
    }
    else
    {
        const hui::ui::Hint hints[] = {{hui::ui::Button::cross, "Change"},
                                       {hui::ui::Button::square, "Default"},
                                       {hui::ui::Button::triangle, "Reload"},
                                       {hui::ui::Button::l1, "Pages", hui::ui::Button::r1}};
        hui::ui::draw_hints(list, fonts_, glyphs, hints, 4, 1824.0f, true, layout);
    }
}

// ---- drawing: the home page -------------------------------------------------------------------

void Screen::draw_home(hui::gfx::DrawList &list, std::uint32_t glass) const
{
    const bool running = snap_.polled && snap_.running;
    std::string state = "Checking the host";
    std::string detail = "One moment.";
    if (snap_.polled && !snap_.running)
    {
        state = "Host stopped";
        detail = "Start it to stream this console to Moonlight.";
    }
    else if (running && !snap_.poll_error.empty())
    {
        state = "Host busy";
        detail = "It did not answer: " + snap_.poll_error;
    }
    else if (snap_.streaming && !live())
    {
        state = "Session paused";
        detail = "\"" + snap_.app + "\" is still open with no one connected. Resume or quit it in Moonlight.";
    }
    else if (snap_.streaming)
    {
        state = "Streaming";
        detail = snap_.streams.size() > 1 ? std::to_string(snap_.streams.size()) + " devices are playing \"" + snap_.app + "\"."
                 : snap_.client.empty()    ? "Moonlight is playing \"" + snap_.app + "\"."
                                      : snap_.client + (snap_.client_address.empty() ? "" : " (" + snap_.client_address + ")") +
                                            " is playing \"" + snap_.app + "\".";
    }
    else if (running)
    {
        state = "Ready to stream";
        detail = snap_.address.empty() ? "Add this console in Moonlight by its IP address."
                                       : "In Moonlight, add " + snap_.address + " and start \"PS5 Screen\".";
    }

    // The hero: caption, title, detail, chips.
    float a = appear(0);
    list.push_opacity(a);
    list.push_transform(1.0f, 0.0f, 0.0f, static_cast<float>(page_direction_) * 40.0f * (1.0f - a), 0.0f);
    list.rounded_rect({96.0f, 222.0f, 40.0f, 3.0f}, 1.5f, kAccent);
    hui::ui::text(list, fonts_.semibold, "HOST STATUS", 152.0f, 232.0f, 22.0f, kAccent, Align::left, 4.0f);
    hui::ui::text(list, fonts_.display, state, 90.0f, 350.0f, 104.0f, kInk);
    hui::ui::text(list, fonts_.regular, shorten(fonts_.regular, detail, 30.0f, 1120.0f), 96.0f, 408.0f, 30.0f, kMuted);
    list.pop_transform();
    list.pop_opacity();

    a = appear(1);
    list.push_opacity(a);
    list.push_transform(1.0f, 0.0f, 0.0f, static_cast<float>(page_direction_) * 40.0f * (1.0f - a), 0.0f);
    float x = 96.0f;
    auto chip = [&](Icon icon, const std::string &text, bool on) {
        const float w = fonts_.semibold.measure(text, 22.0f) + 74.0f;
        const Rect r{x, 444.0f, w, 50.0f};
        list.glass(glass, r, 25.0f, Color::rgb(0xffffff));
        list.rounded_rect(r, 25.0f, on ? kAccent.with_alpha(0.16f) : Color::rgb(0xffffff, 0.06f));
        list.bordered_rect(r, 25.0f, kClear, 1.5f, on ? kAccent.with_alpha(0.55f) : Color::rgb(0xffffff, 0.12f));
        icons::draw(list, icon, x + 30.0f, r.cy(), 22.0f, on ? kAccent : kMuted);
        hui::ui::text(list, fonts_.semibold, text, x + 52.0f, r.cy() + 8.0f, 22.0f, kInk);
        x += w + 14.0f;
    };
    const auto saved = [&](const char *key, const std::string &fallback) {
        const auto it = snap_.config.find(key);
        return it != snap_.config.end() && it->second ? *it->second : fallback;
    };
    chip(Icon::plug, "Port " + saved("port", "47989"), false);
    const std::string lan = saved("lan_encryption_mode", "0");
    chip(Icon::lock, lan == "0" ? "LAN unencrypted" : lan == "1" ? "LAN encrypted when supported" : "LAN encrypted", lan != "0");
    chip(Icon::boot, autoload::boot_chip(snap_.boot_start), snap_.boot_start == autoload::BootStart::on);
    const std::size_t paired = diag::paired_names(snap_.running && snap_.devices_known, snap_.devices, snap_.paired).size();
    chip(Icon::monitor, std::to_string(paired) + (paired == 1 ? " paired device" : " paired devices"), false);
    list.pop_transform();
    list.pop_opacity();

    // The actions.
    a = appear(2);
    list.push_opacity(a);
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, 30.0f * (1.0f - a));
    const bool enabled = !busy();
    draw_button(list, glass, action_rect(0), static_cast<int>(Icon::power), running ? "Stop host" : "Start host",
                !running, action_amount_[0], enabled);
    draw_button(list, glass, action_rect(1), static_cast<int>(Icon::restart), "Restart host", false, action_amount_[1],
                enabled);
    {
        const Rect r = action_rect(2);
        draw_button(list, glass, r, static_cast<int>(Icon::boot), "Start at boot", false, action_amount_[2], enabled);
        const Rect track{r.x + r.w - 104.0f, r.cy() - 19.0f, 76.0f, 38.0f};
        const bool boot_on = snap_.boot_start == autoload::BootStart::on;
        const bool boot_unknown = snap_.boot_start == autoload::BootStart::unknown;
        list.rounded_rect(track, 19.0f, boot_on ? kAccent : Color::rgb(0x3a4363));
        if (boot_on)
            list.glow(track, 19.0f, 12.0f, kAccent.with_alpha(0.35f));
        // Unknown: the knob in the middle, dimmed, neither on nor off.
        const float knob = boot_on ? track.x + track.w - 19.0f : boot_unknown ? track.x + track.w / 2.0f : track.x + 19.0f;
        list.circle(knob, track.cy(), 15.0f, Color::rgb(0xffffff, boot_unknown ? 0.45f : 1.0f));
    }
    // What the firmware or the last capture attempt means for streaming, above the boot hint; first the control socket's
    // development switch while it is on.
    const diag::Notice notice = diag::with_control_lan(snap_.notice, snap_.running && snap_.control_lan);
    if (!notice.text.empty())
        hui::ui::text(list, fonts_.semibold, shorten(fonts_.semibold, notice.text, 22.0f, 1728.0f), 96.0f, 660.0f, 22.0f,
                      notice.bad ? kBad : kAccent);
    const float boot_hint_y = notice.text.empty() ? 660.0f : 690.0f;
    // Unknown says so, and nothing that assumes on or off; before the first read there is nothing to say yet.
    if (snap_.boot_start == autoload::BootStart::unknown)
    {
        if (snap_.files_read)
            hui::ui::text(list, fonts_.regular, autoload::boot_unknown_hint(running), action_rect(2).x, boot_hint_y, 20.0f, kFaint);
    }
    else if (snap_.boot_start == autoload::BootStart::on && snap_.manager_present && !snap_.manager_autoload)
        hui::ui::text(list, fonts_.regular, "The Payload Manager's own autoload is off, so nothing starts at boot.",
                      action_rect(2).x, boot_hint_y, 20.0f, kAccent);
    else if (!snap_.manager_present)
        hui::ui::text(list, fonts_.regular, "Start at boot needs the Payload Manager (PLDM00001).", action_rect(2).x,
                      boot_hint_y, 20.0f, kFaint);
    list.pop_transform();
    list.pop_opacity();

    // The emblem: the sun turns while the host runs and sends rings out while it streams.
    a = appear(1);
    const float ex = 1500.0f, ey = 360.0f;
    list.push_opacity(a);
    list.push_transform(0.9f + 0.1f * a, ex, ey, 0.0f, 0.0f);
    if (live())
    {
        for (int i = 0; i < 3; ++i)
        {
            const float t = std::fmod(pulse_ * 0.45f + static_cast<float>(i) / 3.0f, 1.0f);
            list.ring(ex, ey, 150.0f + t * 230.0f, 3.0f, kAccent.with_alpha(0.5f * (1.0f - t)));
        }
    }
    const float breathe = running ? 1.0f + 0.02f * std::sin(time_ * 1.6f) : 1.0f;
    icons::sun(list, ex, ey, 112.0f * breathe, sun_spin_, sun_glow_, sun_dim_);
    // Its caption.
    const std::string badge = live() ? "LIVE" : snap_.streaming ? "PAUSED" : running ? "READY" : snap_.polled ? "OFF" : "...";
    const Color badge_color = live() ? kBad : snap_.streaming ? kAccent : running ? kGood : kFaint;
    const float bw = fonts_.semibold.measure(badge, 20.0f) + 60.0f;
    const Rect br{ex - bw / 2.0f, ey + 190.0f, bw, 42.0f};
    list.glass(glass, br, 21.0f, Color::rgb(0xffffff));
    list.rounded_rect(br, 21.0f, Color::rgb(0x10162e, 0.6f));
    const float blink = live() ? 0.55f + 0.45f * std::sin(time_ * 4.0f) : 1.0f;
    list.circle(br.x + 24.0f, br.cy(), 6.0f, badge_color.with_alpha(blink));
    hui::ui::text(list, fonts_.semibold, badge, br.x + 40.0f, br.cy() + 7.0f, 20.0f, kInk, Align::left, 3.0f);
    list.pop_transform();
    list.pop_opacity();

    // Pairing requests, and who is paired.
    a = appear(3);
    list.push_opacity(a);
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, 40.0f * (1.0f - a));
    list.rounded_rect({96.0f, 706.0f, 40.0f, 3.0f}, 1.5f, kAccent);
    const int cards = visible_pairings();
    hui::ui::text(list, fonts_.semibold, cards == 0 && streams_shown() ? "STREAMS" : "PAIRING", 152.0f, 716.0f, 22.0f, kAccent,
                  Align::left, 4.0f);
    if (cards == 0 && !streams_shown())
    {
        const Rect r{96.0f, 740.0f, 1100.0f, 172.0f};
        glass_card(list, glass, r, 26.0f, 0.0f);
        list.circle(r.x + 76.0f, r.cy(), 40.0f, Color::rgb(0xffffff, 0.06f));
        icons::draw(list, Icon::link, r.x + 76.0f, r.cy(), 38.0f, running ? kAccent : kFaint);
        hui::ui::text(list, fonts_.semibold, running ? "No one is waiting to pair" : "Pairing needs the host",
                      r.x + 144.0f, r.y + 72.0f, 32.0f, kInk);
        const std::string hint = running ? (snap_.address.empty() ? "In Moonlight, add this console and pick it. The PIN goes in here."
                                                                  : "In Moonlight, add " + snap_.address + " and pick it. The PIN goes in here.")
                                         : "Start the host, then pair from Moonlight.";
        hui::ui::text(list, fonts_.regular, shorten(fonts_.regular, hint, 24.0f, r.w - 180.0f), r.x + 144.0f,
                      r.y + 116.0f, 24.0f, kMuted);
    }
    for (int i = 0; i < cards; ++i)
    {
        const Pairing &p = snap_.pairings[static_cast<std::size_t>(i)];
        const Rect r = pairing_rect(i);
        const float f = card_amount_[i];
        const float pulse = 0.5f + 0.5f * std::sin(time_ * 4.0f);
        list.glow(r, 26.0f, 22.0f, kAccent.with_alpha(0.18f + 0.18f * pulse + 0.2f * f));
        glass_card(list, glass, r, 26.0f, std::max(f, 0.35f));
        list.circle(r.x + 68.0f, r.y + 70.0f, 36.0f, kAccent.with_alpha(0.18f));
        icons::draw(list, Icon::link, r.x + 68.0f, r.y + 70.0f, 34.0f, kAccent);
        hui::ui::text(list, fonts_.semibold, shorten(fonts_.semibold, diag::pairing_label(p.name), 32.0f, r.w - 150.0f),
                      r.x + 126.0f, r.y + 66.0f, 32.0f, kInk);
        hui::ui::text(list, fonts_.regular, p.address.empty() ? "wants to pair" : "from " + p.address + " wants to pair",
                      r.x + 126.0f, r.y + 100.0f, 22.0f, kMuted);
        const Rect pill{r.x + 30.0f, r.y + r.h - 66.0f, 230.0f, 46.0f};
        list.rounded_rect(pill, 23.0f, kAccent.with_alpha(0.18f + 0.3f * f));
        hui::ui::draw_button(list, fonts_, hui::ui::GlyphStyle::dark(), hui::ui::Button::cross, pill.x + 16.0f, pill.cy(), 28.0f);
        hui::ui::text(list, fonts_.semibold, "Enter PIN", pill.x + 58.0f, pill.cy() + 8.0f, 23.0f, kInk);
        const Rect deny{pill.x + pill.w + 16.0f, pill.y, 150.0f, pill.h};
        list.rounded_rect(deny, 23.0f, Color::rgb(0xffffff, 0.06f + 0.06f * f));
        hui::ui::draw_button(list, fonts_, hui::ui::GlyphStyle::dark(), hui::ui::Button::square, deny.x + 16.0f, deny.cy(), 28.0f);
        hui::ui::text(list, fonts_.semibold, "Deny", deny.x + 58.0f, deny.cy() + 8.0f, 23.0f, kMuted);
    }
    if (snap_.pairings.size() > static_cast<std::size_t>(cards))
        hui::ui::text(list, fonts_.regular, "+" + std::to_string(snap_.pairings.size() - static_cast<std::size_t>(cards)) + " more",
                      cards == 1 && streams_shown() ? 634.0f : 1196.0f, 726.0f, 20.0f, kFaint, Align::right);
    if (streams_shown())
    {
        // Who streams now: a row each for the first two, the rest in the list behind Cross.
        const Rect r = streams_rect();
        const float pulse = 0.5f + 0.5f * std::sin(time_ * 3.0f);
        list.glow(r, 26.0f, 18.0f, kGood.with_alpha(0.06f + 0.06f * pulse));
        glass_card(list, glass, r, 26.0f, streams_amount_);
        hui::ui::text(list, fonts_.semibold, "STREAMING NOW", r.x + 30.0f, r.y + 44.0f, 19.0f, kMuted, Align::left, 3.0f);
        const std::size_t shown = std::min<std::size_t>(snap_.streams.size(), 2);
        if (streams_amount_ > 0.01f)
        {
            list.push_opacity(streams_amount_);
            const float label = hui::ui::text(list, fonts_.semibold, "Manage", r.x + r.w - 30.0f, r.y + 45.0f, 21.0f, kInk, Align::right);
            hui::ui::draw_button(list, fonts_, hui::ui::GlyphStyle::dark(), hui::ui::Button::cross, r.x + r.w - 30.0f - label - 46.0f,
                                 r.y + 38.0f, 28.0f);
            list.pop_opacity();
        }
        else if (snap_.streams.size() > shown)
        {
            hui::ui::text(list, fonts_.regular, "+" + std::to_string(snap_.streams.size() - shown) + " more", r.x + r.w - 30.0f,
                          r.y + 45.0f, 20.0f, kFaint, Align::right);
        }
        for (std::size_t i = 0; i < shown; ++i)
        {
            const diag::Stream &stream = snap_.streams[i];
            const float cy = r.y + 90.0f + static_cast<float>(i) * 48.0f;
            list.circle(r.x + 40.0f, cy, 6.0f, kGood.with_alpha(0.55f + 0.45f * pulse));
            const std::string time = diag::duration_text(stream.seconds);
            const float time_w = hui::ui::text(list, fonts_.regular, time, r.x + r.w - 30.0f, cy + 8.0f, 20.0f, kFaint, Align::right);
            // The name, then what it gets in the room that is left.
            const float room = r.w - 60.0f - 30.0f - time_w - 20.0f;
            const std::string name = shorten(fonts_.semibold, diag::stream_label(stream), 22.0f, std::min(room, 260.0f));
            const float name_w = hui::ui::text(list, fonts_.semibold, name, r.x + 60.0f, cy + 8.0f, 22.0f, kInk);
            const std::string info = (stream.role == "joined" ? "joined, " : "") + diag::stream_info(stream);
            if (room - name_w - 16.0f > 60.0f)
                hui::ui::text(list, fonts_.regular, shorten(fonts_.regular, info, 20.0f, room - name_w - 16.0f),
                              r.x + 60.0f + name_w + 16.0f, cy + 8.0f, 20.0f, kMuted);
        }
    }
    {
        const Rect r = kPairedDevices;
        glass_card(list, glass, r, 26.0f, devices_amount_);
        hui::ui::text(list, fonts_.semibold, "PAIRED DEVICES", r.x + 30.0f, r.y + 44.0f, 19.0f, kMuted, Align::left, 3.0f);
        if (devices_amount_ > 0.01f)
        {
            list.push_opacity(devices_amount_);
            const float label = hui::ui::text(list, fonts_.semibold, "Manage", r.x + r.w - 30.0f, r.y + 45.0f, 21.0f, kInk, Align::right);
            hui::ui::draw_button(list, fonts_, hui::ui::GlyphStyle::dark(), hui::ui::Button::cross, r.x + r.w - 30.0f - label - 46.0f, r.y + 38.0f, 28.0f);
            list.pop_opacity();
        }
        const std::vector<std::string> paired = diag::paired_names(snap_.running && snap_.devices_known, snap_.devices, snap_.paired);
        if (paired.empty())
        {
            hui::ui::text(list, fonts_.regular, "None yet.", r.x + 30.0f, r.y + 96.0f, 24.0f, kFaint);
        }
        float cx = r.x + 30.0f, cy = r.y + 70.0f;
        for (std::size_t i = 0; i < paired.size(); ++i)
        {
            const std::string name = shorten(fonts_.semibold, paired[i], 21.0f, 220.0f);
            const float w = fonts_.semibold.measure(name, 21.0f) + 62.0f;
            if (cx + w > r.x + r.w - 24.0f)
            {
                cx = r.x + 30.0f;
                cy += 50.0f;
                if (cy > r.y + r.h - 44.0f)
                    break;
            }
            const Rect c{cx, cy, w, 40.0f};
            list.rounded_rect(c, 20.0f, Color::rgb(0xffffff, 0.07f));
            icons::draw(list, Icon::monitor, cx + 24.0f, c.cy(), 20.0f, kAccent);
            hui::ui::text(list, fonts_.semibold, name, cx + 44.0f, c.cy() + 7.0f, 21.0f, kInk);
            cx += w + 10.0f;
        }
    }
    list.pop_transform();
    list.pop_opacity();
}

// ---- drawing: the settings page ---------------------------------------------------------------

void Screen::draw_settings(hui::gfx::DrawList &list, std::uint32_t glass) const
{
    float a = appear(0);
    list.push_opacity(a);
    list.push_transform(1.0f, 0.0f, 0.0f, static_cast<float>(page_direction_) * 40.0f * (1.0f - a), 0.0f);
    const float tw = hui::ui::text(list, fonts_.display, "Settings", 90.0f, 250.0f, 72.0f, kInk);
    hui::ui::text(list, fonts_.regular, "sunshine.conf on this console. The host reads it when it starts.", 120.0f + tw,
                  248.0f, 24.0f, kMuted);
    list.pop_transform();
    list.pop_opacity();

    // The parts come in from the top left, row by row.
    const auto order = [](const layout::Box &box) {
        return 1 + static_cast<int>((box.y - layout::kTop) / 100.0f) + static_cast<int>((box.x - layout::kLeft) / 300.0f);
    };
    const layout::Layout &page = layout::page();
    const int tiles = settings_tiles();

    // A caption over each group; the Experimental one says what its switches are for on this firmware.
    const std::string where = snap_.firmware != 0 ? "Firmware " + diag::firmware_text(snap_.firmware) : std::string("this firmware");
    for (const layout::Caption &caption : page.captions)
    {
        a = appear(order(caption.box));
        list.push_opacity(a);
        const Rect r = rect_of(caption.box);
        list.rounded_rect({r.x, r.y + 6.0f, 40.0f, 3.0f}, 1.5f, kAccent);
        const float caption_w = hui::ui::text(list, fonts_.semibold, caption.text, r.x + 56.0f, r.y + 16.0f, 22.0f, kAccent, Align::left, 4.0f);
        if (caption.group == OptionGroup::experimental)
        {
            bool any_needed = false;
            for (int i = 0; i < tiles; ++i)
            {
                const OptionSpec &spec = option_specs()[static_cast<std::size_t>(i)];
                any_needed = any_needed || (experimental_switch(spec) >= 0 && !switch_not_needed(i));
            }
            const std::string note = any_needed ? "For what is untested on " + where + ". Off by default; what fails turns itself off."
                                                : "Nothing to switch on " + where + ": all of this is verified there.";
            hui::ui::text(list, fonts_.regular, shorten(fonts_.regular, note, 22.0f, r.w - caption_w - 80.0f), r.x + 80.0f + caption_w,
                          r.y + 16.0f, 22.0f, kMuted);
        }
        list.pop_opacity();
    }

    for (int i = 0; i < tiles; ++i)
    {
        const OptionSpec &spec = option_specs()[static_cast<std::size_t>(i)];
        const bool toggle = spec.kind == OptionKind::toggle;
        const bool inert = switch_not_needed(i);
        const auto edited = edits_.find(spec.key);
        const auto saved = loaded_.find(spec.key);
        const bool changed = edits_loaded_ && edited != edits_.end() && saved != loaded_.end() &&
                             edited->second != saved->second;
        const bool is_default = edited == edits_.end() || !edited->second;
        const bool on = toggle && !is_default && *edited->second == "1";
        const float f = tile_amount_[static_cast<std::size_t>(i)];
        a = appear(order(page.tiles[static_cast<std::size_t>(i)]));
        const Rect base = tile_rect(i);
        list.push_opacity(a);
        list.push_transform(1.0f + 0.02f * f, base.cx(), base.cy(), 0.0f, 30.0f * (1.0f - a));
        if (f > 0.01f)
            list.glow(base, 24.0f, 26.0f, kAccent.with_alpha(0.28f * f));
        glass_card(list, glass, base, 24.0f, f);
        const float text_x = base.x + layout::kTextX;
        icons::draw(list, spec.icon, base.x + layout::kIconX, base.cy(), 30.0f, inert ? kFaint : kAccent);
        hui::ui::text(list, fonts_.semibold,
                      shorten(fonts_.semibold, spec.label, layout::kLabelSize, base.w - layout::kTextX - layout::kTextRight), text_x,
                      base.y + 37.0f, layout::kLabelSize, inert ? kMuted : kInk);
        // The value's line: the value, then default or changed, then a switch's track. Where the value would not fit
        // beside it, the tag stays out: a default value shows in its muted colour, and the Save button shows a change.
        const Rect track{base.x + base.w - 84.0f, base.y + 46.0f, 64.0f, 32.0f};
        const float end = toggle && !inert ? track.x - 12.0f : base.x + base.w - layout::kTextRight;
        const std::string value = value_text(i);
        std::string tag = inert ? "" : changed ? "UNSAVED" : is_default && !toggle ? "DEFAULT" : "";
        float tag_w = tag.empty() ? 0.0f : fonts_.semibold.measure(tag, 15.0f) + 30.0f;
        if (!tag.empty() && text_x + fonts_.semibold.measure(value, layout::kValueSize) > end - tag_w - 12.0f)
        {
            tag.clear();
            tag_w = 0.0f;
        }
        const float value_end = tag.empty() ? end : end - tag_w - 12.0f;
        hui::ui::text(list, fonts_.semibold, shorten(fonts_.semibold, value, layout::kValueSize, value_end - text_x), text_x,
                      base.y + 70.0f, layout::kValueSize, is_default || inert ? kMuted : mix(kInk, kAccent, 0.35f));
        if (toggle && !inert)
        {
            list.rounded_rect(track, 16.0f, on ? kAccent : Color::rgb(0x3a4363));
            if (on)
                list.glow(track, 16.0f, 12.0f, kAccent.with_alpha(0.35f));
            list.circle(on ? track.x + track.w - 16.0f : track.x + 16.0f, track.cy(), 13.0f, Color::rgb(0xffffff));
        }
        if (!tag.empty())
        {
            const Rect t{end - tag_w, base.y + 48.0f, tag_w, 28.0f};
            list.rounded_rect(t, 14.0f, changed ? kAccent.with_alpha(0.22f) : Color::rgb(0xffffff, 0.07f));
            hui::ui::text(list, fonts_.semibold, tag, t.cx(), t.cy() + 5.5f, 15.0f, changed ? kAccent : kMuted, Align::center, 2.0f);
        }
        list.pop_transform();
        list.pop_opacity();
    }

    a = appear(12);
    list.push_opacity(a);
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, 30.0f * (1.0f - a));
    const bool enabled = !busy();
    const auto action_focus = [&](int action) { return tile_amount_[static_cast<std::size_t>(tiles + action)]; };
    draw_button(list, glass, settings_action_rect(0), static_cast<int>(Icon::save), dirty() ? "Save" : "Saved",
                dirty(), action_focus(0), enabled && dirty());
    draw_button(list, glass, settings_action_rect(1), static_cast<int>(Icon::restart), "Save and restart host", false,
                action_focus(1), enabled);
    draw_button(list, glass, settings_action_rect(2), static_cast<int>(Icon::undo), "Revert changes", false,
                action_focus(2), dirty());
    const bool web_on = snap_.web_known && snap_.web_enabled;
    draw_button(list, glass, settings_action_rect(3), static_cast<int>(Icon::globe),
                !snap_.web_known ? "Web page" : web_on ? "Web page: on" : "Web page: off", false, action_focus(3), true);

    // What the focused part does.
    std::string help;
    std::string extra;
    if (tile_ < tiles && switch_not_needed(tile_))
    {
        const OptionSpec &spec = option_specs()[static_cast<std::size_t>(tile_)];
        help = diag::experimental_no_effect(snap_.firmware, spec.key)
                   ? std::string(ps_exp_switch_at(experimental_switch(spec))->no_effect_help)
                   : "Verified on " + where + ": it is there without a switch, and this switch changes nothing.";
    }
    else if (tile_ < tiles)
    {
        const OptionSpec &spec = option_specs()[static_cast<std::size_t>(tile_)];
        help = spec.help;
        extra = std::string("Default: ") + spec.default_label;
    }
    else if (tile_ == tiles)
        help = "Writes the changes to sunshine.conf. They apply when the host starts next, Controller's within seconds.";
    else if (tile_ == tiles + 1)
        help = "Writes the changes and restarts the host, which ends a running stream.";
    else if (tile_ == tiles + 2)
        help = "Puts back what sunshine.conf holds now.";
    else
    {
        help = "The host's page for a browser on your network: " + web_address();
        extra = !snap_.polled            ? std::string()
                : !snap_.running         ? std::string("Host stopped")
                : !snap_.web_known       ? std::string("Not in this host")
                : snap_.web_enabled      ? "On for " + snap_.web_user
                : snap_.web_user.empty() ? std::string("Off: no login")
                                         : "Off, login " + snap_.web_user + " kept";
    }
    const Rect bar = rect_of(layout::kHelpBar);
    glass_card(list, glass, bar, 18.0f, 0.0f);
    list.circle(bar.x + 32.0f, bar.cy(), 6.0f, kAccent);
    // The help and the default side by side; a long help gets the smaller size before it is cut.
    const float extra_w = extra.empty() ? 0.0f : fonts_.regular.measure(extra, 22.0f) + 30.0f;
    const float room = bar.w - 56.0f - 30.0f - extra_w;
    const float help_size = fonts_.regular.measure(help, layout::kHelpSize) <= room ? layout::kHelpSize : layout::kHelpSmallSize;
    hui::ui::text(list, fonts_.regular, shorten(fonts_.regular, help, help_size, room), bar.x + 56.0f, bar.cy() + 8.0f, help_size, kInk);
    if (!extra.empty())
        hui::ui::text(list, fonts_.regular, extra, bar.x + bar.w - 30.0f, bar.cy() + 8.0f, 22.0f, kMuted, Align::right);
    list.pop_transform();
    list.pop_opacity();
}

// ---- drawing: the diagnostics page ------------------------------------------------------------

void Screen::draw_diagnostics(hui::gfx::DrawList &list, std::uint32_t glass) const
{
    float a = appear(0);
    list.push_opacity(a);
    list.push_transform(1.0f, 0.0f, 0.0f, static_cast<float>(page_direction_) * 40.0f * (1.0f - a), 0.0f);
    const float tw = hui::ui::text(list, fonts_.display, "Diagnostics", 90.0f, 250.0f, 72.0f, kInk);
    hui::ui::text(list, fonts_.regular, "For a bug report. No addresses, device names, ids or game titles in it.", 120.0f + tw, 248.0f, 24.0f,
                  kMuted);
    list.pop_transform();
    list.pop_opacity();

    a = appear(1);
    list.push_opacity(a);
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, 30.0f * (1.0f - a));
    const Rect panel{96.0f, 286.0f, 1728.0f, 530.0f};
    glass_card(list, glass, panel, 24.0f, 0.0f);
    const int count = static_cast<int>(report_lines_.size());
    if (count == 0)
        hui::ui::text(list, fonts_.regular, "Collecting...", panel.x + 30.0f, panel.y + 50.0f, 24.0f, kMuted);
    for (int i = 0; i < kReportVisible && report_scroll_ + i < count; ++i)
    {
        const std::string &line = report_lines_[static_cast<std::size_t>(report_scroll_ + i)];
        hui::ui::text(list, fonts_.mono, shorten(fonts_.mono, line, 19.0f, panel.w - 80.0f), panel.x + 30.0f,
                      panel.y + 34.0f + static_cast<float>(i) * kReportLine, 19.0f, kInk);
    }
    if (count > kReportVisible)
    {
        // Where the visible part sits in the report.
        const float track = panel.h - 40.0f;
        const float size = std::max(30.0f, track * static_cast<float>(kReportVisible) / static_cast<float>(count));
        const float at = (track - size) * static_cast<float>(report_scroll_) / static_cast<float>(count - kReportVisible);
        list.rounded_rect({panel.x + panel.w - 18.0f, panel.y + 20.0f, 6.0f, track}, 3.0f, Color::rgb(0xffffff, 0.08f));
        list.rounded_rect({panel.x + panel.w - 18.0f, panel.y + 20.0f + at, 6.0f, size}, 3.0f, kAccent);
    }

    const bool enabled = !busy();
    draw_button(list, glass, focus_rect(), static_cast<int>(Icon::restart), "Collect again", false, report_amount_, enabled);
    // Where to get it from: another device on the network, or the console's file system.
    const float x = 470.0f;
    std::string web;
    if (snap_.report_served && !snap_.address.empty())
        web = "On your phone or PC, open http://" + snap_.address + ":" + std::to_string(kReportPort) + " and copy the text.";
    else if (snap_.polled && !snap_.running)
        web = "Start the host to open this from a phone or PC.";
    else
        web = "The network copy is not available.";
    hui::ui::text(list, fonts_.semibold, shorten(fonts_.semibold, web, 25.0f, 1354.0f), x, 866.0f, 25.0f, kInk);
    hui::ui::text(list, fonts_.regular, std::string("Or fetch the file ") + kReportPath + " over FTP.", x, 900.0f, 22.0f, kMuted);
    list.pop_transform();
    list.pop_opacity();
}

void Screen::draw_page(hui::gfx::DrawList &list, std::uint32_t glass) const
{
    draw_top_bar(list, glass);
    if (page_ == Page::home)
        draw_home(list, glass);
    else if (page_ == Page::settings)
        draw_settings(list, glass);
    else
        draw_diagnostics(list, glass);

    // The focus ring glides between the focused parts.
    if (sheet_ == Sheet::none)
    {
        const Rect h = highlight_.rect(time_);
        const bool pill = page_ == Page::home ? home_row_ == 0 : page_ == Page::diagnostics || tile_ >= settings_tiles();
        const float radius = pill ? h.h / 2.0f + 6.0f : 30.0f;
        const float breathe = 0.75f + 0.25f * std::sin(time_ * 3.0f);
        list.bordered_rect(h.inset(-6.0f), radius, kClear, 3.0f, kAccent.with_alpha(0.95f * breathe));
    }
    if (!has_sheet())
        draw_footer(list, glass);
}

} // namespace psapp
