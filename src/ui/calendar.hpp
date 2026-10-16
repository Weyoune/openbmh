// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/font.hpp"
#include "formats/textd.hpp"
#include "ui/calendar_state.hpp"
#include "ui/desktop.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {

enum class RescheduleActionKind : uint8_t { move, undo };
struct RescheduleAction {
    DeskRequest owner{};
    RescheduleActionKind kind = RescheduleActionKind::move;
    LeagueMatchRef match{};
    int target_day = -1;
    bool operator==(const RescheduleAction&) const = default;
};

struct CalendarCell { int x = 0, y = 0, width = 11, height = 8; };
// CalendarCell: weekdays descend, weeks advance horizontally, July through June.
CalendarCell calendar_cell(const CalendarDayProjection& day);

class CalendarScreen {
public:
    CalendarScreen(const std::filesystem::path& data, CalendarState state,
                   std::string manager_name = {}, bool decorated_background = true, int runtime_club = -1,
                   DeskRequest owner = {});
    Image render() const;
    bool move(int x, int y);
    bool click(int x, int y, bool right = false);
    bool key(std::string_view value);
    bool close_requested() const { return close_requested_; }
    const CalendarState& state() const { return state_; }
    int selected_day() const { return selected_day_; }
    const std::optional<RescheduleAction>& pending_reschedule() const { return pending_reschedule_; }
    const DeskRequest& owner() const { return owner_; }
    void acknowledge_reschedule(CalendarState state, DeskRequest owner);
    void reject_reschedule();
    std::string state_json() const;
private:
    CalendarState state_;
    Image background_, atlas_;
    Font font_;
    SpriteFont sprite_font_;
    LeImage exe_;
    TextDb texts_;
    bool decorated_background_ = true, close_requested_ = false;
    int selected_day_ = -1, active_calendar_class_ = -1;
    DeskRequest owner_{};
    int selected_source_day_ = -1;
    std::optional<RescheduleAction> pending_reschedule_;
    void text(Image&, int x, int baseline, std::string_view bytes, uint8_t color) const;
    std::string information(const CalendarDayProjection&) const;
};

} // namespace bmh::ui
