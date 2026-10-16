// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/outgoing_transfer.hpp"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <utility>

#include "formats/textd.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
constexpr uint32_t kTextBase = ::bmh::layout::TextPointerTable;
constexpr int kCalculatorX = 103, kCalculatorY = 55;
constexpr std::array<int, 4> kFooterButtonX{1, 80, 156, 232};
// FormatError3 stores the adjusted height 10 in the hit descriptor; ExpireQuestion's
// right/bottom comparisons are inclusive, so the hit rectangle ends at y=225.
constexpr int kFooterButtonY = 215, kFooterButtonWidth = 75, kFooterButtonHeight = 10;

std::string_view text_at(const TextDb& texts, uint32_t address) {
    if (address < kTextBase || (address - kTextBase) % 4 != 0)
        throw FormatError("outgoing transfer: invalid Source TEXTD address");
    return texts[(address - kTextBase) / 4];
}

void copy_region(Image& target, const Image& source, int sx, int sy, int x, int y,
                 int width, int height, int transparent = -1) {
    if (sx < 0 || sy < 0 || x < 0 || y < 0 || width < 0 || height < 0 ||
        sx + width > source.width || sy + height > source.height ||
        x + width > target.width || y + height > target.height)
        throw FormatError("outgoing transfer: invalid Source sprite bounds");
    for (int yy = 0; yy < height; ++yy) for (int xx = 0; xx < width; ++xx) {
        const auto pixel = source.pixels[size_t(sy + yy) * size_t(source.width) + size_t(sx + xx)];
        if (transparent < 0 || pixel != transparent) target.at(x + xx, y + yy) = pixel;
    }
}

void fill_rect(Image& out, int x, int y, int width, int height, uint8_t color) {
    if (x < 0 || y < 0 || width < 0 || height < 0 || x + width > out.width || y + height > out.height)
        throw FormatError("outgoing transfer: invalid Source rectangle");
    for (int yy = y; yy < y + height; ++yy)
        std::fill_n(out.pixels.begin() + size_t(yy) * size_t(out.width) + size_t(x), width, color);
}

std::string grouped(uint32_t value) {
    std::string digits = std::to_string(value);
    for (int at = int(digits.size()) - 3; at > 0; at -= 3) digits.insert(size_t(at), ".");
    return digits;
}

std::string source_number(uint32_t value, int width, bool grouping = true) {
    std::string digits = std::to_string(value);
    const int actual = int(digits.size());
    const int padding = std::max(0, width - actual);
    digits.insert(0, size_t(padding), '%');
    if (grouping) {
        for (int from = actual - 3; from > 0; from -= 3)
            digits.insert(size_t(padding + from), ".");
        const int wanted = width > 3 ? (width - 4) / 3 + 1 : 0;
        const int used = actual > 3 ? (actual - 1) / 3 : 0;
        if (wanted > used) digits.insert(0, size_t(wanted - used), ' ');
    }
    return digits;
}

std::string source_money(uint32_t value, uint32_t numerator, uint32_t denominator) {
    if (denominator) value = uint32_t((uint64_t(value) * numerator) / denominator);
    return source_number(value, 1, true) + " EU";
}

std::string calculator_value(uint32_t value, const OutgoingTransferCalculatorProfile& profile,
                             uint32_t numerator, uint32_t denominator) {
    return profile.currency ? source_money(value, numerator, denominator) : grouped(value);
}

bool money_field(OutgoingTransferField field) {
    return field == OutgoingTransferField::fee || field == OutgoingTransferField::monthly_salary;
}

std::string field_name(OutgoingTransferField field) {
    switch (field) {
    case OutgoingTransferField::fee: return "fee";
    case OutgoingTransferField::monthly_salary: return "monthly_salary";
    case OutgoingTransferField::duration_years: return "duration_years";
    case OutgoingTransferField::bonus_0: return "bonus_0";
    case OutgoingTransferField::bonus_1: return "bonus_1";
    case OutgoingTransferField::bonus_2: return "bonus_2";
    case OutgoingTransferField::bonus_3: return "bonus_3";
    case OutgoingTransferField::bonus_4: return "bonus_4";
    case OutgoingTransferField::bonus_5: return "bonus_5";
    case OutgoingTransferField::bonus_6: return "bonus_6";
    case OutgoingTransferField::quote_loan: return "quote_loan";
    }
    return "fee";
}

OutgoingTransferEditorState with_source_title(OutgoingTransferEditorState state) {
    state.panel.title = state.initial_action_loan
        ? IncomingTransferTitle::player_loan : IncomingTransferTitle::player_purchase;
    return state;
}
} // namespace

std::string outgoing_transfer_request_json(const OutgoingTransferRequest& request) {
    const char* kind = "none";
    switch (request.kind) {
    case OutgoingTransferRequestKind::none: kind = "none"; break;
    case OutgoingTransferRequestKind::edit_field: kind = "edit_field"; break;
    case OutgoingTransferRequestKind::quote_change: kind = "quote_change"; break;
    case OutgoingTransferRequestKind::team_info: kind = "team_info"; break;
    case OutgoingTransferRequestKind::player_info: kind = "player_info"; break;
    case OutgoingTransferRequestKind::submit: kind = "submit"; break;
    case OutgoingTransferRequestKind::cancel: kind = "cancel"; break;
    }
    std::ostringstream out;
    const auto& id = request.identity;
    out << "{\"kind\":" << json_quote(kind)
        << ",\"manager_slot\":" << id.manager_slot << ",\"runtime_club\":" << id.runtime_club
        << ",\"revision\":" << id.revision << ",\"list_entry\":" << id.list_entry
        << ",\"source_slot\":" << id.source_slot << ",\"player\":" << id.player
        << ",\"seller\":" << id.seller << ",\"bid_slot\":" << id.bid_slot
        << ",\"field\":" << json_quote(field_name(request.field)) << ",\"value\":" << request.value
        << ",\"accepted\":" << (request.accepted ? "true" : "false")
        << ",\"quote_loan\":" << (request.quote_loan ? "true" : "false")
        << ",\"fee\":" << request.fee << ",\"monthly_salary\":" << request.monthly_salary
        << ",\"duration_years\":" << unsigned(request.duration_years) << ",\"bonuses\":[";
    for (size_t i = 0; i < request.bonuses.size(); ++i) {
        if (i) out << ',';
        out << unsigned(request.bonuses[i]);
    }
    out << "],\"quoted_fee\":" << request.quoted_fee
        << ",\"quoted_monthly_salary\":" << request.quoted_monthly_salary
        << ",\"quoted_years\":" << unsigned(request.quoted_years) << '}';
    return out.str();
}

OutgoingTransferScreen::OutgoingTransferScreen(const std::filesystem::path& data,
                                               OutgoingTransferEditorState state)
    : state_(with_source_title(std::move(state))), confirmed_state_(state_), data_(data),
      panel_(data_, state_.panel, state_.gradient_text),
      exe_(load_le(asset_path(data, "BMH.exe"))),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      body_(load_font(asset_path(data / "F", "BMH3.FNT"))),
      calculator_art_(load_vga(asset_path(data / "KB", "6.VGA"))),
      panel_art_(load_vga(asset_path(data / "KB", "31.VGA"))),
      button_art_(load_vga(asset_path(data / "KB", "0.VGA"))), sprite_font_(data) {
    if (state_.manager_slot < 0 || state_.runtime_club < 0 || state_.list_entry < 0 || state_.list_entry >= 44 ||  // 40..43: poaching
        state_.source_slot < 0 || state_.player < 0 || state_.player != state_.panel.player ||
        state_.list_entry != state_.panel.list_entry || state_.panel.pending_offer_source ||
        !state_.panel.source_outgoing_editor_context ||
        state_.seller != state_.panel.seller || state_.panel.viewer_manager != state_.manager_slot ||
        (state_.editable ? (state_.bid_slot < 0 || state_.bid_slot >= 4) : state_.bid_slot != -1) ||
        state_.source_mode < 1 || state_.source_mode > 3 ||
        panel_art_.width != 320 || panel_art_.height != 206 || calculator_art_.width < 113 ||
        calculator_art_.height < 150 || button_art_.width < 53 || button_art_.height < 104)
        throw FormatError("outgoing transfer: incomplete Source editor projection");
    if (state_.editable) {
        const auto& row = state_.panel.offers[size_t(state_.bid_slot)];
        if (row.present && (row.bid_index != state_.bid_slot || row.buyer_manager != state_.manager_slot ||
                            row.buyer != state_.runtime_club))
            throw FormatError("outgoing transfer: raw bid slot is not owned by active manager");
        if (row.present && (row.fee != state_.fee || row.monthly_salary != state_.monthly_salary ||
            row.bonuses != state_.bonuses ||
            ((int(row.contract_months) - 1) / 12 + 1) != state_.duration_years))
            throw FormatError("outgoing transfer: editor fields do not match the active Native bid row");
    }
    auto check_profile = [](const OutgoingTransferCalculatorProfile& profile) {
        if (!profile.supported || profile.minimum > profile.maximum ||
            profile.initial < profile.minimum || profile.initial > profile.maximum)
            throw FormatError("outgoing transfer: invalid Source numeric calculator profile");
        for (const auto& preset : profile.presets)
            if (preset && (*preset < profile.minimum || *preset > profile.maximum))
                throw FormatError("outgoing transfer: preset outside Source calculator bounds");
    };
    if (state_.editable) {
        for (const auto& profile : state_.money_calculators) {
            check_profile(profile);
            if (profile.source_text_address) (void)text_at(texts_, profile.source_text_address);
        }
        if (state_.source_mode == 3)
            for (const auto& profile : state_.mode3_calculators) {
                check_profile(profile);
                if (profile.source_text_address) (void)text_at(texts_, profile.source_text_address);
            }
    }
    closed_ = state_.closed;
}

OutgoingTransferIdentity OutgoingTransferScreen::identity() const {
    return {state_.manager_slot, state_.runtime_club, state_.revision, state_.list_entry,
            state_.source_slot, state_.player, state_.seller, state_.bid_slot};
}

uint32_t OutgoingTransferScreen::field_value(OutgoingTransferField field) const {
    switch (field) {
    case OutgoingTransferField::fee: return state_.fee;
    case OutgoingTransferField::monthly_salary: return state_.monthly_salary;
    case OutgoingTransferField::duration_years: return state_.duration_years;
    case OutgoingTransferField::bonus_0: return state_.bonuses[0];
    case OutgoingTransferField::bonus_1: return state_.bonuses[1];
    case OutgoingTransferField::bonus_2: return state_.bonuses[2];
    case OutgoingTransferField::bonus_3: return state_.bonuses[3];
    case OutgoingTransferField::bonus_4: return state_.bonuses[4];
    case OutgoingTransferField::bonus_5: return state_.bonuses[5];
    case OutgoingTransferField::bonus_6: return state_.bonuses[6];
    case OutgoingTransferField::quote_loan: return state_.quote_loan ? 1u : 0u;
    }
    return 0;
}

uint32_t OutgoingTransferScreen::field_maximum(OutgoingTransferField field) const {
    if (field == OutgoingTransferField::fee) return state_.money_calculators[0].maximum;
    if (field == OutgoingTransferField::monthly_salary) return state_.money_calculators[1].maximum;
    if (field == OutgoingTransferField::duration_years) return state_.mode3_calculators[0].maximum;
    const int index = int(field) - int(OutgoingTransferField::bonus_0) + 1;
    return index >= 1 && index < 8 ? state_.mode3_calculators[size_t(index)].maximum : 0;
}

void OutgoingTransferScreen::set_field_value(OutgoingTransferField field, uint32_t value) {
    switch (field) {
    case OutgoingTransferField::fee: state_.fee = value; break;
    case OutgoingTransferField::monthly_salary: state_.monthly_salary = value; break;
    case OutgoingTransferField::duration_years: state_.duration_years = uint8_t(value); break;
    case OutgoingTransferField::bonus_0: state_.bonuses[0] = uint8_t(value); break;
    case OutgoingTransferField::bonus_1: state_.bonuses[1] = uint8_t(value); break;
    case OutgoingTransferField::bonus_2: state_.bonuses[2] = uint8_t(value); break;
    case OutgoingTransferField::bonus_3: state_.bonuses[3] = uint8_t(value); break;
    case OutgoingTransferField::bonus_4: state_.bonuses[4] = uint8_t(value); break;
    case OutgoingTransferField::bonus_5: state_.bonuses[5] = uint8_t(value); break;
    case OutgoingTransferField::bonus_6: state_.bonuses[6] = uint8_t(value); break;
    case OutgoingTransferField::quote_loan: state_.quote_loan = value != 0; break;
    }
}

OutgoingTransferRequest OutgoingTransferScreen::make_request(OutgoingTransferRequestKind kind,
                                                              OutgoingTransferField field,
                                                              uint32_t value) const {
    OutgoingTransferRequest request;
    request.kind = kind;
    request.identity = identity();
    request.field = field;
    request.value = value;
    request.quote_loan = state_.quote_loan;
    request.fee = state_.fee;
    request.monthly_salary = state_.monthly_salary;
    request.duration_years = state_.duration_years;
    request.bonuses = state_.bonuses;
    request.quoted_fee = state_.panel.quoted_fee;
    request.quoted_monthly_salary = state_.panel.quoted_monthly_salary;
    request.quoted_years = state_.panel.quoted_years;
    return request;
}

OutgoingTransferRequest OutgoingTransferScreen::commit_field(bool accepted) {
    if (!calculator_open_) return {};
    const uint32_t old = field_value(active_field_);
    uint32_t input_value = calculator_value_;
    uint32_t minimum = 0, maximum = field_maximum(active_field_);
    if (money_field(active_field_)) {
        minimum = active_profile_.minimum;
        maximum = active_profile_.maximum;
    } else if (active_field_ == OutgoingTransferField::duration_years) {
        minimum = state_.mode3_calculators[0].minimum;
    } else {
        const size_t ix = size_t(int(active_field_) - int(OutgoingTransferField::bonus_0) + 1);
        minimum = state_.mode3_calculators[ix].minimum;
    }
    input_value = std::clamp(input_value, minimum, maximum);
    calculator_open_ = false;
    numeric_text_.clear();
    active_profile_ = {};
    if (!accepted) {
        if (!money_field(active_field_)) return {};
        auto request = make_request(OutgoingTransferRequestKind::quote_change, active_field_, old);
        request.accepted = false;
        pending_ = request;
        return request;
    }
    uint32_t native_value = input_value;
    if (active_field_ >= OutgoingTransferField::bonus_3 &&
        active_field_ <= OutgoingTransferField::bonus_6) native_value /= 5u;
    set_field_value(active_field_, native_value);
    // Every committed editor value returns through Source LAB_0001bad7, which
    // recomputes MakeRequest/AddToSquad. Bonus edits also need a fresh quote.
    auto request = make_request(OutgoingTransferRequestKind::quote_change, active_field_, input_value);
    pending_ = request;
    return request;
}

void OutgoingTransferScreen::draw_editor_fields(Image& out) const {
    const auto& panel = state_.panel;
    auto body_baseline = [&](int x, int baseline, std::string_view text, uint8_t color) {
        const int top = baseline - body_.baseline;
        const int width = body_.text_width(text);
        if (x < 0 || top < 0 || x + width > out.width || top + body_.height > out.height || text.empty()) return;
        if (!state_.gradient_text || exe_.u8(::bmh::layout::TextGradientMode) != 2) {
            body_.draw(out, x, top, text, color);
            return;
        }
        Image mask(std::max(1, width), body_.height);
        body_.draw(mask, 0, 0, text, 1);
        int gradient = 0;
        while (gradient < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + uint32_t(gradient)) != color) ++gradient;
        for (int yy = 0; yy < body_.height; ++yy)
            for (int xx = 0; xx < width; ++xx)
                if (mask.at(xx, yy)) out.at(x + xx, top + yy) =
                    gradient < 32 ? exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + uint32_t(gradient * 6 + std::min(yy, 5))) : color;
    };
    auto money_text = [&](uint32_t value, int width) {
        if (panel.currency_denominator)
            value = uint32_t((uint64_t(value) * panel.currency_numerator) / panel.currency_denominator);
        return source_number(value, width, true) + " EU";
    };

    // Keep the asking lines drawn by the shared Source panel at baseline 123.
    // OpenTransferEditor adds the selected bid on its own baseline-130 row.
    auto offer_line = [&](uint32_t label_address, uint32_t value, int width, int x) {
        std::string line(text_at(texts_, label_address));
        if (state_.editable) line += money_text(value, width);
        const int shadow_top = 130 + 2 - body_.baseline;
        const int text_width = body_.text_width(line);
        if (x >= 0 && shadow_top >= 0 && x + 2 + text_width <= out.width &&
            shadow_top + body_.height <= out.height)
            body_.draw(out, x + 2, shadow_top, line, 0x40);
        body_baseline(x, 130, line, 0x6a);
    };
    offer_line(::bmh::layout::OutgoingTransfer_Line_ExecutableField, state_.fee, 7, 7);
    offer_line(::bmh::layout::OutgoingTransfer_Line_ExecutableField2, state_.monthly_salary, 5, 170);

    if (state_.source_mode != 3) return;
    for (int row = 0; row < 8; ++row) {
        const int y = 0x8e + row * 9;
        const uint8_t color = exe_.u8(::bmh::layout::OutgoingTransfer_Line_RuntimeField + uint32_t(row));
        body_baseline(0xc0, y, text_at(texts_, ::bmh::layout::DeskActions_EditorProject_ExecutableField + uint32_t(row) * 4), color);
        if (row == 0) {
            std::string duration = std::to_string(state_.duration_years);
            duration += std::string(text_at(texts_, ::bmh::layout::Contracts_Out_ExecutableField15));
            if (state_.duration_years != 1) duration += exe_.cstring(::bmh::layout::Fltd_Layout_TextPointer3);
            body_baseline(0x108, y, duration, 0x6c);
        } else {
            const uint32_t value = uint32_t(state_.bonuses[size_t(row - 1)]) * (row > 3 ? 5u : 1u);
            const std::string displayed = value == 0 ? std::string(text_at(texts_, ::bmh::layout::OutgoingTransfer_Line_TextPointer)) :
                std::to_string(value) + exe_.cstring(::bmh::layout::Fltd_Layout_TextPointer4);
            body_baseline(0x108, y, displayed, 0x6c);
        }
    }
}

void OutgoingTransferScreen::draw_calculator(Image& out) const {
    constexpr int x0 = kCalculatorX, y0 = kCalculatorY;
    if (!calculator_open_ || !active_profile_.supported) return;
    copy_region(out, calculator_art_, 0, 0, x0, y0, 113, 136);
    fill_rect(out, x0 + 113, y0 + 2, 2, 136, 0x40);
    fill_rect(out, x0 + 2, y0 + 136, 113, 2, 0x40);
    std::string title;
    if (active_profile_.source_text_address)
        title = std::string(text_at(texts_, active_profile_.source_text_address));
    title += calculator_value(active_profile_.initial, active_profile_,
        state_.panel.currency_numerator, state_.panel.currency_denominator);
    auto draw_body = [&](int x, int baseline, std::string_view text, uint8_t color, bool gradient) {
        const int top = baseline - body_.baseline;
        const int width = body_.text_width(text);
        if (text.empty() || x < 0 || top < 0 || x + width > out.width || top + body_.height > out.height) return;
        if (!gradient || !state_.gradient_text || exe_.u8(::bmh::layout::TextGradientMode) != 2) {
            body_.draw(out, x, top, text, color);
            return;
        }
        Image mask(std::max(1, width), body_.height);
        body_.draw(mask, 0, 0, text, 1);
        int gradient_index = 0;
        while (gradient_index < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + uint32_t(gradient_index)) != color) ++gradient_index;
        for (int yy = 0; yy < body_.height; ++yy)
            for (int xx = 0; xx < width; ++xx)
                if (mask.at(xx, yy)) out.at(x + xx, top + yy) = gradient_index < 32
                    ? exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + uint32_t(gradient_index * 6 + std::min(yy, 5))) : color;
    };
    const int width = body_.text_width(title);
    draw_body(x0 + 8 + (105 - width) / 2, y0 + 14, title, 0x62, true);
    constexpr std::string_view keys = "0123456789DRCJN";
    for (int i = 0; i < 15; ++i) {
        const int x = x0 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(i) * 2);
        const int y = y0 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(i));
        copy_region(out, calculator_art_, 0, 136, x, y, 15, 14);
        draw_body(x + 5, y + 9, keys.substr(size_t(i), 1), 0x62, false);
    }
    for (size_t i = 0; i < active_profile_.presets.size(); ++i) {
        if (!active_profile_.presets[i]) continue;
        const uint32_t value = *active_profile_.presets[i];
        const int x = x0 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + (i + 15) * 2);
        const int y = y0 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + i + 15);
        const std::string text = calculator_value(value, active_profile_,
            state_.panel.currency_numerator, state_.panel.currency_denominator);
        draw_body(x + 47 - body_.text_width(text), y + 12, text, 0x62, true);
    }
    const std::string value = calculator_value(calculator_value_, active_profile_,
        state_.panel.currency_numerator, state_.panel.currency_denominator);
    draw_body(x0 + 106 - body_.text_width(value), y0 + 110, value, 0x00, true);
}

void OutgoingTransferScreen::draw_footer(Image& out) const {
    // OpenTransferEditor registers four FormatError3 controls from a216c/a216e/a2170.
    // The address stride in OutgoingTransfer_DrawFooter_TextPointer is exactly one TEXTD pointer (four bytes):
    // IDs 230..233 are Team-Info, Spieler-Info, Angebot and Abbruch.
    constexpr int drawn_height = 10; // FormatError3 raises requested h=9 for width>31.
    for (size_t i = 0; i < kFooterButtonX.size(); ++i) {
        const int x = kFooterButtonX[i];
        // FormatError3 first calls ColorBottomRightBevel for the raised control's shadow.
        fill_rect(out, x + 2, kFooterButtonY + drawn_height, kFooterButtonWidth, 2, 0x40);
        fill_rect(out, x + kFooterButtonWidth, kFooterButtonY + 2, 2, drawn_height, 0x40);
        // FormatError3's standard KB0 frame; its requested nine-pixel height
        // is raised to ten for widths over 31, then the sprite is tiled.
        copy_region(out, button_art_, 24, 74, x, kFooterButtonY, 31, drawn_height);
        int used = 31;
        while (kFooterButtonWidth - used > 20) {
            const int count = std::min(8, kFooterButtonWidth - used - 20);
            copy_region(out, button_art_, 24, 84, x + used, kFooterButtonY, count, drawn_height);
            used += count;
        }
        const int remaining = kFooterButtonWidth - used;
        copy_region(out, button_art_, 45 - remaining, 94, x + used, kFooterButtonY,
                    remaining, drawn_height);
        const auto label = original_to_utf8(text_at(texts_, ::bmh::layout::OutgoingTransfer_DrawFooter_TextPointer + uint32_t(i) * 4));
        const int text_width = sprite_font_.text_width(label);
        sprite_font_.draw(out, x + kFooterButtonWidth / 2 - text_width / 2,
                          kFooterButtonY + 7, label, exe_.u8(::bmh::layout::Advertising_Button_RuntimeField));
    }
}

Image OutgoingTransferScreen::render() const {
    Image out = panel_.render();
    draw_editor_fields(out);
    draw_calculator(out);
    draw_footer(out);
    return out;
}

OutgoingTransferRequest OutgoingTransferScreen::process_timer_expired() {
    return click(0, 0, true); //BalanceFromGame/OpenTransferEditor event8; right path does not inspect coordinates.
}

OutgoingTransferRequest OutgoingTransferScreen::click(int x, int y, bool right, bool double_click) {
    if (closed_ || pending_) return {};
    if (double_click && !right && !calculator_open_ && y > 0x12 && y < 0x21) {
        state_.quote_loan = !state_.quote_loan;
        auto request = make_request(OutgoingTransferRequestKind::quote_change,
                                    OutgoingTransferField::quote_loan, state_.quote_loan ? 1u : 0u);
        pending_ = request;
        return request;
    }
    if (calculator_open_) {
        if (right) return commit_field(false);
        for (int i = 0; i < 15; ++i) {
            const int key_x = kCalculatorX + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(i) * 2);
            const int key_y = kCalculatorY + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(i));
            if (x < key_x || x >= key_x + 16 || y < key_y || y >= key_y + 14) continue;
            if (i < 10) { text(std::to_string(i)); return {}; }
            if (i == 10) { calculator_value_ /= 10; numeric_text_ = std::to_string(calculator_value_); return {}; }
            if (i == 11) { calculator_value_ = active_profile_.initial; numeric_text_.clear(); return {}; }
            if (i == 12) { calculator_value_ = 0; numeric_text_ = "0"; return {}; }
            if (i == 13) return commit_field();
            return commit_field(false);
        }
        for (size_t i = 0; i < active_profile_.presets.size(); ++i) {
            if (!active_profile_.presets[i]) continue;
            const int key_x = kCalculatorX + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + (i + 15) * 2);
            const int key_y = kCalculatorY + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + i + 15);
            if (x >= key_x && x < key_x + 53 && y >= key_y && y < key_y + 14) {
                calculator_value_ = *active_profile_.presets[i];
                numeric_text_ = std::to_string(calculator_value_);
                return {};
            }
        }
        return {};
    }
    if (right) {
        closed_ = true;
        auto request = make_request(OutgoingTransferRequestKind::cancel, active_field_, field_value(active_field_));
        pending_ = request;
        return request;
    }
    for (size_t i = 0; i < kFooterButtonX.size(); ++i) {
        const int left = kFooterButtonX[i];
        // ExpireQuestion uses inclusive right/bottom bounds.
        if (x < left || x > left + kFooterButtonWidth || y < kFooterButtonY ||
            y > kFooterButtonY + kFooterButtonHeight)
            continue;
        if (i == 0 || i == 1) {
            auto request = make_request(i == 0 ? OutgoingTransferRequestKind::team_info
                                               : OutgoingTransferRequestKind::player_info,
                                        active_field_, field_value(active_field_));
            pending_ = request;
            return request;
        }
        if (i == 2) {
            if (!state_.editable) return {};
            closed_ = true;
            auto request = make_request(OutgoingTransferRequestKind::submit,
                                        active_field_, field_value(active_field_));
            pending_ = request;
            return request;
        }
        closed_ = true;
        auto request = make_request(OutgoingTransferRequestKind::cancel,
                                    active_field_, field_value(active_field_));
        pending_ = request;
        return request;
    }
    if (!state_.editable) return {};
    if (x >= 0 && x <= 158 && y >= 118 && y <= 131) {
        active_field_ = OutgoingTransferField::fee;
        active_profile_ = state_.money_calculators[0];
    } else if (x >= 161 && x <= 319 && y >= 118 && y <= 131) {
        active_field_ = OutgoingTransferField::monthly_salary;
        active_profile_ = state_.money_calculators[1];
    } else if (state_.source_mode == 3 && x >= 0xb5 && x <= 0x135 && y >= 0x89 && y < 0xcd) {
        const int row = (y - 0x89) / 9;
        if (row < 0 || row > 7) return {};
        active_field_ = row == 0 ? OutgoingTransferField::duration_years :
            OutgoingTransferField(uint8_t(OutgoingTransferField::bonus_0) + row - 1);
        active_profile_ = state_.mode3_calculators[size_t(row)];
    } else {
        return {};
    }
    numeric_text_.clear();
    if (!active_profile_.supported) return {};
    calculator_value_ = active_profile_.initial;
    calculator_open_ = true;
    // Opening/focusing the Source KB6 editor is local UI state. The owner is
    // called only after a committed value (or the Source-required cancel).
    return {};
}

bool OutgoingTransferScreen::text(std::string_view input) {
    if (closed_ || pending_ || !calculator_open_ || input.empty()) return false;
    for (char c : input) if (c < '0' || c > '9') return false;
    const uint64_t ceiling = active_profile_.maximum;
    std::string candidate = numeric_text_ + std::string(input);
    if (candidate.size() > 10) return false;
    uint64_t parsed = 0;
    const auto [end, ec] = std::from_chars(candidate.data(), candidate.data() + candidate.size(), parsed);
    if (ec != std::errc{} || end != candidate.data() + candidate.size()) return false;
    const uint32_t value = uint32_t(std::min<uint64_t>(parsed, ceiling));
    calculator_value_ = value;
    numeric_text_ = std::to_string(value);
    return true;
}

OutgoingTransferRequest OutgoingTransferScreen::key(std::string_view key_name) {
    if (closed_ || pending_) return {};
    if (key_name == "ESC" || key_name == "ESCAPE" || (key_name == "N" && calculator_open_)) {
        if (calculator_open_) return commit_field(false);
        closed_ = true;
        auto request = make_request(OutgoingTransferRequestKind::cancel, active_field_, field_value(active_field_));
        pending_ = request;
        return request;
    }
    if (key_name == "J" && calculator_open_) return commit_field();
    if (key_name == "ENTER" || key_name == "RETURN") {
        if (calculator_open_) return commit_field();
        if (!state_.editable) return {};
        closed_ = true;
        auto request = make_request(OutgoingTransferRequestKind::submit, active_field_, field_value(active_field_));
        pending_ = request;
        return request;
    }
    if (!calculator_open_) return {};
    if (key_name == "BACKSPACE" || key_name == "D") {
        calculator_value_ /= 10; numeric_text_ = std::to_string(calculator_value_);
        return {};
    }
    if (key_name == "C" || key_name == "SPACE") {
        calculator_value_ = 0; numeric_text_ = "0"; return {};
    }
    if (key_name.size() == 1 && key_name[0] >= '0' && key_name[0] <= '9') {
        text(key_name); return {};
    }
    return {};
}

void OutgoingTransferScreen::acknowledge(OutgoingTransferEditorState state) {
    // Recreate the common panel only after the owner supplies the exact next
    // projection and quote. The caller controls revision advancement.
    if (state.manager_slot != state_.manager_slot || state.runtime_club != state_.runtime_club ||
        state.source_slot != state_.source_slot || state.list_entry != state_.list_entry ||
        state.player != state_.player ||
        (( !pending_ || (pending_->kind != OutgoingTransferRequestKind::team_info &&
                         pending_->kind != OutgoingTransferRequestKind::player_info)) && state.bid_slot != state_.bid_slot) ||
        state.revision < state_.revision ||
        state.initial_action_loan != state_.initial_action_loan ||
        !state.panel.source_outgoing_editor_context)
        throw FormatError("outgoing transfer: stale or mismatched acknowledgement");
    state_ = with_source_title(std::move(state));
    confirmed_state_ = state_;
    pending_.reset();
    closed_ = state_.closed;
    calculator_open_ = false;
    numeric_text_.clear();
    active_profile_ = {};
    panel_ = IncomingTransferScreen(data_, state_.panel, state_.gradient_text);
}

void OutgoingTransferScreen::reject_action(std::string reason) {
    pending_.reset();
    state_ = confirmed_state_;
    panel_ = IncomingTransferScreen(data_, state_.panel, state_.gradient_text);
    closed_ = false;
    feedback_ = std::move(reason);
    calculator_open_ = false;
    active_profile_ = {};
    numeric_text_.clear();
}

std::string OutgoingTransferScreen::state_json() const {
    std::ostringstream out;
    out << "{\"screen\":\"outgoing_transfer\",\"manager_slot\":" << state_.manager_slot
        << ",\"runtime_club\":" << state_.runtime_club << ",\"revision\":" << state_.revision
        << ",\"list_entry\":" << state_.list_entry << ",\"source_slot\":" << state_.source_slot
        << ",\"player\":" << state_.player << ",\"seller\":" << state_.seller
        << ",\"bid_slot\":" << state_.bid_slot << ",\"source_mode\":" << unsigned(state_.source_mode)
        << ",\"editable\":" << (state_.editable ? "true" : "false")
        << ",\"closed\":" << (closed_ ? "true" : "false")
        << ",\"quote_loan\":" << (state_.quote_loan ? "true" : "false")
        << ",\"initial_action_loan\":" << (state_.initial_action_loan ? "true" : "false")
        << ",\"gradient_text\":" << (state_.gradient_text ? "true" : "false")
        << ",\"fee\":" << state_.fee << ",\"monthly_salary\":" << state_.monthly_salary
        << ",\"duration_years\":" << unsigned(state_.duration_years) << ",\"bonuses\":[";
    for (size_t i = 0; i < state_.bonuses.size(); ++i) { if (i) out << ','; out << unsigned(state_.bonuses[i]); }
    out << "],\"calculator_open\":" << (calculator_open_ ? "true" : "false")
        << ",\"feedback\":" << json_quote(feedback_);
    if (pending_) out << ",\"pending\":" << outgoing_transfer_request_json(*pending_);
    out << '}';
    return out.str();
}

} // namespace bmh::ui
