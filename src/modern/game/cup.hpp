// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#pragma once
#include "game/cup.hpp"
// Cup competitions (BMH.exe V2.24 + supported rule profile patch).
//
// All cups are described by the table at CupTable: 36 entries of 0x28 bytes, one per stage
// (knockout rounds of a cup, a group of a group stage, a final, ...). Entries of the same
// cup share the id at +0x18 and are linked:
//   +0x00  pointer to the team list (u16 team ids; for knockout stages the pairs of each
//          round follow each other, round r starting at cup_round_offset(r))
//   +0x08  pointer to the round strings of group stages (fixture tables, see schedule.hpp)
//   +0x0c  pointer to the results of the first/only leg, +0x10 of the second leg (u16 per
//          match: bits 0-4 away goals, 5-9 home goals, 10-15 phase; 0xCCCC not played)
//   +0x14  flags: bit 0 two legs, bit 2 knockout, bit 3 the tie is decided on the day
//          (extra time), bit 1 away-side penalty for league-style chances, ...
//   +0x16  index of the next stage, +0x17 index of the parallel group of this stage
//   +0x18  cup id (0 national cup, 1-3 European cups, 4.. further competitions)
//   +0x1a  number of teams, +0x1b number of rounds (legs), +0x1d legs played so far
//   +0x1e  low nibble: number of parallel groups, high nibble: index of this group
// Match group descriptors of cups have type = cup id + 1 and point to their table entry
// (descriptor +0xcc) instead of holding a league number.

#include <cstdint>
#include <vector>

#include "game/match_engine.hpp"
#include "game/table.hpp"
#include "modern/game/season.hpp"
#include "game/sportschau_presentation.hpp"
#include "modern/game/modern_state.hpp"

namespace bmh::modern {

constexpr uint32_t kCupTable = ::bmh::layout::CupTable;
constexpr int kCupEntries = 36;
constexpr uint32_t kCupEntrySize = 0x28;

inline uint32_t cup_entry_address(int index) { return kCupTable + uint32_t(index) * kCupEntrySize; }
inline int cup_entry_index(uint32_t address) { return int((address - kCupTable) / kCupEntrySize); }

// FindCup(id, 0): the first table entry of a cup, -1 if there is none.
int find_cup(const StaticMemory& mem, int id);
// CupRound: the current round of a stage (legs played, halved for two-legged ties).
int cup_round(const StaticMemory& mem, int entry);
// CupRoundOffset: index of the first team of knockout round `round` in the team list.
int cup_round_offset(const StaticMemory& mem, int entry, int round);

// AddResult's group-table calculation. Exposes the existing Core projection
// as a read-only value for Source overview observers and renderers.
LeagueTable cup_group_table(const StaticMemory& mem, uint32_t entry_address);
// Team count selected by the Source group round-schedule pointer at entry +8.
int cup_group_schedule_teams(const StaticMemory& mem, uint32_t entry_address);

// SetupCupGroups: the match groups of cup `id` for today, appended to `groups`.
void setup_cup_groups(ModernState& g, int id, std::vector<MatchGroup>& groups, MinuteReport& report);
// The lists of today's postponed-match replays (blocks ReplayFixtures/ReplayResults, not in the savegame) as
// the preparation of the matchday built them (prepare_replays, no draws): needed when a game
// saved at the pre-match desk of a replay day is continued.
void rebuild_replay_lists(ModernState& g, uint32_t competitions);

// EndCompetition(id): takes a competition off the list of running ones (ActiveSelectors).
void end_competition(StaticMemory& mem, int id);

// PrepareFriendlies: the friendlies (competition 10, cup table entry 24) of today: the planned ones
// (entry 30: team pairs FriendlyTeams, season days FriendlyDays, 30 places) of this season day
// go into the day's fixture list (Career_CompetitionPointersEnglish_ExecutableField24) with an open result (0xcccc) and leave the plan.
void prepare_friendlies(ModernState& g);

}  // namespace bmh::modern

namespace bmh::modern {

// Writes the results of a cup group (current leg) back into the result array in memory.
void store_cup_results(ModernState& g, const MatchGroup& grp);

// AdvanceCup for knockout stages: after a matchday of cup `id` the legs played go up;
// after the first leg of a two-legged tie the pairs turn round (home and away swapped,
// the results moved to the other leg's array). After a complete round the winners
// (cup_tie) meet in the next round - drawn if the stage has flag 0x20 (WatcomRandom) -
// or, when the stage is over, fill the next linked stage, including qualifiers and direct
// seeds for group and knockout formats; group match simulation and competition-specific
// finals remain outside this routine.
// The indoor tournament draws its next tournament at the end (game generator, `game`).
void advance_cup(ModernState& g, int id, WatcomRandom& rand, MinuteReport& report, GameRandom* game = nullptr,
                 const SeasonCupDrawHook& cup_draw = {}, const CompetitionWinnerHook& winner = {});

// The end of the fifth indoor tournament: the field of the Masters (MastersQualification2,
// IndoorStandingsFromGame(0)), see cup.cpp.
void masters_qualification(ModernState& g, GameRandom& game);

}  // namespace bmh::modern
