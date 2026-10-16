// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "ui/desk_state.hpp"
#include "game/reschedule.hpp"

namespace bmh {
struct GameState;
class LeImage;
}

namespace bmh::ui {

// Read-only projection of one saved day in the Source July-to-June calendar.
// raw_entry keeps the full saved word; competitions is the effective Source bitmap.
struct CalendarDayProjection {
    int season_day = 0; // Native calendar-word index; negative for July cells before Source day zero.
    uint32_t day_counter = 0;
    DeskDate date;
    uint16_t raw_entry = 0;
    uint32_t competitions = 0;
    std::array<int, 9> league_matchdays{}; // 1-based ordinal; zero when this league does not play.
    bool current_day = false;
    bool reschedule_source = false;
    bool reschedule_target = false;
    bool reschedule_undo = false;
    bool friendly_target = false;
};

struct RescheduleOption {
    LeagueMatchRef match;
    std::vector<int> target_days;
};

struct CalendarState {
    // Display dates begin on July 1, while Source calendar word zero begins on
    // the first Wednesday of the season. leading_days bridges those indices.
    uint32_t season_start_counter = 0;
    int leading_days = 0;
    int current_day = -1; // Display-vector index, including leading July cells.
    std::vector<CalendarDayProjection> days;
    int reschedule_manager = -1;
    bool friendly_selection = false;
    std::vector<RescheduleOption> reschedule_options;
    std::vector<LeagueMatchRef> moved_matches;
};

// Projects the current saved season without preparing fixtures, advancing the date,
// changing calendar words, or consuming either random generator.
CalendarState calendar_from_game(const GameState& game, const LeImage& executable);
CalendarState reschedule_calendar_from_game(const GameState& game, const LeImage& executable, int manager);

} // namespace bmh::ui
