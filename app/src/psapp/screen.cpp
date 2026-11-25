// SPDX-License-Identifier: GPL-3.0-only
// The screen's state, geometry, update and the pages' input; the sheets are in screen_sheets.cpp, the drawing in
// screen_draw.cpp and screen_sheets_draw.cpp.
#include "app/psapp/screen.hpp"

#include "app/psapp/experimental_list.h"
#include "app/psapp/screen_internal.hpp"
#include "app/psapp/settings_layout.hpp"
#include "platform/ps5/system.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace psapp
{

namespace
{

float approach(float value, float target, float dt, float speed)
{
    return value + (target - value) * (1.0f - std::exp(-speed * dt));
}

} // namespace

Screen::Screen(Host &host, const hui::ui::Fonts &fonts)
    : host_(host), fonts_(fonts), tile_amount_(static_cast<std::size_t>(layout::page().items()), 0.0f)
{
    style_.theme.focus = kAccent.with_alpha(0.3f);
    style_.theme.primary = kAccent;
    keyboard_.style.theme = style_.theme;
    keyboard_.style.theme.surface = Color::rgb(0x1a2244, 0.9f);
    keyboard_.style.theme.surface_high = Color::rgb(0x232d55);
    keyboard_.style.theme.text = kInk;
    keyboard_.style.theme.primary = kAccent;
    keyboard_.style.theme.on_primary = Color::rgb(0x2a1604);
    keyboard_.style.panel = false;
    keyboard_.style.surfaces = false; // flat caps: frosted keys pick up the sun behind the sheet
    // The kit's standard set (Square deletes, Triangle is space, L2 shift) with R2 as Done, as on the console's own
    // keyboard; the symbols move to R1, which no keyboard sheet uses. Options stays Done too (handle_sheet).
    keyboard_.style.bindings = hui::ui::KeyboardBindings::standard();
    keyboard_.style.bindings.layout = InputAction::page_next;
    keyboard_.style.bindings.done = InputAction::jump_next;
}

// ---- state helpers ----------------------------------------------------------------------------

bool Screen::dirty() const
{
    return edits_loaded_ && edits_ != loaded_;
}

std::string Screen::value_text(int index) const
{
    const OptionSpec &spec = option_specs()[static_cast<std::size_t>(index)];
    if (switch_not_needed(index))
        return diag::experimental_no_effect(snap_.firmware, spec.key) ? ps_exp_switch_at(experimental_switch(spec))->no_effect_word
                                                                      : "Verified here"; // the narrow tiles leave no room for more
    const auto it = edits_.find(spec.key);
    if (it == edits_.end() || !it->second)
        return spec.default_label;
    const std::string &value = *it->second;
    if (spec.kind == OptionKind::choice)
    {
        for (const Choice &choice : spec.choices)
        {
            if (value == choice.value)
                return choice.label;
        }
    }
    if (spec.kind == OptionKind::number && *spec.unit != '\0')
        return value + " " + spec.unit;
    if (spec.kind == OptionKind::toggle)
        return value == "1" ? "On" : "Off";
    return value;
}

bool Screen::switch_not_needed(int index) const
{
    if (index < 0 || index >= settings_tiles())
        return false;
    const OptionSpec &spec = option_specs()[static_cast<std::size_t>(index)];
    return experimental_switch(spec) >= 0 && !diag::experimental_needed(snap_.firmware, spec.key);
}

int Screen::visible_pairings() const
{
    // While someone streams, the streams card takes the second card's place.
    return static_cast<int>(std::min<std::size_t>(snap_.pairings.size(), streams_shown() ? 1 : 2));
}

int Screen::list_rows() const
{
    const bool streams = sheet_drawn_ == Sheet::streams || sheet_drawn_ == Sheet::end_stream;
    return static_cast<int>(streams ? snap_.streams.size() : snap_.devices.size());
}

float Screen::appear(int order) const
{
    return ease_out((page_age_ - 0.045f * static_cast<float>(order)) / 0.42f);
}

Rect Screen::action_rect(int index) const
{
    static const float widths[3] = {318.0f, 318.0f, 400.0f};
    float x = 96.0f;
    for (int i = 0; i < index; ++i)
        x += widths[i] + 20.0f;
    return {x, 532.0f, widths[index], 88.0f};
}

Rect Screen::pairing_rect(int index) const
{
    return {96.0f + static_cast<float>(index) * 562.0f, 740.0f, 538.0f, 196.0f};
}

Rect Screen::streams_rect() const
{
    // Beside a pairing card, or in the empty pairing card's place.
    const int cards = visible_pairings();
    return cards == 0 ? Rect{96.0f, 740.0f, 1100.0f, 172.0f} : Rect{96.0f + static_cast<float>(cards) * 562.0f, 740.0f, 538.0f, 172.0f};
}

Rect Screen::devices_card() const
{
    const int rows = std::clamp(list_rows(), 1, kDeviceRows);
    const float height = 262.0f + static_cast<float>(rows) * 90.0f;
    return {960.0f - 520.0f, 540.0f - height / 2.0f + 10.0f, 1040.0f, height};
}

Rect Screen::device_row_rect(int visible) const
{
    const Rect card = devices_card();
    return {card.x + 50.0f, card.y + 216.0f + static_cast<float>(visible) * 90.0f, card.w - 100.0f, 80.0f};
}

Rect Screen::tile_rect(int index) const
{
    return rect_of(layout::page().tiles[static_cast<std::size_t>(index)]);
}

Rect Screen::settings_action_rect(int index) const
{
    return rect_of(layout::page().actions[static_cast<std::size_t>(index)]);
}

Rect Screen::web_row_rect(int index) const
{
    const float top = 540.0f - kWebSheetHeight / 2.0f + 10.0f;
    return {660.0f, top + kWebRowsTop + static_cast<float>(index) * kWebRowStep, 600.0f, 60.0f};
}

Rect Screen::focus_rect() const
{
    if (page_ == Page::home)
        return home_row_ == 0                      ? action_rect(home_action_)
               : home_card_ < visible_pairings()   ? pairing_rect(home_card_)
               : home_card_ < devices_index()      ? streams_rect()
                                                   : kPairedDevices;
    if (page_ == Page::diagnostics)
        return {96.0f, 836.0f, 340.0f, 76.0f};
    return tile_ < settings_tiles() ? tile_rect(tile_) : settings_action_rect(tile_ - settings_tiles());
}

// ---- update and input -------------------------------------------------------------------------

void Screen::update(const hui::InputFrame &input, float dt, hui::ui::Feedback &feedback)
{
    time_ += dt;
    message_age_ += dt;
    armed_age_ += dt;
    page_age_ += dt;
    snap_ = host_.snapshot();
    if (snap_.message_serial != seen_message_)
    {
        seen_message_ = snap_.message_serial;
        message_age_ = 0.0f;
        feedback.play(snap_.message_ok ? hui::audio::Cue::complete : hui::audio::Cue::error);
        // After a save or a reload the page shows the files again; other outcomes keep unsaved edits.
        if (reload_edits_)
            edits_loaded_ = false;
        reload_edits_ = false;
    }
    if (snap_.report_serial != seen_report_)
    {
        seen_report_ = snap_.report_serial;
        report_lines_.clear();
        const std::string text = host_.report();
        for (std::size_t at = 0; at < text.size();)
        {
            const std::size_t end = std::min(text.find('\n', at), text.size());
            report_lines_.push_back(text.substr(at, end - at));
            at = end + 1;
        }
        report_scroll_ = 0;
    }
    if (!edits_loaded_ && snap_.files_read && snap_.busy.empty())
    {
        edits_ = loaded_ = snap_.config;
        edits_loaded_ = true;
    }
    // The file was read again and changed: after Start on a title without /data (read through the host only now), or after
    // the web page saved and something here read the file. The settings not edited here take the file's values; the edits
    // stay, and Save still writes only those. A failed read (without /data while the host is down) changes nothing.
    if (edits_loaded_ && snap_.busy.empty() && snap_.config_read && snap_.config != loaded_)
    {
        edits_ = rebased(edits_, loaded_, snap_.config);
        loaded_ = snap_.config;
    }
    // Cards come and go before the paired devices (pairing requests, the streams card): a focus on them stays there.
    if (home_card_ == seen_devices_index_)
        home_card_ = devices_index();
    seen_devices_index_ = devices_index();
    // A new pairing request: a chime, and on the home page the focus goes to its card.
    if (snap_.pairings.size() > seen_pairings_)
    {
        feedback.play(hui::audio::Cue::notify);
        if (page_ == Page::home && sheet_ == Sheet::none)
        {
            home_row_ = 1;
            home_card_ = 0;
        }
    }
    seen_pairings_ = snap_.pairings.size();
    home_card_ = std::clamp(home_card_, 0, devices_index());

    // A button that is down while the app starts (the press that started it, or a pad that reports
    // one held, as the virtual pad of a host session without its client does) is ignored until it
    // is let go, so it never acts on the focused Stop host. Every other button works at once.
    if (time_ < 0.5f)
        blocked_ |= input.held;
    else
        blocked_ &= input.held;
    if (blocked_ != 0 && !blocked_logged_ && time_ > 2.0f)
    {
        hui::sys::log("[PSAPP] ignoring buttons held since the start: 0x%x", static_cast<unsigned>(blocked_));
        blocked_logged_ = true;
    }
    hui::InputFrame in = input;
    in.pressed &= ~blocked_;
    const InputAction directions[] = {InputAction::up, InputAction::down, InputAction::left, InputAction::right};
    const Direction navs[] = {Direction::up, Direction::down, Direction::left, Direction::right};
    for (int i = 0; i < 4; ++i)
    {
        if (in.nav == navs[i] && (blocked_ & hui::action_bit(directions[i])) != 0)
            in.nav = Direction::none;
    }
    if (time_ >= 0.5f)
    {
        if (sheet_ != Sheet::none)
            handle_sheet(in, feedback);
        else if (in.is_pressed(InputAction::page_prev) || in.is_pressed(InputAction::page_next))
        {
            const int next = static_cast<int>(page_) + (in.is_pressed(InputAction::page_prev) ? -1 : 1);
            switch_page(static_cast<Page>(std::clamp(next, 0, static_cast<int>(Page::diagnostics))), feedback);
        }
        else if (page_ == Page::home)
            handle_home(in, feedback);
        else if (page_ == Page::settings)
            handle_settings(in, feedback);
        else
            handle_diagnostics(in, feedback);
    }

    // Motion.
    const Rect target = focus_rect();
    if (snap_highlight_)
    {
        highlight_.snap(target);
        snap_highlight_ = false;
    }
    highlight_.target(target);
    highlight_.update(dt, style_);
    for (int i = 0; i < 3; ++i)
        action_amount_[i] = approach(action_amount_[i], page_ == Page::home && home_row_ == 0 && home_action_ == i ? 1.0f : 0.0f, dt, 14.0f);
    for (int i = 0; i < 2; ++i)
        card_amount_[i] = approach(card_amount_[i], page_ == Page::home && home_row_ == 1 && home_card_ == i ? 1.0f : 0.0f, dt, 14.0f);
    devices_amount_ = approach(devices_amount_, page_ == Page::home && home_row_ == 1 && home_card_ == devices_index() ? 1.0f : 0.0f, dt, 14.0f);
    streams_amount_ = approach(streams_amount_,
                               page_ == Page::home && home_row_ == 1 && streams_shown() && home_card_ == visible_pairings() ? 1.0f : 0.0f,
                               dt, 14.0f);
    for (std::size_t i = 0; i < tile_amount_.size(); ++i)
        tile_amount_[i] = approach(tile_amount_[i], page_ == Page::settings && tile_ == static_cast<int>(i) ? 1.0f : 0.0f, dt, 14.0f);
    report_amount_ = approach(report_amount_, page_ == Page::diagnostics ? 1.0f : 0.0f, dt, 14.0f);
    const float tab_target = static_cast<float>(static_cast<int>(page_));
    tab_x_ = tab_x_ < 0.0f ? tab_target : approach(tab_x_, tab_target, dt, 16.0f);
    sheet_amount_ = approach(sheet_amount_, sheet_ == Sheet::none ? 0.0f : 1.0f, dt, 16.0f);
    if (sheet_ == Sheet::none && sheet_amount_ < 0.01f)
        sheet_drawn_ = Sheet::none;

    const bool running = snap_.polled && snap_.running;
    const float spin_speed = live() ? 0.55f : running ? 0.12f : 0.0f;
    sun_spin_ += spin_speed * dt;
    sun_glow_ = approach(sun_glow_, live() ? 1.0f : running ? 0.7f : 0.15f, dt, 3.0f);
    sun_dim_ = approach(sun_dim_, running ? 0.0f : 0.85f, dt, 3.0f);
    if (live())
        pulse_ += dt;
    keyboard_.update(dt);
    if (sheet_ == Sheet::choice || sheet_ == Sheet::devices || sheet_ == Sheet::streams || sheet_ == Sheet::web)
    {
        sheet_highlight_.update(dt, style_);
    }
}

void Screen::switch_page(Page page, hui::ui::Feedback &feedback)
{
    if (page == page_)
    {
        feedback.play(hui::audio::Cue::error, 1.0f, 0.0f, 0.4f);
        return;
    }
    page_direction_ = static_cast<int>(page) > static_cast<int>(page_) ? 1 : -1;
    page_ = page;
    // The report is made when its page opens, and only then does the console offer it on the network.
    host_.serve_report(page == Page::diagnostics);
    if (page == Page::diagnostics)
        host_.make_report();
    page_age_ = 0.0f;
    snap_highlight_ = true;
    armed_ = -1;
    feedback.play(hui::audio::Cue::tab);
}

void Screen::handle_home(const hui::InputFrame &input, hui::ui::Feedback &feedback)
{
    const int cards = visible_pairings();
    switch (input.nav)
    {
    case Direction::up:
    case Direction::down: {
        const int row = input.nav == Direction::up ? 0 : 1;
        if (row == home_row_)
        {
            if (!input.nav_repeat)
                feedback.play(hui::audio::Cue::error, 1.0f, 0.0f, 0.4f);
            break;
        }
        home_row_ = row;
        feedback.play(hui::audio::Cue::focus);
        break;
    }
    case Direction::left:
    case Direction::right: {
        const int step = input.nav == Direction::left ? -1 : 1;
        int &index = home_row_ == 0 ? home_action_ : home_card_;
        const int count = home_row_ == 0 ? 3 : devices_index() + 1;
        if (index + step < 0 || index + step >= count)
        {
            if (!input.nav_repeat)
                feedback.play(hui::audio::Cue::error, 1.0f, 0.0f, 0.4f);
            break;
        }
        index += step;
        feedback.play(hui::audio::Cue::focus);
        break;
    }
    default:
        break;
    }
    if (input.is_pressed(InputAction::confirm))
    {
        if (home_row_ == 0)
        {
            run(static_cast<Action>(home_action_), feedback);
        }
        else if (home_card_ < cards)
        {
            const Pairing &p = snap_.pairings[static_cast<std::size_t>(home_card_)];
            target_ = p.id;
            target_name_ = p.name;
            pair_pin_.clear();
            open_sheet(Sheet::pin, "Pair " + diag::pairing_label(p.name), "Type the four digits Moonlight shows.", "");
            feedback.play(hui::audio::Cue::modal_open);
        }
        else
        {
            device_row_ = 0;
            device_top_ = 0;
            if (home_card_ < devices_index())
                open_streams();
            else
                open_devices();
            sheet_highlight_.snap(device_row_rect(0));
            feedback.play(hui::audio::Cue::modal_open);
        }
    }
    else if (input.is_pressed(InputAction::west) && home_row_ == 1 && home_card_ < cards)
    {
        // Square denies a request, after asking, as Square removes a paired device.
        if (busy())
        {
            feedback.play(hui::audio::Cue::error);
            return;
        }
        const Pairing &p = snap_.pairings[static_cast<std::size_t>(home_card_)];
        target_ = p.id;
        target_name_ = diag::pairing_label(p.name);
        pair_pin_.clear();
        open_sheet(Sheet::deny_pairing, "Deny " + target_name_ + "?",
                   "Moonlight is told no at once. It can ask again later.", "");
        feedback.play(hui::audio::Cue::modal_open);
    }
    if (input.is_pressed(InputAction::north))
    {
        host_.refresh();
        reload_edits_ = true;
        feedback.play(hui::audio::Cue::select);
    }
}

void Screen::handle_settings(const hui::InputFrame &input, hui::ui::Feedback &feedback)
{
    switch (input.nav)
    {
    case Direction::up:
    case Direction::down:
    case Direction::left:
    case Direction::right: {
        // Rows and columns come from the tiles' places (settings_layout.cpp), which the app's tests walk.
        const layout::Move move = input.nav == Direction::up     ? layout::Move::up
                                  : input.nav == Direction::down ? layout::Move::down
                                  : input.nav == Direction::left ? layout::Move::left
                                                                 : layout::Move::right;
        const int next = layout::next(layout::page(), tile_, move);
        if (next < 0)
        {
            if (!input.nav_repeat)
                feedback.play(hui::audio::Cue::error, 1.0f, 0.0f, 0.4f);
            break;
        }
        tile_ = next;
        feedback.play(hui::audio::Cue::focus);
        break;
    }
    default:
        break;
    }
    if (input.is_pressed(InputAction::confirm))
    {
        if (tile_ >= settings_tiles())
        {
            run(static_cast<Action>(static_cast<int>(Action::save) + tile_ - settings_tiles()), feedback);
        }
        else if (switch_not_needed(tile_))
        {
            feedback.play(hui::audio::Cue::error, 1.0f, 0.0f, 0.4f);
        }
        else if (option_specs()[static_cast<std::size_t>(tile_)].kind == OptionKind::toggle)
        {
            std::optional<std::string> &value = edits_[option_specs()[static_cast<std::size_t>(tile_)].key];
            value = value && *value == "1" ? std::nullopt : std::optional<std::string>("1");
            feedback.play(hui::audio::Cue::toggle);
        }
        else
        {
            open_tile(tile_);
            feedback.play(hui::audio::Cue::modal_open);
        }
    }
    if (input.is_pressed(InputAction::west) && tile_ < settings_tiles() && !switch_not_needed(tile_))
    {
        const OptionSpec &spec = option_specs()[static_cast<std::size_t>(tile_)];
        if (edits_[spec.key])
        {
            edits_[spec.key] = std::nullopt;
            feedback.play(hui::audio::Cue::toggle);
        }
    }
    if (input.is_pressed(InputAction::north))
    {
        host_.refresh();
        reload_edits_ = true;
        feedback.play(hui::audio::Cue::select);
    }
}

void Screen::handle_diagnostics(const hui::InputFrame &input, hui::ui::Feedback &feedback)
{
    const int last = std::max(0, static_cast<int>(report_lines_.size()) - kReportVisible);
    int step = 0;
    if (input.nav == Direction::up || input.nav == Direction::down)
        step = input.nav == Direction::up ? -1 : 1;
    if (input.nav == Direction::left || input.nav == Direction::right)
        step = (input.nav == Direction::left ? -1 : 1) * kReportVisible;
    if (step != 0)
    {
        const int next = std::clamp(report_scroll_ + step, 0, last);
        if (next != report_scroll_)
            feedback.play(hui::audio::Cue::focus);
        else if (!input.nav_repeat)
            feedback.play(hui::audio::Cue::error, 1.0f, 0.0f, 0.4f);
        report_scroll_ = next;
    }
    if (input.is_pressed(InputAction::confirm) || input.is_pressed(InputAction::north))
    {
        if (busy())
        {
            feedback.play(hui::audio::Cue::error);
            return;
        }
        host_.make_report();
        feedback.play(hui::audio::Cue::select);
    }
}

void Screen::run(Action action, hui::ui::Feedback &feedback)
{
    if (action == Action::revert)
    {
        edits_ = loaded_; // the file as last read: snap_.config may be from a failed read
        feedback.play(hui::audio::Cue::back);
        return;
    }
    if (action == Action::web)
    {
        open_web();
        feedback.play(hui::audio::Cue::modal_open);
        return;
    }
    if (busy())
    {
        feedback.play(hui::audio::Cue::error);
        return;
    }
    // Stopping the host ends a running stream, and a player streaming this screen may be the one
    // pressing: such a press has to come twice.
    const bool ends_stream = live() && ((action == Action::start_stop && snap_.running) ||
                                                 action == Action::restart || action == Action::save_restart);
    if (ends_stream && (armed_ != static_cast<int>(action) || armed_age_ > kArmSeconds))
    {
        armed_ = static_cast<int>(action);
        armed_age_ = 0.0f;
        feedback.play(hui::audio::Cue::notify);
        return;
    }
    armed_ = -1;
    switch (action)
    {
    case Action::start_stop:
        if (snap_.running)
            host_.stop_host();
        else
            host_.start_host();
        break;
    case Action::restart:
        host_.restart_host();
        break;
    case Action::autoload:
        // Unknown counts as off: a press asks for it on, and without a host to read the list the message says to start one.
        host_.set_autoload(snap_.boot_start != autoload::BootStart::on);
        break;
    case Action::save:
    case Action::save_restart:
        // Only what was changed here: the page's other values may be older than the file (changed_values).
        host_.save_config(changed_values(edits_, loaded_), action == Action::save_restart);
        reload_edits_ = true;
        break;
    case Action::revert:
    case Action::web:
        break;
    }
    feedback.play(hui::audio::Cue::select);
}

} // namespace psapp
