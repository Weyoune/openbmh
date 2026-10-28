#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// Moving a home league match (desk menu 7, src/game/reschedule.hpp) on a real savegame:
// the offered matches and days follow the rules of legacy routine mode 2, and a move writes
// the postponed entry, the result byte 0xCE and calendar bit 0x4000 like legacy routine.

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "formats/fbm.hpp"
#include "formats/le.hpp"
#include "formats/savestate.hpp"
#include "game/career.hpp"
#include "game/reschedule.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"

using namespace bmh;

namespace {

void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

void reschedule_test(GameState g) {
    const int team = g.manager_teams.at(0);
    const int league = team_league(team);
    const int today = g.season_day();
    const auto matches = reschedulable_matches(g, 0);
    check(!matches.empty(), "the manager has home matches to move");
    for (const LeagueMatchRef& m : matches) {
        check(m.home == team && m.league == league, "only own home matches are offered");
        check(m.day > today && m.round > 0 && m.round + 2 < league_matchdays(league), "only future middle rounds");
        check(g.results.at(size_t(m.match) + size_t(m.round) * 10 + size_t(league) * 0x17c) == kNotPlayed, "unplayed");
    }
    const LeagueMatchRef m = matches.front();
    const auto days = reschedule_target_days(g, 0, m);
    check(!days.empty(), "the match has free days");
    for (int d : days) {
        const uint16_t word = g.memory.u16(::bmh::layout::CompetitionCalendar + uint32_t(d) * 2);
        check(d > today && d != m.day && d + 1 <= 0x11e && d + 1 >= 0x15, "target day in range");
        check(d < 0x96 || d > 0xc8, "no target in the winter break");
        check(word == 0 || word == 0x4001 || (word & 0x1c0) != 0 || d == m.day - 1, "target day is free");
    }
    // floodlights add the Friday before the matchday
    GameState lit = g;
    lit.manager_records.at(0x0e) = 1;
    const auto lit_days = reschedule_target_days(lit, 0, m);
    check(std::find(lit_days.begin(), lit_days.end(), m.day - 1) != lit_days.end(), "floodlights offer the day before");

    check(reschedule_league_match(g, 0, m, m.day) == RescheduleStatus::invalid_day, "the matchday itself is no target");
    const int target = days.front();
    int free_slot = -1;
    for (int e = 0; e < 40 && free_slot < 0; e++)
        if (g.memory.u16(::bmh::layout::Postponed + uint32_t(e) * 6) == 0) free_slot = e;
    check(reschedule_league_match(g, 0, m, target) == RescheduleStatus::moved, "the match is moved");
    const uint32_t at = ::bmh::layout::Postponed + uint32_t(free_slot) * 6;
    check(g.memory.u16(at) == target && g.memory.u8(at + 2) == m.round && g.memory.u8(at + 3) == league &&
              g.memory.u8(at + 4) == m.match, "postponed entry written");
    check(g.results.at(size_t(m.match) + size_t(m.round) * 10 + size_t(league) * 0x17c) == 0xce, "result byte 0xCE");
    check((g.memory.u16(::bmh::layout::CompetitionCalendar + uint32_t(target) * 2) & 0x4000) != 0, "calendar replay bit");
    const auto after = reschedulable_matches(g, 0);
    check(std::none_of(after.begin(), after.end(), [&](const LeagueMatchRef& r) { return r.day == m.day; }),
          "a moved match is not offered again");
    check(reschedule_league_match(g, 0, m, target) == RescheduleStatus::invalid_match, "no second move");
    const auto moved = moved_matches(g, 0);
    check(moved.size() == 1 && moved.front().day == target && moved.front().match == m.match, "the move can be taken back");
    GameState undone = g;
    check(!undo_reschedule(undone, 0, m.day), "no undo on the old matchday");
    check(undo_reschedule(undone, 0, target), "undo");
    check(undone.memory.u16(at) == 0 && undone.results.at(size_t(m.match) + size_t(m.round) * 10 + size_t(league) * 0x17c) == kNotPlayed,
          "undo frees the entry and restores 0xCC");
    check(reschedulable_matches(undone, 0).size() == matches.size(), "the match is movable again");
    // UndoReschedule clears the replay bit when no other replay stays on the day: the same
    // target days as before the move (issue 16)
    check((undone.memory.u16(::bmh::layout::CompetitionCalendar + uint32_t(target) * 2) & 0x4000) == 0,
          "undo clears the calendar replay bit");
    check(reschedule_target_days(undone, 0, m) == days, "the same target days after the undo");
    std::cout << "moved round " << m.round + 1 << " from day " << m.day << " to day " << target << " (" << days.size()
              << " days offered)\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 3, "usage: reschedule_tests ORIGINAL_DATA SAVEGAME");
        if (!*argv[1]) {
            std::cout << "reschedule tests: skipped (Original data directory not configured)\n";
            return 0;
        }
        const auto exe = bmh::test::load_executable(std::filesystem::path(argv[1]) / "BMH.exe");
        const auto file = load_savegame(argv[2]);
        const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
        GameState g = GameState::from_save(SaveState(file.stream, counts), counts);
        competition_pointers(g.memory, exe);
        reschedule_test(g);
        std::cout << "reschedule tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "reschedule tests: " << error.what() << "\n";
        return 1;
    }
}
