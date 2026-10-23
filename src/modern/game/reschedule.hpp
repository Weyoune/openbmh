// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/reschedule.hpp"
// Rescheduling a home league match (desk menu 7, "Verlegen", H2 p. 49; BMH.exe V2.24 +
// supported rule profile patch). The original does it on the calendar screen CalendarCell with mode 2:
//
//   1. The manager clicks a matchday of his own league. The match of his club on that day
//      is selected when it is a home match, still in the future, not on the first matchday
//      and not on one of the last two matchdays.
//   2. The calendar then marks the days the match can move to (draw loop of CalendarCell,
//      see reschedule_target_days). With floodlights (manager record +0x0e) the day before
//      the matchday (the Friday) is offered as well.
//   3. A click on a marked day moves the match with RescheduleLeagueMatch(day, league, match, slot,
//      -0x32, round): the match becomes a "replay" on that day (postponed list Postponed),
//      its result byte becomes 0xCE and the calendar day gets bit 0x4000. On the original
//      matchday the 0xCE turns into 0xCD (matchday_setup) and the match is played on the
//      new day like a winter postponement.
//   4. A click on the new day of a moved match takes the move back (UndoReschedule(day, 1,
//      team)): the entry is freed and the result byte is 0xCC again. The calendar keeps bit
//      0x4000, as in the original.
//
// Everything here works on the saved ModernState only: no random numbers are drawn, so a
// rescheduled career stays deterministic.

#include <cstdint>
#include <vector>

#include "modern/game/modern_state.hpp"

namespace bmh::modern {

// A league match: day of the season (index into the calendar CompetitionCalendar), the 0-based
// league round, the league and the index of the match in the fixture list of that round.


// The own home league matches that can be moved: future days, rounds 1 .. matchdays - 3
// (0-based), result byte still 0xCC (not played, not postponed). Only clubs of leagues
// 0-4 (calendar bits 0-3) take part: the lower leagues have no own calendar bit.
std::vector<LeagueMatchRef> reschedulable_matches(const ModernState& g, int manager);

// The days `match` can be moved to (the days CalendarCell marks in mode 2), ascending.
// Days that already hold a replay of one of the teams in the same league are left out, as
// the second click refuses them (RescheduleTargetDays).
std::vector<int> reschedule_target_days(const ModernState& g, int manager, const LeagueMatchRef& match);

// Moves `match` to `day` (RescheduleLeagueMatch with param_5 = -0x32). Returns moved on success; on
// failure nothing is changed.
RescheduleStatus reschedule_league_match(ModernState& g, int manager, const LeagueMatchRef& match, int day);

// The own matches moved with reschedule_league_match that can still be taken back: their new
// day (`day`) is in the future, the result byte is 0xCE and the round is one the screen
// accepts (1 .. matchdays - 3). Winter postponements (0xCD) are not listed. Like the
// original the club need not be the home side: in a hot seat game the opponent's manager can
// take back a move as well.
std::vector<LeagueMatchRef> moved_matches(const ModernState& g, int manager);

// Takes back the move of the club's match on `day` (one of moved_matches) - UndoReschedule with
// param_2 = 1: every replay on that day with the club gets result byte 0xCC and its entry is
// freed. Returns false if there is no such match.
bool undo_reschedule(ModernState& g, int manager, int day);

}  // namespace bmh::modern
