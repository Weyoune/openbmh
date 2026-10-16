// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/league_table_range_dialog.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <string>
#include <utility>

#include "game/messages.hpp"
#include "game/state.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
constexpr uint32_t kTextDBase = ::bmh::layout::TextPointerTable;
constexpr uint32_t kSourceMin = 1;
constexpr uint32_t kSourcePresetStep = 2;
constexpr uint32_t kSourcePresetAdd = 0;
constexpr uint32_t kFlashWait = 4;
constexpr uint32_t kCancelWait = 0x32;
constexpr uint8_t kTransparent = 0xc0;
constexpr uint8_t kTextColor = 0x62;
constexpr uint8_t kValueColor = 0x00;
constexpr std::string_view kKeys = "0123456789DRCJN";

bool inside_inclusive(int x, int y, int left, int top, int right, int bottom) {
    return x >= left && x <= right && y >= top && y <= bottom;
}

uint32_t decimal_digits(uint32_t value) {
    uint32_t count = 0;
    do { ++count; value /= 10; } while (value != 0);
    return count;
}
} // namespace

LeagueTableRangeDialog::LeagueTableRangeDialog(const std::filesystem::path& data_directory,
                                               Image backdrop,
                                               TableRangeDialogProfile profile)
    : LeagueTableRangeDialog(data_directory, std::move(backdrop), profile, false) {}

LeagueTableRangeDialog::LeagueTableRangeDialog(const std::filesystem::path& data_directory,
                                               Image backdrop,
                                               TotoStakeDialogProfile profile)
    : LeagueTableRangeDialog(data_directory, std::move(backdrop),
        TableRangeDialogProfile{::bmh::layout::LeagueTableRangeDialog_Backdrop_ExecutableField, 10000, 400000, 0, profile.text_mode, profile.x, profile.y}, true) {}

LeagueTableRangeDialog::LeagueTableRangeDialog(const std::filesystem::path& data_directory,
                                               Image backdrop,
                                               TableRangeDialogProfile profile, bool toto)
    : backdrop_(std::move(backdrop)),
      calculator_art_(load_vga(asset_path(data_directory / "KB", "6.VGA"))),
      body_(load_font(asset_path(data_directory / "F", "BMH3.FNT"))),
      exe_(load_le(asset_path(data_directory, "BMH.exe"))),
      texts_(load_textd(asset_path(data_directory, "TEXTD.BLK"))),
      profile_(profile), x_(profile.x), y_(profile.y) {
    if ((!toto && profile_.title_textd_address != ::bmh::layout::CareerSession_CurrentLeagueTableDispatch_ExecutableField && profile_.title_textd_address != ::bmh::layout::CareerSession_CurrentLeagueTableDispatch_ExecutableField2) ||
        (toto && (profile_.title_textd_address != ::bmh::layout::LeagueTableRangeDialog_Backdrop_ExecutableField || profile_.minimum != 10000 ||
                  profile_.maximum != 400000 || profile_.default_value != 0)))
        throw FormatError("table range dialog: unsupported Source TEXTD title");
    if (profile_.minimum > profile_.maximum || profile_.maximum > 99999999 || !profile_.text_mode)
        throw FormatError("table range dialog: invalid Source bounds");
    if (backdrop_.width != 320 || backdrop_.height != 240 ||
        calculator_art_.width < 113 || calculator_art_.height < 150 ||
        !exe_.contains(::bmh::layout::Advertising_Click_RuntimeField + 47) || !exe_.contains(::bmh::layout::Advertising_Click_RuntimeField2 + 23))
        throw FormatError("table range dialog: incomplete Source assets or backdrop");

    x_ = std::clamp(x_, 0, 0xcf);
    y_ = std::clamp(y_, 0, 0x68);
    value_ = std::min(profile_.default_value, profile_.maximum); // param7 is separate from presets
    displayed_value_ = value_;
    digit_count_ = value_ == 0 ? 0 : decimal_digits(value_);

    for (size_t i = 0; i < cell_x_.size(); ++i) {
        cell_x_[i] = exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + uint32_t(i) * 2);
        cell_y_[i] = exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + uint32_t(i));
    }

    // BalanceFromGame Table calls pass param4=1,param5=2,param6=0. The current
    // displayed default (param7) does not seed this preset recurrence.
    uint64_t preset = toto ? 20000 : std::max<uint32_t>(kSourceMin, profile_.minimum);
    for (size_t i = 0; i < 6; ++i) {
        if (preset <= profile_.maximum) presets_[i] = uint32_t(preset);
        preset = toto ? preset + 20000 : preset * kSourcePresetStep + kSourcePresetAdd;
    }
    presets_[6] = profile_.minimum;
    presets_[7] = profile_.maximum;
    (void)render();
}

std::string_view LeagueTableRangeDialog::source_text(uint32_t address) const {
    if (address < kTextDBase || ((address - kTextDBase) & 3U) != 0)
        throw FormatError("table range dialog: invalid TEXTD address");
    return texts_[size_t((address - kTextDBase) / 4)];
}

void LeagueTableRangeDialog::copy_region(Image& out, int sx, int sy, int dx, int dy,
                                         int width, int height, uint8_t transparent) const {
    if (sx < 0 || sy < 0 || dx < 0 || dy < 0 || width < 0 || height < 0 ||
        sx + width > calculator_art_.width || sy + height > calculator_art_.height ||
        dx + width > out.width || dy + height > out.height)
        throw FormatError("table range dialog: KB/6.VGA crop outside source or backdrop");
    for (int yy = 0; yy < height; ++yy) {
        for (int xx = 0; xx < width; ++xx) {
            const uint8_t pixel = calculator_art_.pixels[size_t(sy + yy) * size_t(calculator_art_.width) + size_t(sx + xx)];
            if (pixel != transparent) out.at(dx + xx, dy + yy) = pixel;
        }
    }
}

void LeagueTableRangeDialog::fill_region(Image& out, int x, int y, int width, int height,
                                         uint8_t color) const {
    for (int yy = std::max(0, y); yy < std::min(out.height, y + height); ++yy)
        for (int xx = std::max(0, x); xx < std::min(out.width, x + width); ++xx)
            out.at(xx, yy) = color;
}

void LeagueTableRangeDialog::draw_source_text(Image& out, int x, int baseline,
                                               std::string_view text, uint8_t color,
                                               bool allow_gradient) const {
    const int width = body_.text_width(text);
    const int top = baseline - body_.baseline;
    if (width <= 0 || x < 0 || top < 0 || x + width > out.width || top + body_.height > out.height)
        return;
    const bool gradient = allow_gradient && *profile_.text_mode == TableRangeTextMode::source_gradient &&
                          exe_.u8(::bmh::layout::TextGradientMode) == 2;
    if (!gradient) {
        body_.draw(out, x, top, text, color);
        return;
    }
    int ramp = 0;
    while (ramp < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2Unsigned + uint32_t(ramp)) != color) ++ramp;
    if (ramp == 32) {
        body_.draw(out, x, top, text, color);
        return;
    }
    Image mask(std::max(1, width), body_.height);
    body_.draw(mask, 0, 0, text, 1);
    for (int yy = 0; yy < body_.height; ++yy)
        for (int xx = 0; xx < width; ++xx)
            if (mask.at(xx, yy))
                out.at(x + xx, top + yy) = exe_.u8(::bmh::layout::Advertising_Text_RuntimeFieldUnsigned + uint32_t(ramp * 6 + std::min(yy, 5)));
}

void LeagueTableRangeDialog::draw_control(Image& out, int index, bool pressed) const {
    const int x = x_ + cell_x_[size_t(index)];
    const int y = y_ + cell_y_[size_t(index)];
    // StatisticsRankingsFromGame2 restores the saved panel pixels. render() starts with a
    // pristine owned backdrop and redraws KB6, so those pixels are already here.
    if (pressed) {
        copy_region(out, 0x0f, 0x88, x + 1, y + 1, 0x0e, 0x0d, kTransparent);
        draw_source_text(out, x + 6, y + 10, kKeys.substr(size_t(index), 1), kTextColor);
    } else {
        copy_region(out, 0, 0x88, x, y, 0x0f, 0x0e, kTransparent);
        draw_source_text(out, x + 5, y + 9, kKeys.substr(size_t(index), 1), kTextColor);
    }
}

Image LeagueTableRangeDialog::render() const {
    Image out = backdrop_;
    if (awaiting_exit_present_) return out; // Source restored the saved underlay before its exit Present.
    copy_region(out, 0, 0, x_, y_, 0x71, 0x88, kTransparent);
    fill_region(out, x_ + 0x71, y_ + 2, 2, 0x88, 0x40);
    fill_region(out, x_ + 2, y_ + 0x88, 0x71, 2, 0x40);

    const std::string_view title = source_text(profile_.title_textd_address);
    draw_source_text(out, x_ + 8 + (105 - 8) / 2 - body_.text_width(title) / 2,
                     y_ + 14, title, kTextColor, true);

    for (int i = 0; i < 15; ++i) draw_control(out, i, pressed_control_ == i);

    for (size_t i = 0; i < presets_.size(); ++i) {
        if (!presets_[i]) continue; // Source marks above-max presets disabled and does not draw text.
        const int x = x_ + cell_x_[i + 15];
        const int y = y_ + cell_y_[i + 15];
        const std::string label = format_number(GameState{}, int32_t(*presets_[i]), 7);
        draw_source_text(out, x + 47 - body_.text_width(label), y + 12, label, kTextColor, true);
    }

    const int value_x = x_ + cell_x_[23];
    const int value_y = y_ + cell_y_[23];
    const std::string current = format_number(GameState{}, int32_t(displayed_value_), int(displayed_format_));
    draw_source_text(out, value_x + 47 - body_.text_width(current), value_y + 12,
                     current, kValueColor, true);
    return out;
}

bool LeagueTableRangeDialog::inside_cell(int index, int x, int y) const {
    if (index < 0 || index > 22) return false;
    const int left = x_ + cell_x_[size_t(index)];
    const int top = y_ + cell_y_[size_t(index)];
    const int right_offset = index >= 15 ? 0x35 : 0x10;
    return inside_inclusive(x, y, left, top, left + right_offset, top + 0x0e);
}

void LeagueTableRangeDialog::clamp_to_source_max() {
    if (value_ > profile_.maximum) value_ = profile_.maximum;
    if (value_ != 0) digit_count_ = decimal_digits(value_);
}

void LeagueTableRangeDialog::append_digit(unsigned digit) {
    if (digit_count_ >= 10) return;
    value_ = value_ * 10 + digit;
    ++digit_count_;
}

bool LeagueTableRangeDialog::input_ready() const {
    return initial_presented_ && !updated_modal_present_pending_ && !awaiting_exit_present_ &&
           !flash_waiting_for_present_ && !flash_presented_ && source_wait_remaining_ == 0 &&
           completion_ == Completion::none && !outcome_delivered_;
}

void LeagueTableRangeDialog::begin_flash(int control, Completion completion) {
    pressed_control_ = control;
    flash_waiting_for_present_ = true;
    flash_presented_ = false;
    source_wait_remaining_ = 0;
    completion_ = completion;
}

void LeagueTableRangeDialog::begin_cancel() {
    if (outcome_delivered_ || awaiting_exit_present_ || completion_ != Completion::none) return;
    pressed_control_ = -1;
    dragging_ = false;
    completion_ = Completion::cancel;
    flash_waiting_for_present_ = false;
    flash_presented_ = false;
    source_wait_remaining_ = kCancelWait;
}

bool LeagueTableRangeDialog::apply_control_key(int index) {
    if (index < 0 || index > 14 || !input_ready())
        return false;

    if (index < 10) {
        append_digit(unsigned(index));
        begin_flash(index);
        return true;
    }
    if (index == 10) { // Source backspace
        if (digit_count_ != 0) {
            value_ /= 10;
            --digit_count_;
        }
        begin_flash(index);
        return true;
    }
    if (index == 11) { // Reset to param7; it is not preset recurrence state.
        value_ = std::min(profile_.default_value, profile_.maximum);
        digit_count_ = value_ == 0 ? 1 : decimal_digits(value_);
        begin_flash(index);
        return true;
    }
    if (index == 12) { // Clear
        value_ = 0;
        digit_count_ = 0;
        begin_flash(index);
        return true;
    }
    if (index == 13) { // Accept; below minimum clamps and leaves the dialog open.
        if (value_ < profile_.minimum) {
            value_ = profile_.minimum;
            digit_count_ = decimal_digits(value_);
            begin_flash(index);
        } else {
            begin_flash(index, Completion::accept);
        }
        return true;
    }
    begin_cancel(); // Source Cancel control 14
    return true;
}

bool LeagueTableRangeDialog::activate_cell(int index) {
    if (index < 15 || index > 22 || !input_ready())
        return false;
    if (!presets_[size_t(index - 15)]) return true; // disabled Source preset hit is consumed
    value_ = *presets_[size_t(index - 15)];
    digit_count_ = value_ == 0 ? 0 : decimal_digits(value_);
    displayed_value_ = value_;
    displayed_format_ = 8;
    updated_modal_present_pending_ = true;
    return true; // BalanceFromGame redraws/presents the new value without a 4-unit key flash.
}

bool LeagueTableRangeDialog::pointer_down(int px, int py, bool right) {
    if (outcome_delivered_) return false;
    if (right) return true; // modal consumes RMB but does not infer Source event 8
    if (dragging_) {
        pointer_x_ = px;
        pointer_y_ = py;
        x_ = std::clamp(px - drag_offset_x_, 0, 0xcf);
        y_ = std::clamp(py - drag_offset_y_, 0, 0x68);
        return true;
    }
    if (!input_ready())
        return true;
    pointer_x_ = px;
    pointer_y_ = py;

    // InsideInclusive uses inclusive rectangle edges for dragging the dialog title strip.
    if (inside_inclusive(px, py, x_, y_, x_ + 0x71, y_ + 0x10)) {
        dragging_ = true;
        drag_offset_x_ = px - x_;
        drag_offset_y_ = py - y_;
        return true;
    }

    for (int i = 0; i < 15; ++i) {
        if (inside_cell(i, px, py)) return apply_control_key(i);
    }
    for (int i = 15; i <= 22; ++i) {
        if (inside_cell(i, px, py)) return activate_cell(i);
    }
    return true; // modal owns the event; do not leak clicks to the table below
}

bool LeagueTableRangeDialog::move(int px, int py) {
    if (outcome_delivered_ || completion_ != Completion::none || awaiting_exit_present_ ||
        pointer_x_ < 0)
        return false;
    if (dragging_) {
        x_ = std::clamp(px - drag_offset_x_, 0, 0xcf);
        y_ = std::clamp(py - drag_offset_y_, 0, 0x68);
    }
    pointer_x_ = px;
    pointer_y_ = py;
    return dragging_;
}

bool LeagueTableRangeDialog::pointer_up(int px, int py, bool) {
    if (outcome_delivered_) return false;
    const bool handled = true; // modal owns releases even when Source has no button action
    (void)px;
    (void)py;
    dragging_ = false;
    pointer_x_ = pointer_y_ = -1;
    return handled;
}

bool LeagueTableRangeDialog::key(std::string_view key_name) {
    if (outcome_delivered_) return false;
    // Any non-event2 input exits BalanceFromGame's nested drag loop; this same key is
    // then processed by the outer keyboard-event handler.
    dragging_ = false;
    pointer_x_ = pointer_y_ = -1;
    if (!input_ready()) return true;
    if (key_name.size() == 1 && key_name[0] >= '0' && key_name[0] <= '9')
        return text(key_name);
    if (key_name == "BACKSPACE") return apply_control_key(10);
    if (key_name == "SPACE") return apply_control_key(12);
    if (key_name == "ENTER" || key_name == "RETURN") return apply_control_key(13);
    // BalanceFromGame has no proven ESC keyboard branch. Do not map ESC to Cancel.
    return true; // modal consumes unsupported keys; ESC deliberately has no effect
}

bool LeagueTableRangeDialog::text(std::string_view input) {
    if (outcome_delivered_) return false;
    dragging_ = false;
    pointer_x_ = pointer_y_ = -1;
    if (input.size() != 1 || input[0] < '0' || input[0] > '9') return true;
    if (!input_ready()) return true;
    const unsigned digit = unsigned(input[0] - '0');
    append_digit(digit);
    begin_flash(int(digit));
    return true;
}

bool LeagueTableRangeDialog::cancel_from_source_event() {
    if (!input_ready())
        return false;
    // Raw CareerSession_BeginFriendlyOpponentTable_ExecutableField2==8 jumps to exit before the local_38==-1 check that waits 0x32.
    // It still requires the final exit Present before the typed cancel is exposed.
    dragging_ = false;
    pointer_x_ = pointer_y_ = -1;
    completion_ = Completion::cancel;
    awaiting_exit_present_ = true;
    return true;
}

bool LeagueTableRangeDialog::tick_source_units(uint32_t units) {
    if (outcome_delivered_ || units == 0) return false;
    if (flash_waiting_for_present_ && !flash_presented_) return false;
    if (source_wait_remaining_ == 0) return false;

    const uint32_t elapsed = std::min(units, source_wait_remaining_);
    source_wait_remaining_ -= elapsed;
    if (source_wait_remaining_ != 0) return false;

    if (flash_presented_) {
        flash_presented_ = false;
        flash_waiting_for_present_ = false;
        pressed_control_ = -1;
        clamp_to_source_max();
        displayed_value_ = value_;
        displayed_format_ = 8;
        updated_modal_present_pending_ = true;
        return true;
    }
    if (completion_ == Completion::cancel) awaiting_exit_present_ = true;
    return true;
}

std::optional<TableRangeDialogOutcome> LeagueTableRangeDialog::after_present() {
    if (outcome_delivered_) return std::nullopt;
    if (!initial_presented_) {
        initial_presented_ = true;
        updated_modal_present_pending_ = false;
        return std::nullopt;
    }
    if (flash_waiting_for_present_ && !flash_presented_) {
        flash_presented_ = true;
        source_wait_remaining_ = kFlashWait;
        return std::nullopt;
    }
    if (updated_modal_present_pending_ && !awaiting_exit_present_) {
        updated_modal_present_pending_ = false;
        if (completion_ != Completion::none) awaiting_exit_present_ = true;
        return std::nullopt;
    }
    if (!awaiting_exit_present_ || source_wait_remaining_ != 0 || completion_ == Completion::none)
        return std::nullopt;

    outcome_delivered_ = true;
    return TableRangeDialogOutcome{
        completion_ == Completion::accept ? TableRangeDialogResult::accept : TableRangeDialogResult::cancel,
        completion_ == Completion::accept ? value_ : 0U,
        x_, y_
    };
}

} // namespace bmh::ui
