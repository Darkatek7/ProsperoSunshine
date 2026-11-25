// SPDX-License-Identifier: GPL-3.0-only
// The screen's sheets: opening and closing them (an option's editor, the PIN, the device and stream lists, the web
// page's) and their input. They are drawn in screen_sheets_draw.cpp.
#include "app/psapp/screen.hpp"

#include "app/psapp/screen_internal.hpp"
#include "app/psapp/web.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace psapp
{

void Screen::open_tile(int index)
{
    const OptionSpec &spec = option_specs()[static_cast<std::size_t>(index)];
    target_ = spec.key;
    const std::optional<std::string> &value = edits_[spec.key];
    switch (spec.kind)
    {
    case OptionKind::text:
        open_sheet(Sheet::text, spec.label, std::string(spec.help) + " Leave it empty for the default.",
                   value ? *value : std::string());
        break;
    case OptionKind::choice:
        choice_ = 0;
        for (std::size_t i = 0; value && i < spec.choices.size(); ++i)
        {
            if (*value == spec.choices[i].value)
                choice_ = static_cast<int>(i) + 1;
        }
        open_sheet(Sheet::choice, spec.label, spec.help, "");
        break;
    case OptionKind::number:
        number_default_ = !value;
        number_ = value ? std::strtol(value->c_str(), nullptr, 10) : spec.fallback;
        open_sheet(Sheet::number, spec.label, spec.help, "");
        break;
    case OptionKind::toggle:
        break; // flips on the tile
    }
}

void Screen::open_sheet(Sheet kind, const std::string &title, const std::string &help, const std::string &initial)
{
    web_step_ = WebStep::none; // the web page's steps set it again after this
    show_password_ = false;
    sheet_ = kind;
    sheet_drawn_ = kind;
    sheet_title_ = title;
    sheet_help_ = help;
    buffer_ = initial;
    sheet_error_.clear();
    if (kind == Sheet::text || kind == Sheet::pin)
    {
        if (kind == Sheet::text)
        {
            keyboard_.set_layouts({hui::ui::KeyboardLayout::letters(), hui::ui::KeyboardLayout::symbols()});
            keyboard_.style.max_length = 63;
            keyboard_.set_bounds({460.0f, 520.0f, 1000.0f, 380.0f});
        }
        else
        {
            keyboard_.set_layouts({hui::ui::KeyboardLayout::numeric()});
            keyboard_.style.max_length = 4;
            keyboard_.set_bounds({735.0f, 520.0f, 450.0f, 380.0f});
        }
        keyboard_.set_length(static_cast<int>(buffer_.size()));
        keyboard_.set_focus(0, 0);
        keyboard_.enter();
    }
    if (kind == Sheet::choice)
    {
        const Rect row{660.0f, 400.0f + static_cast<float>(choice_) * 76.0f, 600.0f, 64.0f};
        sheet_highlight_.snap(row);
    }
}

void Screen::close_sheet()
{
    sheet_ = Sheet::none;
}

void Screen::open_devices()
{
    renaming_ = false;
    target_.clear();
    // Which profiles a device plays as is set on the web page only, for now (src/control/device_profiles.hpp).
    open_sheet(Sheet::devices, "Paired devices", "Rename a device, or take its access away. Its profiles are set on the web page.", "");
}

void Screen::open_streams()
{
    renaming_ = false;
    target_.clear();
    open_sheet(Sheet::streams, "Streaming now", "End a device's stream. The game keeps running.", "");
}

void Screen::open_web()
{
    renaming_ = false;
    pair_pin_.clear();
    target_.clear();
    web_user_.clear();
    open_sheet(Sheet::web, "Web page", "Settings, devices and streams from a browser on your network.", "");
    web_row_ = 0;
    while (web_row_ < kWebRows - 1 && !web_row_enabled(static_cast<WebRow>(web_row_)))
        ++web_row_;
    sheet_highlight_.snap(web_row_rect(web_row_));
}

bool Screen::web_row_enabled(WebRow row) const
{
    // The host keeps the login: without a running host that knows WEBLOGIN there is nothing to change.
    const bool host = snap_.polled && snap_.running && snap_.web_known;
    switch (row)
    {
    case WebRow::toggle:
        // Turn off while on; Turn on needs a stored login and a host that keeps one while off.
        return host && (snap_.web_enabled || (snap_.web_switchable && !snap_.web_user.empty()));
    case WebRow::set_login:
        return host;
    case WebRow::forget:
        return host && snap_.web_switchable && !snap_.web_user.empty();
    case WebRow::close:
        return true;
    }
    return false;
}

std::string Screen::web_address() const
{
    const auto base = snap_.config.find("port");
    const std::uint16_t port = web::port(base != snap_.config.end() ? base->second : std::nullopt);
    const std::string address = web::address(snap_.address, port);
    return address.empty() ? "Port " + std::to_string(port) + " on the console's IP" : address;
}

void Screen::handle_web(const hui::InputFrame &input, hui::ui::Feedback &feedback)
{
    // The rows that can't do anything now are skipped; the host's state may change under the focus.
    if (!web_row_enabled(static_cast<WebRow>(web_row_)))
    {
        web_row_ = 0;
        while (web_row_ < kWebRows - 1 && !web_row_enabled(static_cast<WebRow>(web_row_)))
            ++web_row_;
    }
    if (input.nav == Direction::up || input.nav == Direction::down)
    {
        const int step = input.nav == Direction::up ? -1 : 1;
        int next = web_row_ + step;
        while (next >= 0 && next < kWebRows && !web_row_enabled(static_cast<WebRow>(next)))
            next += step;
        if (next >= 0 && next < kWebRows)
        {
            web_row_ = next;
            feedback.play(hui::audio::Cue::focus);
        }
        else if (!input.nav_repeat)
        {
            feedback.play(hui::audio::Cue::error, 1.0f, 0.0f, 0.4f);
        }
    }
    sheet_highlight_.target(web_row_rect(web_row_));
    if (input.is_pressed(InputAction::back))
    {
        feedback.play(hui::audio::Cue::modal_close);
        close_sheet();
        return;
    }
    if (!input.is_pressed(InputAction::confirm))
        return;
    const WebRow row = static_cast<WebRow>(web_row_);
    if (row == WebRow::close)
    {
        feedback.play(hui::audio::Cue::modal_close);
        close_sheet();
        return;
    }
    if (busy() || !web_row_enabled(row))
    {
        feedback.play(hui::audio::Cue::error);
        return;
    }
    if (row == WebRow::toggle)
    {
        if (snap_.web_enabled)
            host_.web_login_off();
        else
            host_.web_login_on();
        feedback.play(hui::audio::Cue::toggle);
        return;
    }
    if (row == WebRow::forget)
    {
        open_sheet(Sheet::forget_web, "Forget the login?",
                   "The web page turns off. Turning it on again needs a new login.", "");
        feedback.play(hui::audio::Cue::modal_open);
        return;
    }
    open_sheet(Sheet::text, "Web page user", "1 to 32 letters, digits, '.', '_' or '-'. You log in with it.", snap_.web_user);
    web_step_ = WebStep::user;
    keyboard_.style.max_length = static_cast<int>(web::kUserMax);
    feedback.play(hui::audio::Cue::select);
}

void Screen::handle_sheet(const hui::InputFrame &input, hui::ui::Feedback &feedback)
{
    if (sheet_ == Sheet::web)
    {
        handle_web(input, feedback);
        return;
    }
    if (sheet_ == Sheet::devices)
    {
        const int count = static_cast<int>(snap_.devices.size());
        device_row_ = std::clamp(device_row_, 0, std::max(0, count - 1));
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int next = device_row_ + (input.nav == Direction::up ? -1 : 1);
            if (next >= 0 && next < count)
            {
                device_row_ = next;
                feedback.play(hui::audio::Cue::focus);
            }
            else if (!input.nav_repeat)
            {
                feedback.play(hui::audio::Cue::error, 1.0f, 0.0f, 0.4f);
            }
        }
        device_top_ = std::clamp(device_top_, std::max(0, device_row_ - kDeviceRows + 1), device_row_);
        device_top_ = std::clamp(device_top_, 0, std::max(0, count - kDeviceRows));
        sheet_highlight_.target(device_row_rect(device_row_ - device_top_));
        if (input.is_pressed(InputAction::back))
        {
            feedback.play(hui::audio::Cue::modal_close);
            close_sheet();
        }
        else if (count > 0 && (input.is_pressed(InputAction::confirm) || input.is_pressed(InputAction::west)))
        {
            if (busy())
            {
                feedback.play(hui::audio::Cue::error);
                return;
            }
            const diag::Device &device = snap_.devices[static_cast<std::size_t>(device_row_)];
            target_ = device.id;
            target_name_ = diag::device_label(device);
            if (input.is_pressed(InputAction::west))
            {
                open_sheet(Sheet::remove_device, "Remove " + target_name_ + "?",
                           device.streaming ? "Its stream ends now, and it has to pair again to come back."
                                            : "It has to pair again before it can stream here.",
                           "");
                feedback.play(hui::audio::Cue::modal_open);
            }
            else
            {
                renaming_ = true;
                pair_suggestion_ = device.name;
                open_sheet(Sheet::text, "Rename device", "So you can tell it apart later. Options keeps this name.", device.name);
                keyboard_.style.max_length = 24;
                feedback.play(hui::audio::Cue::select);
            }
        }
        return;
    }
    if (sheet_ == Sheet::streams)
    {
        // The list changes under the focus as streams start and end: it stays in range.
        const int count = static_cast<int>(snap_.streams.size());
        device_row_ = std::clamp(device_row_, 0, std::max(0, count - 1));
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int next = device_row_ + (input.nav == Direction::up ? -1 : 1);
            if (next >= 0 && next < count)
            {
                device_row_ = next;
                feedback.play(hui::audio::Cue::focus);
            }
            else if (!input.nav_repeat)
            {
                feedback.play(hui::audio::Cue::error, 1.0f, 0.0f, 0.4f);
            }
        }
        device_top_ = std::clamp(device_top_, std::max(0, device_row_ - kDeviceRows + 1), device_row_);
        device_top_ = std::clamp(device_top_, 0, std::max(0, count - kDeviceRows));
        sheet_highlight_.target(device_row_rect(device_row_ - device_top_));
        if (input.is_pressed(InputAction::back))
        {
            feedback.play(hui::audio::Cue::modal_close);
            close_sheet();
        }
        else if (count > 0 && input.is_pressed(InputAction::confirm))
        {
            const diag::Stream &stream = snap_.streams[static_cast<std::size_t>(device_row_)];
            if (busy() || stream.id.empty())
            {
                feedback.play(hui::audio::Cue::error);
                return;
            }
            target_ = stream.id;
            target_name_ = diag::stream_label(stream);
            open_sheet(Sheet::end_stream, "End the stream of " + target_name_ + "?",
                       count > 1 ? "The game and the other streams go on. It can connect again from Moonlight."
                                 : "The game stays open. It can connect again from Moonlight.",
                       "");
            feedback.play(hui::audio::Cue::modal_open);
        }
        return;
    }
    if (sheet_ == Sheet::end_stream)
    {
        if (input.is_pressed(InputAction::confirm))
        {
            host_.end_stream(target_, target_name_);
            feedback.play(hui::audio::Cue::select);
            open_streams();
        }
        else if (input.is_pressed(InputAction::back))
        {
            feedback.play(hui::audio::Cue::modal_close);
            open_streams();
        }
        return;
    }
    if (sheet_ == Sheet::deny_pairing)
    {
        if (input.is_pressed(InputAction::confirm))
        {
            if (busy())
            {
                feedback.play(hui::audio::Cue::error);
                return;
            }
            host_.deny_pairing(target_, target_name_);
            feedback.play(hui::audio::Cue::select);
            close_sheet();
        }
        else if (input.is_pressed(InputAction::back))
        {
            feedback.play(hui::audio::Cue::modal_close);
            close_sheet();
        }
        return;
    }
    if (sheet_ == Sheet::forget_web)
    {
        if (input.is_pressed(InputAction::confirm))
        {
            if (busy())
            {
                feedback.play(hui::audio::Cue::error);
                return;
            }
            host_.web_login_forget();
            feedback.play(hui::audio::Cue::select);
            open_web();
        }
        else if (input.is_pressed(InputAction::back))
        {
            feedback.play(hui::audio::Cue::modal_close);
            open_web();
        }
        return;
    }
    if (sheet_ == Sheet::remove_device)
    {
        if (input.is_pressed(InputAction::confirm))
        {
            host_.remove_device(target_, target_name_);
            feedback.play(hui::audio::Cue::select);
            open_devices();
        }
        else if (input.is_pressed(InputAction::back))
        {
            feedback.play(hui::audio::Cue::modal_close);
            open_devices();
        }
        return;
    }
    const OptionSpec *spec = find_option(target_);
    if (sheet_ == Sheet::choice && spec != nullptr)
    {
        const int count = static_cast<int>(spec->choices.size()) + 1;
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int next = choice_ + (input.nav == Direction::up ? -1 : 1);
            if (next >= 0 && next < count)
            {
                choice_ = next;
                feedback.play(hui::audio::Cue::focus);
            }
            else if (!input.nav_repeat)
            {
                feedback.play(hui::audio::Cue::error, 1.0f, 0.0f, 0.4f);
            }
        }
        sheet_highlight_.target({660.0f, 400.0f + static_cast<float>(choice_) * 76.0f, 600.0f, 64.0f});
        if (input.is_pressed(InputAction::confirm))
        {
            edits_[spec->key] = choice_ == 0 ? std::nullopt
                                             : std::optional<std::string>(
                                                   spec->choices[static_cast<std::size_t>(choice_ - 1)].value);
            feedback.play(hui::audio::Cue::select);
            close_sheet();
        }
        else if (input.is_pressed(InputAction::back))
        {
            feedback.play(hui::audio::Cue::modal_close);
            close_sheet();
        }
        return;
    }
    if (sheet_ == Sheet::number && spec != nullptr)
    {
        const NumberSteps steps = number_steps(*spec);
        long step = 0;
        if (input.nav == Direction::left || input.nav == Direction::right)
            step = steps.small * (input.nav == Direction::left ? -1 : 1);
        if (input.nav == Direction::up || input.nav == Direction::down)
            step = steps.medium * (input.nav == Direction::down ? -1 : 1);
        if (input.is_pressed(InputAction::page_prev) || input.is_pressed(InputAction::page_next))
            step = steps.large * (input.is_pressed(InputAction::page_prev) ? -1 : 1);
        if (step != 0)
        {
            const long next = std::clamp(number_default_ ? spec->fallback + step : number_ + step, spec->min, spec->max);
            number_default_ = false;
            if (next == number_ && !input.nav_repeat)
                feedback.play(hui::audio::Cue::error, 1.0f, 0.0f, 0.4f);
            else if (next != number_)
                feedback.play(hui::audio::Cue::slider);
            number_ = next;
        }
        if (input.is_pressed(InputAction::west))
        {
            number_default_ = true;
            number_ = spec->fallback;
            feedback.play(hui::audio::Cue::toggle);
        }
        if (input.is_pressed(InputAction::confirm))
        {
            edits_[spec->key] = number_default_ ? std::nullopt : std::optional<std::string>(std::to_string(number_));
            feedback.play(hui::audio::Cue::select);
            close_sheet();
        }
        else if (input.is_pressed(InputAction::back))
        {
            feedback.play(hui::audio::Cue::modal_close);
            close_sheet();
        }
        return;
    }

    // The keyboard: the PIN and the host name.
    if (web_step_ == WebStep::password && input.is_pressed(InputAction::r3))
    {
        show_password_ = !show_password_;
        feedback.play(hui::audio::Cue::toggle);
    }
    const hui::ui::Event event = keyboard_.handle(input, feedback);
    for (int i = 0; i < keyboard_.erased() && !buffer_.empty(); ++i)
        buffer_.pop_back();
    buffer_ += keyboard_.typed();
    keyboard_.set_length(static_cast<int>(buffer_.size()));
    if (!keyboard_.typed().empty() || keyboard_.erased() > 0)
        sheet_error_.clear();
    if (event == hui::ui::Event::cancelled)
    {
        pair_pin_.clear();
        feedback.play(hui::audio::Cue::modal_close);
        if (renaming_)
            open_devices();
        else if (web_step_ != WebStep::none)
            open_web(); // drops a typed password with the buffer
        else
            close_sheet();
        return;
    }
    if (event != hui::ui::Event::activated && !input.is_pressed(InputAction::menu))
        return;
    if (sheet_ == Sheet::pin)
    {
        if (buffer_.size() != 4)
        {
            sheet_error_ = "The PIN has four digits";
            feedback.play(hui::audio::Cue::error);
            return;
        }
        // Moonlight doesn't say which device it is, so the name is asked here, as Sunshine's web page does.
        pair_pin_ = buffer_;
        pair_suggestion_ = diag::suggested_device_name(
            target_name_, diag::paired_names(snap_.running && snap_.devices_known, snap_.devices, snap_.paired));
        open_sheet(Sheet::text, "Name this device", "So you can tell it apart later. Options keeps this name.", pair_suggestion_);
        keyboard_.style.max_length = 24;
        feedback.play(hui::audio::Cue::select);
        return;
    }
    if (web_step_ == WebStep::user)
    {
        const std::string problem = web::user_problem(buffer_);
        if (!problem.empty())
        {
            sheet_error_ = problem;
            feedback.play(hui::audio::Cue::error);
            return;
        }
        // The password next; the name stays for it.
        const std::string user = buffer_;
        open_sheet(Sheet::text, "Password for " + user, "8 to 128 characters, spaces count. It goes only to the host.", "");
        web_step_ = WebStep::password;
        web_user_ = user;
        keyboard_.style.max_length = static_cast<int>(web::kPasswordMax);
        feedback.play(hui::audio::Cue::select);
        return;
    }
    if (web_step_ == WebStep::password)
    {
        const std::string problem = web::password_problem(buffer_);
        if (!problem.empty())
        {
            sheet_error_ = problem;
            feedback.play(hui::audio::Cue::error);
            return;
        }
        if (busy())
        {
            sheet_error_ = "One moment: the host is busy with something else";
            feedback.play(hui::audio::Cue::error);
            return;
        }
        host_.set_web_login(web_user_, buffer_);
        std::fill(buffer_.begin(), buffer_.end(), '\0');
        buffer_.clear();
        feedback.play(hui::audio::Cue::select);
        open_web(); // the outcome shows there
        return;
    }
    if (!pair_pin_.empty())
    {
        const std::string name = diag::clean_device_name(buffer_);
        host_.submit_pin(target_, pair_pin_, name.empty() ? pair_suggestion_ : name);
        pair_pin_.clear();
        close_sheet();
        return;
    }
    if (renaming_)
    {
        const std::string name = diag::clean_device_name(buffer_);
        if (!name.empty() && name != pair_suggestion_)
            host_.rename_device(target_, name);
        feedback.play(hui::audio::Cue::select);
        open_devices();
        return;
    }
    if (spec == nullptr)
    {
        close_sheet();
        return;
    }
    if (buffer_.empty())
    {
        edits_[spec->key] = std::nullopt; // empty: the host's default
        close_sheet();
        return;
    }
    if (const auto problem = validate(*spec, buffer_))
    {
        sheet_error_ = std::string(spec->label) + " " + *problem;
        feedback.play(hui::audio::Cue::error);
        return;
    }
    edits_[spec->key] = buffer_;
    close_sheet();
}

} // namespace psapp
