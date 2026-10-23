// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// League dates of a Modern world in the season calendar (docs/new_engine, M2).
//
// The season calendar (CompetitionCalendar) has one 16-bit entry per season day, day 0 being
// 1 July. Bits 0-4 start the league matchdays of the calendar groups; entries with bit 14 set
// are special days (indoor tournament, season change, ...) whose low bits mean something
// else. A Modern world gives the league dates of a group as a list of dates; this module
// replaces the group's bit on the ordinary days by those dates.

#include <string>
#include <vector>

#include "modern/game/modern_state.hpp"

namespace bmh::modern {

// Season day (0 = 1 July of `start_year`) of an ISO date "YYYY-MM-DD"; -1 for a malformed date.
int season_day_of(const std::string& date, int start_year);

struct LeagueDatesResult {
    int dates = 0;                      // dates placed
    std::vector<std::string> moved;     // dates moved to the next free ordinary day
};

// Sets `calendar_bit` (0-4) exactly on the given season days: the bit is cleared on every
// ordinary day first; a date on a special day (bit 14) or on a day that already has the bit
// moves to the next ordinary day. Throws std::invalid_argument when a date cannot be placed
// within the 366 days or the calendar is not loaded.
LeagueDatesResult install_league_dates(ModernState& g, int calendar_bit, const std::vector<int>& season_days);

}  // namespace bmh::modern
