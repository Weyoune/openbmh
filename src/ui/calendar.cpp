// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/calendar.hpp"
#include "game/schedule.hpp"

#include <sstream>
#include <limits>
#include <algorithm>

#include "game/messages.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {

CalendarCell calendar_cell(const CalendarDayProjection& day) {
    const auto& date = day.date;
    if (date.month < 1 || date.month > 12 || date.day < 1 || date.day > 31 ||
        date.weekday < 0 || date.weekday > 6)
        throw FormatError("calendar: invalid date geometry");
    const int month = (date.month + 5) % 12;
    const int first_weekday = (date.weekday - (date.day - 1) % 7 + 7) % 7;
    const int week = (first_weekday + date.day - 1) / 7;
    return {28 + (month % 4) * 70 + week * 12,
            29 + (month / 4) * 70 + date.weekday * 8, 11, 8};
}

CalendarScreen::CalendarScreen(const std::filesystem::path& data, CalendarState state,
                               std::string manager_name, bool decorated_background, int runtime_club,
                               DeskRequest owner)
    : state_(std::move(state)),
      background_(load_vga(asset_path(data / "KB", "129.VGA"))),
      atlas_(load_vga(asset_path(data / "KB", "55.VGA"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))), sprite_font_(data),
      exe_(load_le(asset_path(data, "BMH.exe"))), texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      decorated_background_(decorated_background), owner_(owner) {
    if (runtime_club >= 0) {
        // TeamLeague2 returns zero for team handles above0x164 (national included).
        int league = 0, boundary = 0;
        if (runtime_club <= 0x164) {
            for (; league < 9; ++league) {
                boundary += exe_.u8(::bmh::layout::LeagueSizes + uint32_t(league));
                if (runtime_club < boundary) break;
            }
        }
        // DE shares the third-tier bit between leagues 2/3; EN division 3
        // has its own bit. Use the loaded calendar topology for hit eligibility.
        active_calendar_class_ = league < kLeagueCount ? kLeagueBit[league] : -1;
    }
    (void)manager_name; // Source calendar's initial footer is empty, not a manager title.
    if (state_.season_start_counter > std::numeric_limits<uint32_t>::max() - 366 ||
        background_.width != 320 || background_.height != 240 ||
        atlas_.width != 320 || atlas_.height < 209 ||
        (state_.days.size() != 365 && state_.days.size() != 366) ||
        state_.current_day < 0 || size_t(state_.current_day) >= state_.days.size())
        throw FormatError("calendar: invalid annual projection or original assets");
    for (size_t index = 0; index < state_.days.size(); ++index) {
        const auto& day = state_.days[index];
        const auto expected = desk_date(state_.season_start_counter + uint32_t(index), exe_);
        if (day.season_day != int(index) - state_.leading_days ||
            day.day_counter != state_.season_start_counter + index ||
            day.date.year != expected.year || day.date.month != expected.month ||
            day.date.day != expected.day || day.date.weekday != expected.weekday ||
            day.current_day != (int(index) == state_.current_day))
            throw FormatError("calendar: inconsistent owned date projection");
        (void)calendar_cell(day);
    }
    const auto& first = state_.days.front().date;
    const auto& last = state_.days.back().date;
    const auto next = desk_date(state_.days.back().day_counter + 1, exe_);
    if (first.month != 7 || first.day != 1 || last.month != 6 || last.day != 30 ||
        next.month != 7 || next.day != 1)
        throw FormatError("calendar: projection is not a complete July-June season");
}

void CalendarScreen::text(Image& out, int x, int baseline, std::string_view bytes, uint8_t color) const {
    // TextWidth3 refuses text outside the original screen; no UTF-8 enters BMH3.
    const int top = baseline - font_.baseline;
    if (x >= 0 && top >= 0 && top + font_.height <= out.height &&
        x + font_.text_width(bytes) <= out.width)
        font_.draw(out, x, top, bytes, color);
}

std::string CalendarScreen::information(const CalendarDayProjection& day) const {
    // CalendarCell's generic competition footer. Club-specific fixture/score detail
    // remains outside this read-only calendar packet.
    std::string result;
    if ((day.raw_entry & 0x7fff) == 0) return result;
    for (int bit = 0; bit < 19; ++bit) {
        if (!(day.competitions & (uint32_t(1) << bit))) continue;
        if (!result.empty()) result += exe_.cstring(::bmh::layout::Calendar_Information_ExecutableField);
        if (bit < 4) result += std::to_string(bit + 1) + texts_[(::bmh::layout::Calendar_Information_TextPointer3 - ::bmh::layout::TextPointerTable) / 4];
        else if (bit == 4) result += texts_[(::bmh::layout::Calendar_Information_TextPointer4 - ::bmh::layout::TextPointerTable) / 4];
        else if (bit < 16) {
            const auto& label = texts_[(::bmh::layout::Calendar_Information_TextPointer2 - ::bmh::layout::TextPointerTable) / 4 + size_t(bit)];
            result += result.size() + label.size() < 30 ? label : exe_.cstring(::bmh::layout::Calendar_Information_TextPointer);
        } else result += texts_[((bit == 16 ? ::bmh::layout::Calendar_Information_TextPointer5 : ::bmh::layout::Calendar_Information_TextPointer6) - ::bmh::layout::TextPointerTable) / 4];
    }
    return result;
}

Image CalendarScreen::render() const {
    auto out = decorated_background_ ? background_ : Image(320, 240);
    if (!decorated_background_) {
        for (int y = 4; y <= 16; ++y) for (int x = 0; x < 320; ++x) out.at(x,y) = 0x44;
        for (int y = 5; y <= 15; ++y) for (int x = 1; x < 319; ++x) out.at(x,y) = 0x39;
        for (int y = 228; y <= 237; ++y) for (int x = 0; x < 320; ++x) out.at(x,y) = 0x44;
        for (int y = 229; y <= 236; ++y) for (int x = 1; x < 319; ++x) out.at(x,y) = 0x39;
    }
    const auto title_address = state_.reschedule_manager >= 0 ? ::bmh::layout::Calendar_Render_ExecutableField3Unsigned :
        state_.friendly_selection ? ::bmh::layout::Calendar_Render_ExecutableField2Unsigned : ::bmh::layout::Calendar_Render_ExecutableFieldUnsigned;
    const auto title = original_to_utf8(texts_[(title_address - ::bmh::layout::TextPointerTable) / 4]);
    sprite_font_.draw(out, SpriteFont::centered_title_x(sprite_font_.text_width(title)), 13, title);
    // FormatError2: opaque KB55 crop 320x209 at (0,18).
    for (int y = 0; y < 209; ++y) for (int x = 0; x < 320; ++x) out.at(x, y + 18) = atlas_.pixels[size_t(y) * atlas_.width + x];
    for (int row = 0; row < 3; ++row) for (int weekday = 0; weekday < 7; ++weekday)
        text(out, 8, 36 + weekday * 8 + row * 70,
             texts_[(::bmh::layout::Calendar_Render_TextPointer - ::bmh::layout::TextPointerTable) / 4 + size_t(weekday)], weekday == 6 ? 0xf7 : 0x6a);
    for (int month = 0; month < 12; ++month) {
        const auto& label = texts_[(::bmh::layout::Calendar_Render_TextPointer2 - ::bmh::layout::TextPointerTable) / 4 + size_t((month + 6) % 12 + 1)];
        text(out, 61 + (month % 4) * 70 - font_.text_width(label) / 2,
             25 + (month / 4) * 70, label, 0x6a);
    }
    for (const auto& day : state_.days) {
        const auto cell = calendar_cell(day);
        // CalendarCell colors every Source-eligible date in the calendar's local
        // copy; it does not draw persistent boxes around targets or moved dates.
        const uint8_t color = day.current_day ? 0xf7 :
            (day.reschedule_target || day.friendly_target) ? ((day.competitions & 0x1c0) != 0 ? 0x7a : 0x51) :
            day.competitions != 0 && day.competitions != 0x800000 ? 0x27 : 0x6a;
        text(out, cell.x + 1, cell.y + 7, format_number(GameState{}, day.date.day, 0x82), color);
        if (selected_source_day_ == day.season_day) {
            for (int x = cell.x; x < cell.x + cell.width; ++x) {
                out.at(x,cell.y + 1) = 0xf7; out.at(x,cell.y + 7) = 0xf7;
            }
            for (int y = cell.y + 1; y <= cell.y + 7; ++y) {
                out.at(cell.x,y) = 0xf7; out.at(cell.x + 10,y) = 0xf7;
            }
        }
    }
    if (selected_day_ >= 0) {
        const auto& day = state_.days[size_t(selected_day_)];
        const auto cell = calendar_cell(day);
        // InclusiveHoverOutlineAtYHitThroughYHit's inclusive hover outline at yHit+1 through yHit+7.
        const uint8_t hover_color = (day.raw_entry & 0x8000) || day.reschedule_target || day.friendly_target ? 0x75 : 0x27;
        for (int x = cell.x; x < cell.x + cell.width; ++x) {
            out.at(x,cell.y + 1) = hover_color; out.at(x,cell.y + 7) = hover_color;
        }
        for (int y = cell.y + 1; y <= cell.y + 7; ++y) {
            out.at(cell.x,y) = hover_color; out.at(cell.x + 10,y) = hover_color;
        }
        const auto footer = original_to_utf8(information(day));
        sprite_font_.draw(out, 5, 235, footer);
    }
    return out;
}

bool CalendarScreen::move(int x, int y) {
    if (close_requested_) return false;
    // Source removes the hover outline and footer when leaving all day cells.
    for (size_t index = 0; index < state_.days.size(); ++index) {
        const auto& day = state_.days[index];
        const auto cell = calendar_cell(day);
        if (x >= cell.x && x < cell.x + cell.width && y >= cell.y && y < cell.y + cell.height) {
            if (selected_day_ == int(index)) return false;
            selected_day_ = int(index); return true;
        }
    }
    const bool changed = selected_day_ != -1;
    selected_day_ = -1;
    return changed;
}

bool CalendarScreen::click(int x, int y, bool right) {
    if (close_requested_) return false;
    if (right) { close_requested_ = true; return true; }
    const bool changed = move(x,y);
    if (selected_day_ >= 0) {
        const auto& day = state_.days[size_t(selected_day_)];
        if (state_.reschedule_manager >= 0) {
            if (pending_reschedule_) return changed;
            if (selected_source_day_ >= 0) {
                const auto option = std::find_if(state_.reschedule_options.begin(), state_.reschedule_options.end(),
                    [&](const RescheduleOption& item) { return item.match.day == selected_source_day_; });
                if (option != state_.reschedule_options.end()) {
                    if (day.season_day == selected_source_day_) { selected_source_day_ = -1; return true; }
                    if (std::find(option->target_days.begin(), option->target_days.end(), day.season_day) != option->target_days.end()) {
                        pending_reschedule_ = RescheduleAction{owner_, RescheduleActionKind::move,
                            option->match, day.season_day};
                        return true;
                    }
                    selected_source_day_ = -1;
                }
            }
            const auto moved = std::find_if(state_.moved_matches.begin(), state_.moved_matches.end(),
                [&](const LeagueMatchRef& match) { return match.day == day.season_day; });
            if (moved != state_.moved_matches.end()) {
                pending_reschedule_ = RescheduleAction{owner_, RescheduleActionKind::undo, *moved, day.season_day};
                return true;
            }
            const auto source = std::find_if(state_.reschedule_options.begin(), state_.reschedule_options.end(),
                [&](const RescheduleOption& item) { return item.match.day == day.season_day; });
            if (source != state_.reschedule_options.end() && !source->target_days.empty()) {
                selected_source_day_ = day.season_day;
                return true;
            }
            return changed;
        }
        const bool own_league = active_calendar_class_ >= 0 && active_calendar_class_ < 4 &&
            (day.competitions & (uint32_t(1) << active_calendar_class_));
        // Source local30 is also set by the DFB/rearranged-fixture bit14.
        if ((day.raw_entry & 0x7fff) && (own_league || (day.competitions & (uint32_t(1) << 14)))) {
            close_requested_ = true; return true;
        }
    }
    return changed;
}

void CalendarScreen::acknowledge_reschedule(CalendarState state, DeskRequest owner) {
    if (state.reschedule_manager != owner.manager_slot || owner.menu_kind != 7 ||
        owner.kind != DeskRequestKind::open_menu || owner.revision <= owner_.revision || !pending_reschedule_)
        throw FormatError("calendar: invalid reschedule acknowledgement");
    state_ = std::move(state);
    owner_ = owner;
    selected_source_day_ = -1;
    pending_reschedule_.reset();
}

void CalendarScreen::reject_reschedule() {
    if (!pending_reschedule_) throw FormatError("calendar: no pending reschedule request");
    pending_reschedule_.reset();
    selected_source_day_ = -1;
}

bool CalendarScreen::key(std::string_view value) {
    if (close_requested_ || value != "ESC") return false;
    close_requested_ = true; return true;
}

std::string CalendarScreen::state_json() const {
    std::ostringstream out;
    out << "{\"screen\":\"" << (state_.reschedule_manager >= 0 ? "reschedule_calendar" : "calendar")
        << "\",\"season_start_counter\":" << state_.season_start_counter
        << ",\"day_count\":" << state_.days.size() << ",\"current_day\":" << state_.current_day
        << ",\"selected_day\":" << selected_day_ << ",\"selected_source_day\":" << selected_source_day_
        << ",\"reschedule_options\":" << state_.reschedule_options.size()
        << ",\"moved_matches\":" << state_.moved_matches.size()
        << ",\"close_requested\":" << (close_requested_ ? "true" : "false");
    if (state_.reschedule_manager >= 0) {
        out << ",\"source_days\":[";
        for (size_t i = 0; i < state_.reschedule_options.size(); ++i) {
            if (i) out << ',';
            const auto& option = state_.reschedule_options[i];
            out << "{\"day\":" << option.match.day << ",\"targets\":[";
            for (size_t j = 0; j < option.target_days.size(); ++j) {
                if (j) out << ',';
                out << option.target_days[j];
            }
            out << "]}";
        }
        out << "],\"undo_days\":[";
        for (size_t i = 0; i < state_.moved_matches.size(); ++i) {
            if (i) out << ',';
            out << state_.moved_matches[i].day;
        }
        out << ']';
    }
    if (selected_day_ >= 0) {
        const auto& day = state_.days[size_t(selected_day_)];
        out << ",\"day_counter\":" << day.day_counter << ",\"raw_entry\":" << day.raw_entry
            << ",\"competitions\":" << day.competitions << ",\"league_matchdays\":[";
        for (size_t i = 0; i < day.league_matchdays.size(); ++i) { if (i) out << ','; out << day.league_matchdays[i]; }
        out << "],\"information\":" << json_quote(original_to_utf8(information(day)));
    }
    out << '}'; return out.str();
}

} // namespace bmh::ui
