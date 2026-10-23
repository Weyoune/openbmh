// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "modern/game/modern_state.hpp"
#include "modern/game/cup.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "modern/game/driver.hpp"
#include "game/lineup.hpp"
#include "modern/game/matchday.hpp"
#include "modern/game/matchday_setup.hpp"
#include "game/messages.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"

namespace bmh::modern {

// After the fifth indoor tournament (the end of AdvanceCup for competition 0xb): the
// field of the Masters.
//   MastersQualification2: the holders of the three titles of last season (Cup_AdvanceCup_RuntimeField6[0..2],
//   u16 club ids) qualify directly, each once (list Cup_MastersQualification_RuntimeField, count CupRuntimeField);
//   they head the team list (Teams).
//   IndoorStandingsFromGame(0): the clubs 0-39 are sorted by their Masters points (Cup_AdvanceCup_ExecutableField,
//   Games descending); IndoorTournamentDraw only resets the tournament (no draw while
//   CupRuntimeField != 0); the best clubs not qualified yet fill the list up to eight, which
//   is shuffled (PrepareEvent, one game draw per club). The strength class Cup_AdvanceCup_RuntimeField3
//   stays that of the last tournament.
// The test "qualified yet" (MastersQualification) scans Cup_MastersQualification_RuntimeField[0 .. CupRuntimeField), but
// CupRuntimeField also counts the clubs filled in from the points list, which never enter
// Cup_MastersQualification_RuntimeField: the scan runs over the stale entries 2 and 3 (0 unless an earlier Masters
// had three direct qualifiers) and from entry 4 into the invitation counters that follow
// (MatchdaySetup_IndoorTournamentDraw_RuntimeField, u16 per club). A club whose id equals one of those values (team 0 with a
// 0 there, small ids with an invitation count) is skipped although it has the points.
// The list is not in the savegame; the replay keeps it in memory, starting at 0.
void masters_qualification(ModernState& g, GameRandom& game) {
    StaticMemory& mem = g.memory;
    constexpr uint32_t kTeams = ::bmh::layout::Teams, kDirect = ::bmh::layout::Cup_MastersQualification_RuntimeField;
    if (!mem.has(kDirect)) mem.add_block(kDirect, 8);
    // MastersQualification: whether club t is in Cup_MastersQualification_RuntimeField[0 .. count)
    auto listed = [&](uint16_t t, size_t count) {
        for (size_t k = 0; k < count; k++)
            if (mem.u16(kDirect + uint32_t(k) * 2) == t) return true;
        return false;
    };
    size_t count = 0;  // CupRuntimeField
    for (uint32_t k = 0; k < 3; k++) {
        const uint16_t t = mem.u16(::bmh::layout::Cup_AdvanceCup_RuntimeField6 + k * 2);
        if (k == 0 || !listed(t, count)) mem.set16(kDirect + uint32_t(count++) * 2, t);
    }
    for (size_t k = 0; k < count; k++) mem.set16(kTeams + uint32_t(k) * 2, mem.u16(kDirect + uint32_t(k) * 2));
    int16_t order[0x28];
    uint32_t points[0x28];
    for (int i = 0; i < 0x28; i++) order[i] = int16_t(i), points[i] = mem.u16(::bmh::layout::Cup_AdvanceCup_ExecutableField + uint32_t(i) * 2);
    quicksort_by_key(order, points, 0x28);
    modern::indoor_tournament_reset(g);
    for (int i = 0; count < 8; i++) {
        const uint16_t t = uint16_t(order[i]);
        if (!listed(t, count)) mem.set16(kTeams + uint32_t(count++) * 2, t);
    }
    for (uint32_t i = 0; i < 8; i++) {  // PrepareEvent(8, teams)
        const uint32_t j = uint32_t(game.range(0, 7));
        const uint16_t a = mem.u16(kTeams + i * 2);
        mem.set16(kTeams + i * 2, mem.u16(kTeams + j * 2));
        mem.set16(kTeams + j * 2, a);
    }
    for (uint32_t k = 0; k < 8; k++) {
        const int manager = g.manager_of(mem.u16(kTeams + k * 2));
        if (manager < 0) continue;
        post_composed(g, manager, kMastersQualificationText);  // MastersQualificationText
        modern::book_money(g, manager, -150000, true, true);
    }
    const std::string name = message_text(g, ::bmh::layout::Cup_MastersQualification_TextPointer);
    if (g.tournament_name.size() < 0x24) g.tournament_name.resize(0x24, 0);
    for (size_t i = 0; i <= name.size() && i < 0x24; i++) g.tournament_name[i] = i < name.size() ? uint8_t(name[i]) : 0;
}

namespace {

void put32(Bytes& b, size_t o, uint32_t v) {
    for (int i = 0; i < 4; i++) b.at(o + i) = uint8_t(v >> (8 * i));
}

// Replays of postponed league matches: cup entry type 9 plays the matches whose replay day
// (table Postponed, 40 entries of 6 bytes: day, round, league, match) is today.
constexpr uint32_t kPostponed = ::bmh::layout::Postponed;
constexpr uint32_t kReplayFixtures = ::bmh::layout::ReplayFixtures;  // team list of the type 9 entry (home, away)
constexpr uint32_t kReplayResults = ::bmh::layout::ReplayResults;   // its result array (u16 per match)

// AddBlock: the fixtures of today's replays; the entry's team count (+0x1a) and played
// counter (+0x1d) restart.
void prepare_replays(ModernState& g, uint32_t e, int day) {
    StaticMemory& mem = g.memory;
    mem.add_block(kReplayFixtures, 0x50);
    mem.add_block(kReplayResults, 0x12);
    mem.set8(e + 0x1d, 0);
    mem.set8(e + 0x1a, 0);
    for (uint32_t i = 0; i < 0x12; i++) mem.set8(kReplayResults + i, 0xcc);
    int k = 0;
    for (int n = 0; n < 0x28; n++) {
        const uint32_t at = kPostponed + uint32_t(n) * 6;
        if (mem.u16(at) != day) continue;
        const int league = mem.u8(at + 3);
        const std::vector<Fixture> fx = generate_fixtures(league_table_teams(league), mem.u8(at + 2) + 1, league_matchdays(league) / 2,
                                                          league_first_team(league));
        const Fixture& f = fx.at(mem.u8(at + 4));
        mem.set16(kReplayFixtures + uint32_t(k++) * 2, uint16_t(f.home));
        mem.set16(kReplayFixtures + uint32_t(k++) * 2, uint16_t(f.away));
        mem.set8(e + 0x1a, uint8_t(mem.u8(e + 0x1a) + 2));
    }
}

// EndCompetition(id): takes a competition off the list of running ones (ActiveSelectors).
void remove_competition(StaticMemory& mem, int id) {
    for (int i = 0; i < 8; i++) {
        if (int8_t(mem.u8(::bmh::layout::ActiveSelectors + uint32_t(i))) != id) continue;
        for (int j = i; j < 7; j++) mem.set8(::bmh::layout::ActiveSelectors + uint32_t(j), mem.u8(::bmh::layout::Cup_RemoveCompetition_RuntimeField + uint32_t(j)));
        mem.set8(::bmh::layout::Cup_RemoveCompetition_RuntimeField2, 0x14);
        return;
    }
}

// UndoReschedule(day, 0, 0): today's replay results go back into the league result table and
// their entries are freed; then EndCompetition(9) takes the competition off the list of
// running competitions (ActiveSelectors).
void store_replays(ModernState& g, int day) {
    StaticMemory& mem = g.memory;
    int k = 0;
    for (int n = 0; n < 0x28; n++) {
        const uint32_t at = kPostponed + uint32_t(n) * 6;
        if (mem.u16(at) != day) continue;
        const uint16_t r = mem.u16(kReplayResults + uint32_t(k++) * 2);
        const uint8_t result = uint8_t(((r >> 5) << 4) | (r & 0xf));
        g.results.at(size_t(mem.u8(at + 4)) + size_t(mem.u8(at + 2)) * 10 + size_t(mem.u8(at + 3)) * 0x17C) = result;
        mem.set16(at, 0);
    }
    remove_competition(mem, 9);
}

// Size of the round string table a cup group plays with (pointer at entry +8).
int group_table_teams(const StaticMemory& mem, uint32_t e) {
    switch (mem.pointer(e + 8)) {
        case ::bmh::layout::CareerSession_RuntimeError_RuntimeField: return 4;
        case ::bmh::layout::CareerSession_RuntimeError_RuntimeField2: return 6;
        case ::bmh::layout::CareerSession_RuntimeError_RuntimeField3: return 12;
        case ::bmh::layout::CareerSession_RuntimeError_ExecutableField3: return 16;
        case ::bmh::layout::CareerSession_RuntimeError_ExecutableField: return 18;
        case ::bmh::layout::CareerSession_RuntimeError_ExecutableField2: return 20;
    }
    return 0;
}

// AddResult(entry, 0): the table of a cup group from its result array (u16 per match,
// 0xCCCC = not played), indexed by draw position. Bit 7 of the flags: three points for a
// win. The ranks are sorted like the league tables (AddResult2) after the last two rounds
// and after later rounds with results.
LeagueTable group_table(const StaticMemory& mem, uint32_t e) {
    LeagueTable t;
    const uint8_t flags = mem.u8(e + 0x14);
    const int teams = mem.u8(e + 0x1a), rounds = mem.u8(e + 0x1b), played = mem.u8(e + 0x1d);
    t.size = teams;
    const int three_points = flags >> 7;
    int last = played - 1, before_last = 0x80;
    if (last > 1) before_last = played - 2;
    if (last < 0) last = 0;
    sort_table(t, 0);
    const uint32_t results = mem.pointer(e + 0x0c);
    for (int r = 0; r < rounds; r++) {
        const std::vector<Fixture> fx = generate_fixtures(group_table_teams(mem, e), r + 1, rounds / ((flags & 1) + 1), 0);
        bool counted = false;
        for (int s = 0; s < teams; s += 2) {
            const uint16_t u = mem.u16(results + uint32_t(s / 2 + r * teams / 2) * 2);
            if (u == 0xcccc) continue;
            const uint8_t goals[2] = {uint8_t((u >> 5) & 0x1f), uint8_t(u & 0x1f)};
            const int club[2] = {fx.at(size_t(s / 2)).home, fx.at(size_t(s / 2)).away};
            add_result(t, club, goals, 3, three_points);
            counted = true;
        }
        if (before_last == r || last == r || (last < r && counted)) sort_table(t, 0);
    }
    return t;
}

// The end of a group stage (AdvanceCup): the qualifiers place by place (all group
// winners, then the runners-up, ...) until the stage's quota (+0x1f) is reached; a place
// taken only partly keeps its best clubs by the table key. Group winner and runner-up are
// noted in +0x22 / +0x24 of every group. Returns the qualifiers and in `loser_pair` the
// first two clubs of the first group ordered by the last table's second place (for a final
// of two).
std::vector<uint16_t> group_qualifiers(StaticMemory& mem, uint32_t e, std::array<uint16_t, 2>& loser_pair) {
    const int groups = mem.u8(e + 0x1e) & 0xF;
    int quota = int8_t(mem.u8(e + 0x1f));
    std::vector<uint16_t> teams;
    std::vector<uint32_t> keys;
    LeagueTable t;
    for (int place = 0; quota > 0; place++) {
        for (int grp = 0; grp < groups; grp++) {
            uint32_t ge = e;
            while ((mem.u8(ge + 0x1e) >> 4) != grp) ge += kCupEntrySize;
            t = group_table(mem, ge);
            int idx = 0;
            while (idx < mem.u8(ge + 0x1a) && t.rank(idx) != place) idx++;
            const uint16_t team = mem.u16(mem.pointer(ge) + uint32_t(idx) * 2);
            if (place == 0) mem.set16(ge + 0x22, team);
            if (place == 1) mem.set16(ge + 0x24, team);
            keys.push_back(sort_key(t, idx, 0));
            teams.push_back(team);
            quota--;
        }
    }
    const uint32_t list = mem.pointer(e);
    loser_pair[0] = t.rank(0) == 1 ? mem.u16(list) : mem.u16(list + 2);
    const int wanted = int8_t(mem.u8(e + 0x1f));
    const int cut = wanted - wanted % groups;
    for (; quota < 0; quota++) {
        uint32_t worst = keys.back();
        size_t at = 0;
        for (int k = cut; k < wanted; k++)
            if (keys[size_t(k)] < worst) at = size_t(k), worst = keys[size_t(k)];
        if (worst < keys.back()) {
            std::swap(teams.back(), teams[at]);
            std::swap(keys.back(), keys[at]);
        }
        teams.pop_back();
        keys.pop_back();
    }
    return teams;
}

}  // namespace

LeagueTable cup_group_table(const StaticMemory& mem, uint32_t entry_address) {
    return group_table(mem, entry_address);
}

int cup_group_schedule_teams(const StaticMemory& mem, uint32_t entry_address) {
    return group_table_teams(mem, entry_address);
}

void end_competition(StaticMemory& mem, int id) { remove_competition(mem, id); }

void prepare_friendlies(ModernState& g) {
    StaticMemory& mem = g.memory;
    if (!mem.has(::bmh::layout::FriendlyDays) || !mem.has(::bmh::layout::FriendlyTeams) || !mem.has(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField24)) return;
    const int today = g.season_day();  // DaysIntoSeason
    int count = 0;                      // local_c: two teams per friendly
    for (int i = 0; i < 0x1e; i++) {
        if (int16_t(mem.u16(::bmh::layout::FriendlyDays + uint32_t(i) * 2)) != int16_t(today)) continue;
        for (int c = 0; c < 2; c++)
            mem.set16(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField24 + uint32_t(c + count) * 2, mem.u16(::bmh::layout::FriendlyTeams + uint32_t(i * 2 + c) * 2));
        mem.set16(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField29 + uint32_t(count >> 1) * 2, 0xcccc);
        mem.set16(::bmh::layout::FriendlyDays + uint32_t(i) * 2, 0);
        count += 2;
        if (i < 0x1d) {
            // the planned ones after it move up (the last place keeps its copy)
            for (int k = i; k < 0x1d; k++) {
                mem.set32(::bmh::layout::FriendlyTeams + uint32_t(k) * 4, mem.u32(::bmh::layout::Cup_PrepareFriendlies_RuntimeField + uint32_t(k) * 4));
                mem.set16(::bmh::layout::FriendlyDays + uint32_t(k) * 2, mem.u16(::bmh::layout::Cup_PrepareFriendlies_RuntimeField2 + uint32_t(k) * 2));
            }
            i--;
            // the planner's entry 30 counts them (two per friendly); only decreased here,
            // not for the last place (as in the original)
            const uint32_t planned = modern::cup_entry_address(30) + 0x1a;
            mem.set8(planned, uint8_t(mem.u8(planned) - 2));
        }
    }
    mem.set8(modern::cup_entry_address(24) + 0x1a, uint8_t(count));  // today's friendlies (CupRuntimeField2)
}

int find_cup(const StaticMemory& mem, int id) {
    for (int i = 0; i < kCupEntries; i++)
        if (int16_t(mem.u16(modern::cup_entry_address(i) + 0x18)) == id) return i;
    return -1;
}

int cup_round(const StaticMemory& mem, int entry) {
    const uint32_t e = modern::cup_entry_address(entry);
    const uint8_t flags = mem.u8(e + 0x14), played = mem.u8(e + 0x1d);
    if (!(flags & 4)) return played;
    return played / ((flags & 1) + 1);
}

int cup_round_offset(const StaticMemory& mem, int entry, int round) {
    if (round == 0) return 0;
    const int teams = mem.u8(modern::cup_entry_address(entry) + 0x1a);
    return teams * 2 - teams / (1 << (round - 1));
}

void rebuild_replay_lists(ModernState& g, uint32_t competitions) {
    for (int bit = 5; bit < 0x13; bit++) {
        if (!((competitions >> bit) & 1)) continue;
        const int entry = modern::find_cup(g.memory, bit - 5);
        if (entry >= 0 && int16_t(g.memory.u16(modern::cup_entry_address(entry) + 0x18)) == 9)
            prepare_replays(g, modern::cup_entry_address(entry), g.season_day());
    }
}

void setup_cup_groups(ModernState& g, int id, std::vector<MatchGroup>& groups, MinuteReport& report) {
    const StaticMemory& mem = g.memory;
    int entry = modern::find_cup(mem, id);
    if (entry < 0) {
        report.fail("cup " + std::to_string(id) + " not in the cup table");
        return;
    }
    const int type = int16_t(mem.u16(modern::cup_entry_address(entry) + 0x18));
    if (type == 9) prepare_replays(g, modern::cup_entry_address(entry), g.season_day());
    if (type == 10) modern::prepare_friendlies(g);
    if (type > 9 && mem.u8(modern::cup_entry_address(entry) + 0x1a) != 0) {
        // AddCompetition: onto the list of running competitions (ActiveSelectors, 0x14 = free)
        for (uint32_t i = 0; i < 8; i++) {
            if (int8_t(mem.u8(::bmh::layout::ActiveSelectors + i)) == type) break;
            if (g.memory.u8(::bmh::layout::ActiveSelectors + i) == 0x14) {
                g.memory.set8(::bmh::layout::ActiveSelectors + i, uint8_t(type));
                break;
            }
        }
    }
    // (the friendlies 10, the promotion play-off 0xd and the indoor tournament 0xb only join
    //  the running list above)
    if (type > 0xb && type != 0xd && mem.u8(modern::cup_entry_address(entry) + 0x1a) != 0) {
        report.fail("cup " + std::to_string(id) + ": preparation of this competition (AddBlock/2631b/26175)");
        return;
    }
    // skip the stages that are completed (bounded: a broken chain would loop for ever)
    for (int steps = 0; modern::cup_round(mem, entry) >= mem.u8(modern::cup_entry_address(entry) + 0x1b); ++steps) {
        const int next = int8_t(mem.u8(modern::cup_entry_address(entry) + 0x16));
        if (steps > 0x24 || next < 0 || next >= 0x24) {
            report.fail("cup " + std::to_string(id) + ": stage chain without an open stage (entry " + std::to_string(entry) +
                        ", round " + std::to_string(modern::cup_round(mem, entry)) + " of " +
                        std::to_string(mem.u8(modern::cup_entry_address(entry) + 0x1b)) + ", next " + std::to_string(next) + ")");
            return;
        }
        entry = next;
    }
    const int parallel = mem.u8(modern::cup_entry_address(entry) + 0x1e) & 0xF;
    if (mem.u32(modern::cup_entry_address(entry)) == 0) return;
    for (int k = 0; k < parallel; k++) {
        const uint32_t e = modern::cup_entry_address(entry);
        const uint8_t flags = mem.u8(e + 0x14);
        const int teams = mem.u8(e + 0x1a);
        if (teams == 0) continue;  // (the original then looks at the same entry again)
        MatchGroup grp;
        grp.desc = modern::group_descriptor(g, groups.size());
        grp.desc.at(0) = uint8_t(type + 1);
        grp.desc.at(1) = flags;
        // competition 11 is the German indoor tournament; in the English game the same entry
        // is the League Cup ("Koka Kola Cup"), an ordinary cup on grass (EN Put32)
        if (type == 0xb && !kEnglishCompetitions) g.indoor = true;
        put32(grp.desc, 0xcc, e);
        // the first leg of a two-legged knockout tie is never decided on the day
        if ((flags & 5) == 5 && mem.u8(e + 0x1d) % 2 == 0) grp.desc.at(1) &= 0xF7;
        int matches = 0, first = 0;  // local_1c; the result index is `first` / 2
        const uint32_t list = mem.pointer(e);
        if (!(flags & 4)) {
            // a group: GenerateFixtures on the cup's round strings (+8), round = played + 1, the
            // draw positions index the group's team list; two-legged groups play the rounds
            // of the string table twice, home and away swapped
            matches = teams / 2;
            const int played = mem.u8(e + 0x1d);
            first = uint8_t(matches * played * 2);
            const int rounds = mem.u8(e + 0x1b) / ((flags & 1) + 1);
            int table_teams = 0;
            switch (mem.pointer(e + 8)) {
                case ::bmh::layout::CareerSession_RuntimeError_RuntimeField: table_teams = 4; break;
                case ::bmh::layout::CareerSession_RuntimeError_RuntimeField2: table_teams = 6; break;
                case ::bmh::layout::CareerSession_RuntimeError_RuntimeField3: table_teams = 12; break;
                case ::bmh::layout::CareerSession_RuntimeError_ExecutableField3: table_teams = 16; break;
                case ::bmh::layout::CareerSession_RuntimeError_ExecutableField: table_teams = 18; break;
                case ::bmh::layout::CareerSession_RuntimeError_ExecutableField2: table_teams = 20; break;
            }
            if (table_teams == 0) {
                report.fail("cup group with unknown round strings");
                return;
            }
            for (const Fixture& f : generate_fixtures(table_teams, played + 1, rounds, 0)) {
                if (int(grp.fixtures.size()) >= matches * 2) break;
                grp.fixtures.push_back(int16_t(mem.u16(list + uint32_t(f.home) * 2)));
                grp.fixtures.push_back(int16_t(mem.u16(list + uint32_t(f.away) * 2)));
            }
            // the fixtures are kept in the descriptor itself (+0x88; +0x7c points there)
            for (size_t i = 0; i < grp.fixtures.size(); i++) {
                grp.desc.at(0x88 + i * 2) = uint8_t(grp.fixtures[i]);
                grp.desc.at(0x89 + i * 2) = uint8_t(uint16_t(grp.fixtures[i]) >> 8);
            }
        } else {
            const int round = modern::cup_round(mem, entry);
            matches = teams / (1 << (round + 1));
            first = modern::cup_round_offset(mem, entry, round);
            for (int i = 0; i < matches * 2; i++) grp.fixtures.push_back(int16_t(mem.u16(list + uint32_t(first + i) * 2)));
        }
        grp.desc.at(2) = uint8_t(matches);
        if (type == 0 && matches == 1) grp.desc.at(1) &= 0xFD;  // the national cup final
        // MatchdayNeedsManagerDesks: a human manager's club notes its side (record +0x28a)
        for (int i = 0; i < matches * 2; i++)
            for (size_t mgr = 0; mgr < g.manager_teams.size(); mgr++)
                if (g.manager_teams[mgr] == grp.fixtures[size_t(i)]) g.manager_records.at(mgr * 0xB0C + 0x28a) = uint8_t(i % 2);
        for (int leg = 0; leg < 2; leg++) {
            if (leg == 1 && !(flags & 1)) break;
            const uint32_t results = mem.pointer(e + 0x0c + uint32_t(leg) * 4);
            if (results == 0) continue;
            grp.cup_result_address[leg] = results + uint32_t(first / 2) * 2;
            std::vector<uint16_t>& r = leg == 0 ? grp.cup_results : grp.cup_other_leg;
            for (int i = 0; i < matches; i++) r.push_back(mem.u16(grp.cup_result_address[leg] + uint32_t(i) * 2));
        }
        groups.push_back(grp);
        entry++;
    }
}

}  // namespace bmh::modern

namespace bmh::modern {

void store_cup_results(ModernState& g, const MatchGroup& grp) {
    if (grp.desc.at(0) == 0 || grp.cup_result_address[0] == 0) return;
    for (size_t i = 0; i < grp.cup_results.size(); i++)
        g.memory.set16(grp.cup_result_address[0] + uint32_t(i) * 2, grp.cup_results[i]);
}

namespace {

// NationalTournamentEnd, at the end of the international tournament (competition type 4): the
// honours list of the tournaments (Placements, 4 bytes per tournament, newest first)
// moves one place on and gets the first four (final winner and loser Cup_NationalTournamentEnd_RuntimeField/2dce,
// third place match Cup_NationalTournamentEnd_RuntimeField3/2df6) as national team index | 0x80. A Bundestrainer's
// association (manager slot 0 holds team 0x165) pays 400000 for taking part and the
// premium of the place (CupRuntimeField3).
void national_tournament_end(ModernState& g) {
    StaticMemory& mem = g.memory;
    const int teams[4] = {int16_t(mem.u16(::bmh::layout::Cup_NationalTournamentEnd_RuntimeField)), int16_t(mem.u16(::bmh::layout::Cup_NationalTournamentEnd_RuntimeField2)), int16_t(mem.u16(::bmh::layout::Cup_NationalTournamentEnd_RuntimeField3)),
                          int16_t(mem.u16(::bmh::layout::Cup_NationalTournamentEnd_RuntimeField4))};
    static constexpr int32_t kPremium[4] = {1000000, 600000, 300000, 200000};  // CupRuntimeField3
    if (mem.has(::bmh::layout::Placements))
        for (int i = 0x4c - 1; i >= 0; i--) mem.set8(::bmh::layout::Cup_NationalTournamentEnd_RuntimeField5 + uint32_t(i), mem.u8(::bmh::layout::Placements + uint32_t(i)));
    const bool coach = !g.manager_teams.empty() && g.manager_teams[0] == no_team();
    if (coach) modern::book_money(g, 0, -400000, true, true);
    for (int i = 0; i < 4; i++) {
        if (teams[i] == no_team() && coach) modern::book_money(g, 0, -kPremium[i], true, true);
        // (0x8a + 0x1e + 0xbd = 0x165: CareerSession_DispatchLineup_RuntimeField + CareerSession_InstallLeagueSizes_RuntimeField6 + ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField3)
        if (mem.has(::bmh::layout::Placements)) mem.set8(::bmh::layout::Placements + uint32_t(i), uint8_t((teams[i] - no_team()) | 0x80));
    }
}

// cup_tie on the result arrays of a stage (CupTie(results0, results1, index, flags))
int stage_tie(const StaticMemory& mem, uint32_t e, int index) {
    MatchGroup grp;
    grp.desc.assign(0xd0, 0);
    grp.desc.at(1) = mem.u8(e + 0x14);
    const uint32_t r0 = mem.pointer(e + 0x0c), r1 = mem.pointer(e + 0x10);
    grp.cup_results.assign(size_t(index) + 1, 0);
    grp.cup_results[size_t(index)] = mem.u16(r0 + uint32_t(index) * 2);
    if (r1 != 0) {
        grp.cup_other_leg.assign(size_t(index) + 1, 0);
        grp.cup_other_leg[size_t(index)] = mem.u16(r1 + uint32_t(index) * 2);
    }
    return modern::cup_tie(grp, index);
}

void swap16(StaticMemory& mem, uint32_t a, uint32_t b) {
    const uint16_t x = mem.u16(a);
    mem.set16(a, mem.u16(b));
    mem.set16(b, x);
}

// Getenv(teams, offset): Source home advantage followed by an unstable-free
// descending sort on each tie's combined league weight. Kept in this Core module so
// finish_cup can expose the Original draw before the inner sort and retain its outer sort.
void order_national_cup_ties(ModernState& g, int teams, int offset) {
    StaticMemory& mem = g.memory;
    const uint32_t list = mem.pointer(modern::cup_entry_address(0)) + uint32_t(offset) * 2;
    static constexpr uint8_t kWeight[10] = {3, 1, 0, 0, 2, 0, 0, 0, 7, 0};
    const int lower = league_first_team(2);
    std::vector<uint8_t> weights;
    weights.reserve(size_t(teams / 2));
    auto team = [&](int i) { return int16_t(mem.u16(list + uint32_t(i) * 2)); };
    auto swap_pair = [&](int a, int b) {
        swap16(mem, list + uint32_t(a) * 2, list + uint32_t(b) * 2);
        swap16(mem, list + uint32_t(a + 1) * 2, list + uint32_t(b + 1) * 2);
    };
    for (int i = 0; i < teams; i += 2) {
        if (lower <= team(i + 1) && team(i) < lower)
            swap16(mem, list + uint32_t(i) * 2, list + uint32_t(i + 1) * 2);
        auto league = [](int t) { return t > 0x164 ? 0 : team_league(t); };
        const int la = league(team(i)), lb = league(team(i + 1));
        weights.push_back(uint8_t(kWeight[la < 10 ? la : 9] + kWeight[lb < 10 ? lb : 9]));
    }
    for (int i = 0; i < teams / 2 - 1; ++i)
        for (int j = i + 1; j < teams / 2; ++j)
            if (weights[size_t(i)] < weights[size_t(j)]) {
                std::swap(weights[size_t(i)], weights[size_t(j)]);
                swap_pair(i * 2, j * 2);
            }
}

bool source_cup_draw_enabled(const StaticMemory& mem, int competition) {
    // CupDrawFromGame: outside leagues 4..7 the leagues must be enabled, then the
    // competition's display option must be clear. The follow-up package targets comp0.
    if (competition < 4 || competition > 7) {
        if (!mem.has(::bmh::layout::CompetitionSelection) || (mem.u8(::bmh::layout::CompetitionSelection) & 1) == 0) return false;
    }
    const uint32_t option = ::bmh::layout::LeaguePresentationOptions + uint32_t(competition + 5) * 9;
    return mem.has(option) && mem.u8(option) == 0;
}

void observe_cup_draw(const ModernState& g, int entry, SeasonCupDrawPhase phase,
                      const SeasonCupDrawHook& hook) {
    if (!hook) return;
    const StaticMemory& mem = g.memory;
    const uint32_t address = modern::cup_entry_address(entry);
    SeasonCupDrawEvent event;
    event.phase = phase;
    event.entry_address = address;
    event.competition = mem.u8(address + 0x18);
    event.round = modern::cup_round(mem, entry);
    event.offset = modern::cup_round_offset(mem, entry, event.round);
    event.count = size_t(mem.u8(address + 0x1a)) >> event.round;
    const uint32_t first = mem.pointer(address) + uint32_t(event.offset) * 2;
    event.participants.reserve(event.count);
    for (size_t i = 0; i < event.count; ++i)
        event.participants.push_back(mem.u16(first + uint32_t(i) * 2));
    hook(g, event);
}

}  // namespace

void advance_cup(ModernState& g, int id, WatcomRandom& rand, MinuteReport& report, GameRandom* game,
                 const SeasonCupDrawHook& cup_draw, const CompetitionWinnerHook& winner_display) {
    StaticMemory& mem = g.memory;
    int entry = modern::find_cup(mem, id);
    if (entry < 0) return;
    const int type = int16_t(mem.u16(modern::cup_entry_address(entry) + 0x18));
    if (type == 9) return store_replays(g, g.season_day());
    // EndCompetition(type), re-added at the next preparation; the English Put32 keeps its
    // League Cup (type 0xb) running like the other cups
    if (type == 0xb && !kEnglishCompetitions) remove_competition(mem, type);
    if (type == 10) {
        // the friendlies: off the running list, the day's calendar entry masked with 0xffe
        // (0x4001 becomes 0) and no teams left in today's entry (CupRuntimeField2)
        remove_competition(mem, type);
        const uint32_t day = ::bmh::layout::CompetitionCalendar + uint32_t(g.season_day()) * 2;
        if (mem.has(day)) mem.set16(day, uint16_t(mem.u16(day) & 0xffe));
        mem.set8(modern::cup_entry_address(24) + 0x1a, 0);
        return;
    }
    if (type > 8 && type != 0xd && type != 0xb) {
        report.fail("cup " + std::to_string(id) + ": processing of this competition (AdvanceCup)");
        return;
    }
    while (modern::cup_round(mem, entry) >= mem.u8(modern::cup_entry_address(entry) + 0x1b))
        entry = int8_t(mem.u8(modern::cup_entry_address(entry) + 0x16));
    uint32_t e = modern::cup_entry_address(entry);
    std::vector<uint16_t> winners, losers;
    std::array<uint16_t, 2> loser_pair{};
    if (!(mem.u8(e + 0x14) & 4)) {
        // a group stage: every group (the consecutive entries up to the one without a
        // parallel link, +0x17) counts the round
        for (uint32_t ge = e;; ge += kCupEntrySize) {
            mem.set8(ge + 0x1d, uint8_t(mem.u8(ge + 0x1d) + 1));
            if (mem.u8(ge + 0x17) == 0) break;
        }
        if (mem.u8(e + 0x1d) < mem.u8(e + 0x1b)) return;
        winners = group_qualifiers(mem, e, loser_pair);
    } else {
        const int round = modern::cup_round(mem, entry);
        const int slots = mem.u8(e + 0x1a) / (1 << round);  // local_3c: teams in this round
        const int first = modern::cup_round_offset(mem, entry, round);
        mem.set8(e + 0x1d, uint8_t(mem.u8(e + 0x1d) + 1));  // (unless CupRuntimeField4)
        const uint32_t list = mem.pointer(e);
        if ((mem.u8(e + 0x14) & 1) && mem.u8(e + 0x1d) % 2 != 0) {
            // after the first leg: the pairs turn round, the results go to the second array
            for (int i = 0; i < slots; i += 2) {
                swap16(mem, list + uint32_t(first + i) * 2, list + uint32_t(first + i + 1) * 2);
                const uint32_t k = uint32_t((first + i) / 2) * 2;
                swap16(mem, mem.pointer(e + 0x0c) + k, mem.pointer(e + 0x10) + k);
            }
            return;
        }
        for (int i = 0; i < slots; i += 2) {
            int t = stage_tie(mem, e, (first + i) / 2);
            if (t > 1) t = 1;
            winners.push_back(mem.u16(list + uint32_t(first + i + t) * 2));
            losers.push_back(mem.u16(list + uint32_t(first + i + 1 - t) * 2));
        }
    }
    // CupRuntimeField5[type] < 5 (national cup and the three European cups): a human manager's
    // club that goes through counts one more round in his season record (ManagerOf,
    // record +0xda + season * 3 + 1 national / 2 European)
    if (type >= 0 && type <= 3)
        for (uint16_t t : winners) {
            const int manager = g.manager_of(t);
            if (manager < 0) continue;
            const size_t at = size_t(manager) * 0xB0C + 0xda + size_t(std::min(g.season, 0x31)) * 3 + (type > 0 ? 2 : 1);
            g.manager_records.at(at) = uint8_t(g.manager_records.at(at) + 1);
        }
    if (modern::cup_round(mem, entry) == mem.u8(e + 0x1b)) {
        // the stage is over
        if (mem.u8(e + 0x16) == 0) {
            // The last stage of the competition. A single final notes its winner (+0x22) and
            // loser (+0x24); competition 0xc leaves the running list.
            if (winners.size() == 1) {
                mem.set16(e + 0x22, winners[0]);
                if (!losers.empty()) mem.set16(e + 0x24, losers[0]);
            }
            if (type == 0xc) remove_competition(mem, 0xc);
            // CupRuntimeField5: the title of the competition (0xFF: none)
            static constexpr uint8_t kTitle[14] = {1, 2, 3, 4, 5, 0xff, 6, 0xff, 8, 0xff, 0xff, 0xff, 0xff, 0xff};
            const uint8_t title = type >= 0 && type < 14 ? kTitle[type] : 0xff;
            if (title != 0xff && !winners.empty()) {
                if (type == 4) national_tournament_end(g);  // NationalTournamentEnd
                // CompetitionWinnerScreen(0, title, winner): the winner screen (unless switched off,
                // Cup_AdvanceCup_RuntimeField; no draws) clears CareerSession_BeginFriendlyOpponentTable_ExecutableField2. A human manager's club counts
                // the title in his record: +0x8a6 national cup, +0x8a8 European cups.
                if (mem.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField) == 0 && winner_display)
                    winner_display(g, {title, winners[0]});
                if (mem.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField) == 0 && mem.has(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2)) mem.set8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2, 0);
                const int manager = g.manager_of(winners[0]);
                if (manager >= 0 && type >= 0 && type < 4) {
                    const size_t at = size_t(manager) * 0xB0C + (type == 0 ? 0x8a6 : 0x8a8);
                    const uint16_t n = uint16_t(g.manager_records.at(at) | g.manager_records.at(at + 1) << 8);
                    g.manager_records.at(at) = uint8_t(n + 1), g.manager_records.at(at + 1) = uint8_t((n + 1) >> 8);
                }
                // ListTitleHoldersAf(winner, title): the list of title holders (Titles)
                mem.set16(::bmh::layout::Titles + uint32_t(title) * 100 + uint32_t(std::min<int>(g.season, 0x31)) * 2, winners[0]);
            }
            if (type == 0 && !winners.empty()) {
                // the national cup: winner (Cup_AdvanceCup_RuntimeField5) and the previous holder
                // (Career_CompetitionPointersEnglish_ExecutableField28 = Cup_AdvanceCup_RuntimeField6); Cup_AdvanceCup_RuntimeField2 = 0 if the holder won again, else 2
                mem.set16(::bmh::layout::Cup_AdvanceCup_RuntimeField5, winners[0]);
                mem.set16(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField28, mem.u16(::bmh::layout::Cup_AdvanceCup_RuntimeField6));
                if (mem.has(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField31)) mem.set16(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField31, 0xcccc);
                if (mem.has(::bmh::layout::Cup_AdvanceCup_RuntimeField2)) mem.set8(::bmh::layout::Cup_AdvanceCup_RuntimeField2, mem.u16(::bmh::layout::Cup_AdvanceCup_RuntimeField6) == winners[0] ? 0 : 2);
            }
            // a linked stage (+0x17) takes the runner-up (the indoor tournament's final group
            // notes it in the group for third place)
            if (mem.u8(e + 0x17) != 0 && winners.size() > 1)
                mem.set16(modern::cup_entry_address(mem.u8(e + 0x17)) + 0x22, winners[1]);
            // (the indoor tournament only: the English League Cup has no Masters; its winner
            //  would go to EN CupRuntimeField6 for Europe - not ported, see docs/fltd.md)
            if (type != 0xb || kEnglishCompetitions) return;
            // the indoor tournament: the four places (CupRuntimeField7 picks them from the list
            // champion..fourth) earn points for the Masters (CupRuntimeField8 x strength class
            // Cup_AdvanceCup_RuntimeField3, clubs of the first two leagues only), then the next tournament is
            // drawn; after five tournaments the Masters follow
            int held = mem.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField4);
            if (held > 4) {
                // the end of the Masters: CompetitionWinnerScreen(0, 7, winner) shows the winner (unless
                // switched off, Cup_AdvanceCup_RuntimeField; no draws) and clears CareerSession_BeginFriendlyOpponentTable_ExecutableField2;
                // ListTitleHoldersAf(winner, 7) enters it in the list of title holders (Titles,
                // 50 seasons per competition); Cup_AdvanceCup_RuntimeField7 = winner; a human manager's club
                // gets a message and 250 000; EndCompetition removes the competition
                const uint16_t winner = mem.u16(e + 0x22);
                if (mem.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField) == 0 && winner_display)
                    winner_display(g, {7, winner});
                if (mem.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField) == 0 && mem.has(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2)) mem.set8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2, 0);
                mem.set16(::bmh::layout::Titles + 7 * 100 + uint32_t(std::min<int>(g.season, 0x31)) * 2, winner);
                if (mem.has(::bmh::layout::Cup_AdvanceCup_RuntimeField7)) mem.set16(::bmh::layout::Cup_AdvanceCup_RuntimeField7, winner);
                const int manager = g.manager_of(winner);
                if (manager >= 0) {
                    post_composed(g, manager, kMastersWinnerText);  // MastersWinnerText
                    modern::book_money(g, manager, -250000, true, true);
                }
                remove_competition(mem, type);
                return;
            }
            static constexpr uint8_t kPlaceOrder[4] = {0, 2, 1, 3};  // CupRuntimeField7
            static constexpr uint8_t kPlacePoints[5] = {0, 6, 4, 2, 1};  // CupRuntimeField8
            for (int i = 0; i < 4 && size_t(kPlaceOrder[i]) < winners.size(); i++) {  // Uint32T
                const int team = winners[kPlaceOrder[i]];
                if (team < kLeagueSizes[0] + kLeagueSizes[1]) {
                    const uint32_t at = ::bmh::layout::Cup_AdvanceCup_ExecutableField + uint32_t(team) * 2;
                    mem.set16(at, uint16_t(mem.u16(at) + kPlacePoints[i + 1] * mem.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField3)));
                }
            }
            mem.set8(::bmh::layout::Cup_AdvanceCup_RuntimeField4, uint8_t(++held));
            if (!game) {
                report.fail("indoor tournament: the next draw needs the game generator");
                return;
            }
            if (held >= 5) {
                modern::masters_qualification(g, *game);
                return;
            }
            modern::indoor_tournament_draw(g, *game, rand);  // (CupRuntimeField = 0)
            return;
        }
        uint32_t last = e;
        const int seeded = mem.u8(e + 0x20);
        if (seeded != 0) {
            // clubs seeded directly into the next stage stand before the team lists
            const int groups = mem.u8(e + 0x1e) & 0xF;
            int left = seeded;
            for (int k = 0; left != 0; k++)
                for (int grp = 0; grp < groups; grp++) {
                    uint32_t ge = e;
                    while ((mem.u8(ge + 0x1e) >> 4) != grp) ge += kCupEntrySize;
                    last = ge;
                    if (left != 0) {
                        winners.push_back(mem.u16(mem.pointer(ge) - 2 - uint32_t(k) * 2));
                        left--;
                    }
                }
        }
        entry = int8_t(mem.u8(last + 0x16));
        e = modern::cup_entry_address(entry);
    }
    const int count = int(winners.size());  // local_14
    if (mem.u8(e + 0x14) & 0x20) {
        // the draw
        for (int i = 0; i < count; i++) {
            const int a = rand.range(0, count - 1);
            const int b = rand.range(0, count - 1);
            std::swap(winners[size_t(b)], winners[size_t(a)]);
        }
    }
    if (!(mem.u8(e + 0x14) & 4)) {
        // a group stage: the teams go round the groups, taken from the end
        // groups of two (third place and final): the losers of the ties are added (local_88)
        if (mem.u8(e + 0x1a) == 2)
            for (int i = 0; i < 2 && size_t(i) < losers.size(); i++) winners.push_back(losers[size_t(i)]);
        const int groups = mem.u8(e + 0x1e) & 0xF;
        int left = int(winners.size());
        uint32_t ge = e;
        for (int slot = 0; slot < mem.u8(e + 0x1a); slot++)
            for (int grp = 0; grp < groups; grp++) {
                ge = e;
                while ((mem.u8(ge + 0x1e) >> 4) != grp) ge += kCupEntrySize;
                mem.set16(mem.pointer(ge) + uint32_t(slot) * 2, winners.at(size_t(--left)));
            }
        // AdvanceCup leaves any entrants beyond the regular group slots in the reserved
        // list immediately before the final linked group's team list (entry +0x20 is the
        // source guard). The Source consumes the remaining local list backwards.
        if (mem.u8(ge + 0x20) != 0) {
            for (int slot = 0; left > 0; ++slot)
                for (int grp = 0; grp < groups; ++grp)
                    if (left > 0)
                        mem.set16(mem.pointer(ge) - 2 - uint32_t(slot) * 2, winners.at(size_t(--left)));
        }
        if (mem.u8(e + 0x1a) == 2) {
            // the first club of this group and the second of the linked group change places
            const uint32_t a = mem.pointer(e), b = mem.pointer(modern::cup_entry_address(mem.u8(e + 0x17))) + 2;
            const uint16_t x = mem.u16(a);
            mem.set16(a, mem.u16(b));
            mem.set16(b, x);
        }
    } else {
        const int next_round = modern::cup_round(mem, entry);
        const int offset = modern::cup_round_offset(mem, entry, next_round);
        const uint32_t to = mem.pointer(e);
        int first_pair = 0;
        if (next_round == mem.u8(e + 0x1b) && mem.u8(e + 0x20) != 0) {
            first_pair = mem.u8(e + 0x20);
            for (int i = 0; i < first_pair; ++i)
                mem.set16(to - 2 - uint32_t(i) * 2, winners.at(size_t(i)));
        }
        for (int i = first_pair; i < count / 2; i++) {
            mem.set16(to + uint32_t(offset + i * 2) * 2, winners[size_t(i)]);
            mem.set16(to + uint32_t(offset + i * 2 + 1) * 2, winners[size_t(count - 1 - i)]);
        }
        if (e == modern::cup_entry_address(0)) {
            const bool eligible = type == 0 && count > 2 && (mem.u8(e + 0x14) & 0x20) != 0 &&
                                  source_cup_draw_enabled(mem, type);
            if (eligible) {
                // CupDrawFromGame displays the just-written shuffled order first.
                observe_cup_draw(g, entry, SeasonCupDrawPhase::draw, cup_draw);
                // It then performs Getenv and CupGroupOverviewEffects before returning.
                order_national_cup_ties(g, count, offset);
                modern::cup_overview_effects(g);  // simulation effects occur even without an observer
                observe_cup_draw(g, entry, SeasonCupDrawPhase::overview, cup_draw);
            }
            // AdvanceCup performs this outer sort after CupDrawFromGame returns (including
            // its hidden-display early return). It is idempotent after the inner sort.
            order_national_cup_ties(g, count, offset);
        }
    }
    // Career_EuropeanClub_ExecutableField / Career_EuropeanClub_ExecutableField2 / Career_EuropeanClub_ExecutableField3: clubs left in the European cups
    const uint32_t left_at[4] = {0, ::bmh::layout::Career_EuropeanClub_ExecutableField, ::bmh::layout::Career_EuropeanClub_ExecutableField2, ::bmh::layout::Career_EuropeanClub_ExecutableField3};
    if (type >= 1 && type <= 3 && mem.has(left_at[type])) mem.set8(left_at[type], uint8_t(count));
}

}  // namespace bmh::modern
