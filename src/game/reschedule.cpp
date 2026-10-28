// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/reschedule.hpp"

#include <algorithm>

#include "game/matchday_setup.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"

namespace bmh {

namespace {

constexpr uint32_t kCalendar = ::bmh::layout::CompetitionCalendar;   // u16 per season day
constexpr int kCalendarDays = 0x16e;       // CalendarCell copies 0x16e words
constexpr uint32_t kPostponed = ::bmh::layout::Postponed;   // 40 entries: u16 day, round, league, match
constexpr int kPostponedEntries = 0x28;
constexpr uint32_t kFriendlyTeams = ::bmh::layout::FriendlyTeams;  // friendly plan: team pairs
constexpr uint32_t kFriendlyDays = ::bmh::layout::FriendlyDays;   // friendly plan: days, 30 entries
constexpr int kFriendlyEntries = 30;
// MatchdaySetupRuntimeField3 / MatchdaySetupRuntimeField4: first and last day of the winter break.
constexpr int kWinterBreakFirst = 0x96, kWinterBreakLast = 0xc8;
// RescheduleLeagueMatch accepts replay days up to 0x11e; the draw loop wants the day after the
// target inside 0x15 .. 0x11e.
constexpr int kLastReplayDay = 0x11e, kFirstDayAfter = 0x15;
constexpr uint8_t kRescheduledMark = 0xce;  // param_5 = -0x32
constexpr size_t kRecordSize = 0xb0c;
constexpr size_t kFloodlights = 0x0e;  // manager record byte (RescheduleRuntimeField)
// LeagueCalendarBits: calendar bit of a league (the two leagues 2 and 3 share bit 2, the four
// leagues of 12 clubs bit 4; compare kGroupLeague in matchday_setup.cpp).
// (kLeagueBit: schedule.hpp, set from the groups of the loaded game; 0xff = not played)

size_t result_at(int league, int round, int match) { return size_t(match) + size_t(round) * 10 + size_t(league) * 0x17c; }

bool memory_ok(const GameState& g) {
    return g.memory.has(kCalendar + kCalendarDays * 2 - 1) &&
           g.memory.has(kPostponed + kPostponedEntries * 6 - 1) && g.memory.has(kFriendlyDays + kFriendlyEntries * 2 - 1) &&
           g.memory.has(kFriendlyTeams + kFriendlyEntries * 4 - 1);
}

// The club of a human manager in league play, or -1.
int manager_team(const GameState& g, int manager) {
    if (manager < 0 || size_t(manager) >= g.manager_teams.size()) return -1;
    const int team = g.manager_teams[size_t(manager)];
    if (team < 0 || team_league(team) >= kLeagueCount) return -1;
    return team;
}

std::vector<Fixture> round_fixtures(int league, int round) {
    return generate_fixtures(league_table_teams(league), round + 1, league_matchdays(league) / 2, league_first_team(league));
}

// DayConflict(team, other, word, day): 1 when `day` (calendar word `word` of the screen's
// copy) cannot take a match of `team`/`other`: the word is taken (anything but 0, 0x4001 or
// a word marked eligible with bit 15) or the friendly plan already has a match of one of the
// teams - or of the national team - on that day.
bool day_conflict(const GameState& g, int team, int other, uint16_t word, int day) {
    if (word != 0 && word != 0x4001 && (word & 0x8000) == 0) return true;
    for (int i = 0; i < kFriendlyEntries; i++) {
        if (g.memory.u16(kFriendlyDays + uint32_t(i) * 2) != uint16_t(day)) continue;
        for (int side = 0; side < 2; side++) {
            const int t = int16_t(g.memory.u16(kFriendlyTeams + uint32_t(i) * 4 + uint32_t(side) * 2));
            if (t == kNoTeam || t == team || t == other) return true;
        }
    }
    return false;
}

// RescheduleTargetDays(day, team, league): entry + 1 of a replay of `team` in `league` on `day`, or 0
// (the last such entry, as the original keeps looking).
int team_replay_on(const GameState& g, int day, int team, int league) {
    int found = 0;
    for (int e = 0; e < kPostponedEntries; e++) {
        const uint32_t at = kPostponed + uint32_t(e) * 6;
        if (g.memory.u16(at) != uint16_t(day) || g.memory.u8(at + 3) != league) continue;
        const Fixture f = round_fixtures(league, g.memory.u8(at + 2)).at(g.memory.u8(at + 4));
        if (f.home == team || f.away == team) found = e + 1;
    }
    return found;
}

// The draw loop of CalendarCell in mode 2 on a copy of the calendar: a future day is marked
// (bit 15) when it is free (0, 0x4001 or a calendar event 0x1c0), neither it nor its two
// neighbours conflict for the club (the neighbours before it may already be marked), the
// day after it lies in 0x15 .. 0x11e and it is not in the winter break. The national team
// (manager 0 coaching team 0x165) also checks the club of the Bundestrainer, CupDraw_FormatError_RuntimeField3.
std::vector<uint16_t> marked_calendar(const GameState& g, int team) {
    std::vector<uint16_t> cal(kCalendarDays);
    for (int d = 0; d < kCalendarDays; d++) cal[size_t(d)] = g.memory.u16(kCalendar + uint32_t(d) * 2);
    // DaysIntoSeason, the day of the season; its saved copy in g.memory is only the load day
    const int today = g.season_day();
    const bool national = !g.manager_teams.empty() && g.manager_teams[0] == kNoTeam && g.memory.has(::bmh::layout::DeskActions_NationalOpponentManager_RuntimeField);
    for (int d = today + 1; d < kCalendarDays; d++) {
        const uint16_t word = cal[size_t(d)];
        if (word != 0 && word != 0x4001 && (word & 0x1c0) == 0) continue;
        bool ok = true;
        for (int i = std::max(0, d - 1); i <= std::min(kCalendarDays - 1, d + 1); i++) {
            if (day_conflict(g, team, -1, cal[size_t(i)], i)) ok = false;
            if (national && day_conflict(g, int16_t(g.memory.u16(::bmh::layout::CupDraw_FormatError_RuntimeField3)), -1, cal[size_t(i)], i)) ok = false;
        }
        if (d + 1 > kLastReplayDay || d + 1 < kFirstDayAfter) ok = false;
        if (d >= kWinterBreakFirst && d <= kWinterBreakLast) ok = false;
        if (team == kNoTeam && (word & 0x1c0) != 0) ok = false;
        if (ok) cal[size_t(d)] |= 0x8000;
    }
    return cal;
}

}  // namespace

std::vector<LeagueMatchRef> reschedulable_matches(const GameState& g, int manager) {
    std::vector<LeagueMatchRef> out;
    const int team = manager_team(g, manager);
    if (team < 0 || !memory_ok(g)) return out;
    const int league = team_league(team);
    // The screen only reacts to the calendar bit of the club's league for bits 0-3
    // (local_34 == bit); CalendarCell derives local_34 as league - (league > 2).
    const int screen_bit = league > 2 ? league - 1 : league;
    if (screen_bit > 3 || kLeagueBit[league] != screen_bit) return out;
    // DaysIntoSeason, the day of the season; its saved copy in g.memory is only the load day
    const int today = g.season_day();
    const int matchdays = league_matchdays(league);
    int round = -1;
    for (int day = 0; day < kCalendarDays; day++) {
        const uint32_t comps = day_competitions(g, g.memory.u16(kCalendar + uint32_t(day) * 2));
        if (kLeagueBit[league] > 7 || !((comps >> kLeagueBit[league]) & 1)) continue;
        ++round;  // local_58 - 1: the league days up to this one
        if (day <= today || round == 0 || round + 2 >= matchdays) continue;
        const std::vector<Fixture> fixtures = round_fixtures(league, round);
        for (size_t m = 0; m < fixtures.size(); m++) {
            const Fixture& f = fixtures[m];
            if (f.home != team && f.away != team) continue;
            // the screen picks the club's match of the day; only a home match can be moved
            if (f.home == team && g.results.at(result_at(league, round, int(m))) == kNotPlayed)
                out.push_back({day, round, league, int(m), f.home, f.away});
            break;
        }
    }
    return out;
}

std::vector<int> reschedule_target_days(const GameState& g, int manager, const LeagueMatchRef& match) {
    std::vector<int> out;
    const int team = manager_team(g, manager);
    if (team < 0 || !memory_ok(g)) return out;
    const std::vector<LeagueMatchRef> movable = reschedulable_matches(g, manager);
    if (std::none_of(movable.begin(), movable.end(), [&](const LeagueMatchRef& m) {
            return m.day == match.day && m.round == match.round && m.league == match.league && m.match == match.match;
        }))
        return out;
    std::vector<uint16_t> cal = marked_calendar(g, team);
    // floodlights: the day before the matchday (the Friday) is offered as well
    if ((size_t(manager) + 1) * kRecordSize <= g.manager_records.size() &&
        g.manager_records[size_t(manager) * kRecordSize + kFloodlights] != 0 && match.day > 0)
        cal[size_t(match.day - 1)] |= 0x8000;
    for (int d = 0; d < kCalendarDays; d++)
        if ((cal[size_t(d)] & 0x8000) && d != match.day && d <= kLastReplayDay && team_replay_on(g, d, team, match.league) == 0)
            out.push_back(d);
    return out;
}

RescheduleStatus reschedule_league_match(GameState& g, int manager, const LeagueMatchRef& match, int day) {
    if (manager_team(g, manager) < 0 || !memory_ok(g)) return RescheduleStatus::invalid_manager;
    const std::vector<LeagueMatchRef> movable = reschedulable_matches(g, manager);
    const auto it = std::find_if(movable.begin(), movable.end(), [&](const LeagueMatchRef& m) {
        return m.day == match.day && m.round == match.round && m.league == match.league && m.match == match.match;
    });
    if (it == movable.end()) return RescheduleStatus::invalid_match;
    const std::vector<int> days = reschedule_target_days(g, manager, *it);
    if (std::find(days.begin(), days.end(), day) == days.end()) return RescheduleStatus::invalid_day;
    // the first free entry of the postponed list
    int slot = -1;
    for (int e = 0; e < kPostponedEntries && slot < 0; e++)
        if (g.memory.u16(kPostponed + uint32_t(e) * 6) == 0) slot = e;
    if (slot < 0) return RescheduleStatus::no_free_slot;
    // RescheduleLeagueMatch(day, league, match, slot, -0x32, round): eight replays on the day or a
    // team clash with one of them refuse the move. Like the winter mode the original builds
    // the other replay's fixtures with the parameters of this league and compares the low
    // bytes of the team ids only.
    const int league = it->league;
    const Fixture own = round_fixtures(league, it->round).at(size_t(it->match));
    int same_day = 0;
    for (int e = 0; e < kPostponedEntries; e++) {
        const uint32_t at = kPostponed + uint32_t(e) * 6;
        if (g.memory.u16(at) != uint16_t(day)) continue;
        if (++same_day == 8) return RescheduleStatus::day_full;
        const Fixture other = round_fixtures(league, g.memory.u8(at + 2)).at(g.memory.u8(at + 4));
        const uint8_t oh = uint8_t(other.home), oa = uint8_t(other.away), fh = uint8_t(own.home), fa = uint8_t(own.away);
        if (fh == oh || fa == oa || fa == oh || fh == oa) return RescheduleStatus::team_clash;
    }
    const uint32_t at = kPostponed + uint32_t(slot) * 6;
    g.memory.set16(at, uint16_t(day));
    g.memory.set8(at + 2, uint8_t(it->round));
    g.memory.set8(at + 3, uint8_t(league));
    g.memory.set8(at + 4, uint8_t(it->match));
    g.results.at(result_at(league, it->round, it->match)) = kRescheduledMark;
    const uint32_t high = kCalendar + uint32_t(day) * 2 + 1;
    g.memory.set8(high, g.memory.u8(high) | 0x40);
    return RescheduleStatus::moved;
}

std::vector<LeagueMatchRef> moved_matches(const GameState& g, int manager) {
    std::vector<LeagueMatchRef> out;
    const int team = manager_team(g, manager);
    if (team < 0 || !memory_ok(g)) return out;
    const int league = team_league(team);
    const int today = g.season_day();
    // CalendarCell: a click on a replay day (calendar bit 0xe) finds the club's replay with
    // RescheduleTargetDays(day, team, league) and calls UndoReschedule when its result byte is 0xCE
    for (int e = 0; e < kPostponedEntries; e++) {
        const uint32_t at = kPostponed + uint32_t(e) * 6;
        const int day = g.memory.u16(at);
        if (day == 0 || day <= today || g.memory.u8(at + 3) != league) continue;
        if (team_replay_on(g, day, team, league) != e + 1) continue;
        const int round = g.memory.u8(at + 2), m = g.memory.u8(at + 4);
        if (round == 0 || round + 2 >= league_matchdays(league)) continue;
        if ((g.results.at(result_at(league, round, m)) & 0xcf) != kRescheduledMark) continue;
        const Fixture f = round_fixtures(league, round).at(size_t(m));
        out.push_back({day, round, league, m, f.home, f.away});
    }
    std::sort(out.begin(), out.end(), [](const LeagueMatchRef& a, const LeagueMatchRef& b) { return a.day < b.day; });
    return out;
}

bool undo_reschedule(GameState& g, int manager, int day) {
    const std::vector<LeagueMatchRef> moved = moved_matches(g, manager);
    if (std::none_of(moved.begin(), moved.end(), [&](const LeagueMatchRef& m) { return m.day == day; })) return false;
    const int team = manager_team(g, manager);
    // UndoReschedule(day, 1, team): every replay of the club on that day, whatever its league
    int kept = 0;  // local_18: the replays of other clubs that stay on the day
    for (int e = 0; e < kPostponedEntries; e++) {
        const uint32_t at = kPostponed + uint32_t(e) * 6;
        if (g.memory.u16(at) != uint16_t(day)) continue;
        const int league = g.memory.u8(at + 3), round = g.memory.u8(at + 2), m = g.memory.u8(at + 4);
        const Fixture f = round_fixtures(league, round).at(size_t(m));
        if (f.home != team && f.away != team) { ++kept; continue; }
        g.results.at(result_at(league, round, m)) = kNotPlayed;
        g.memory.set16(at, 0);
    }
    // with no replay left the day loses its replay bit 0x4000 again (calendar high byte & 0xbf),
    // so it can be a target once more
    if (kept == 0) {
        const uint32_t high = kCalendar + uint32_t(day) * 2 + 1;
        g.memory.set8(high, uint8_t(g.memory.u8(high) & 0xbf));
    }
    return true;
}

}  // namespace bmh
