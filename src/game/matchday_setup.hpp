// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#pragma once
// Before the first minute of a matchday (BMH.exe V2.24 + supported rule profile patch).
//
// When "Weiter" reaches a day with a calendar event, AdvanceTurn prepares the matchday:
//   1. DayCompetitions2 builds one match group per league playing today (FixturesFromGenerateFixtures):
//      fixtures of the next round, the result bytes of that round, and per match the
//      weather and the referee (MatchWeather). Every team is then prepared for the match
//      (PrepareTeam: cards of this competition, injuries, line-up flags) and gets an
//      automatic line-up (Int2 mode 100). CycleManagerOptions advances the human
//      managers' option byte 0 to the next enabled setting.
//   2. CurrentMatchday = WatcomRandom(0, 10000); PrepareEvent shuffles the order in which
//      the human managers see their pre-match desks (one game draw per manager).
//   3. The matchday driver FinishEvent (called through the supported rule profile wrapper
//      MatchdaySetupOperation) starts: "played" flags are cleared, the human tactics boards note who
//      starts, every group's unplayed result bytes are set to 0:0 (anything else voids
//      the match), and per match the attendance (Attendance) and the side records
//      (BuildSideRecords3 + BuildSideRecords2) are computed.
// After that the minute loop (match_engine.hpp) runs.
//
// Ported for league matchdays of computer teams. Cups, indoor tournaments, the winter
// break postponements, human teams in a match (pre-match desk, own stadium, income) and
// Teams with fewer than 8 fit players receive the original 0:2 forfeit award.

#include <cstdint>
#include <vector>

#include "game/match_engine.hpp"
#include "game/random.hpp"
#include "game/state.hpp"

namespace bmh {

// Globals of the original that the preparation reads but that are not part of GameState.
struct MatchdayContext {
    int month = 0;        // CareerSession_PairingsRuntimeDraw_ExecutableField5: index into the monthly temperature table
    int season_day = 0;   // DaysIntoSeason: day since 1 July (winter break test)
    int weekday = 0;      // CareerSession_PairingsRuntimeDraw_ExecutableField6: weekday code of the day (the last DeskDate2 call of the day step)
    // RefereeProperties: 50 referees from M3D.MAN; bit 7 marks "already chosen today".
    std::vector<uint8_t>* referees = nullptr;
    // the national team day (DayCompetitions -> national-only flag): AdvanceTurn skips the Bundestrainer
    // check of the other days (no take-over, no hand-back)
    bool national_team_day = false;
};

// The 0xD0-byte descriptor of match group `index` as it is left in memory (block MatchdayGroupDescriptors) from
// the previous matchday: the preparation only overwrites what it needs, so for example
// the referee bytes of matches without referee keep old values.
Bytes group_descriptor(const GameState& g, size_t index);
// Writes the descriptors back (the per-match bytes, void mask, attendance, league/cup).
void store_group_descriptors(GameState& g, const std::vector<MatchGroup>& groups, bool with_log = false);

// MatchWeather: temperature (descriptor +0x30), rain (+0x50) and referee (+0x10) of
// match m of a group.
void match_weather(MatchGroup& grp, int m, const MatchdayContext& ctx, GameRandom& game, WatcomRandom& rand);

// PrepareTeam: prepares the squad of `team` before a match of a competition of class
// `cls` (MatchEngineRuntimeField9[type]: 0 league, 1-4 cups with their own card counters, 0xFF none).
// `index` is the team's position in the flat fixture list (home 0, away 1, ...).
void prepare_team(GameState& g, int team, int cls, int index);
// Kick-off clears played markers after lineup preparation and before ratings.
void reset_match_played_flags(GameState&);

// DayCompetitions2(..., 1): builds the match groups of the competitions in `competitions`
// (SavedCompetition) and prepares all teams. Returns the groups in the order of the original.
// DayCompetitions: the competitions of a day from its calendar entry (CompetitionCalendar): the low
// 15 bits, with special entries mapped (0x4001 group -> 0x8000, 0x4002 -> layout field indoor
// tournament, 0x4004 -> layout field, 0x4006 -> national-only flag or nothing) and the four league
// groups 0x1e00 rotating with the season.
uint32_t day_competitions(const GameState& g, uint16_t entry);

// IndoorTournamentDraw: the draw of an indoor tournament (eight clubs, the host names it).
void indoor_tournament_draw(GameState& g, GameRandom& game, WatcomRandom& rand);
// The part of IndoorTournamentDraw that also runs before the Masters (CupRuntimeField != 0, no draw):
// before the first tournament the Masters points (Cup_AdvanceCup_ExecutableField) and invitations
// (MatchdaySetup_IndoorTournamentDraw_RuntimeField) are cleared; the name becomes "In ", the results of block MatchdayGroupWorkspace 0xCC and
// the played counters of the cup entries 25-29 0.
void indoor_tournament_reset(GameState& g);

std::vector<MatchGroup> setup_matchday(GameState& g, uint32_t competitions, const MatchdayContext& ctx,
                                       GameRandom& game, WatcomRandom& rand, MinuteReport& report);

// MatchdayNeedsManagerDesks / AdvanceTurn: after preparation and manager-order shuffling,
// F6 (MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField == 0) admits the whole manager desk loop only if a manager plays.
// Postponed/already played league fixtures do not count. Opening/closing season
// desks, national friendlies and the Source auto-continue latch retain their gates.
// Read-only; callers still simulate an event whose desks are skipped.
bool matchday_needs_manager_desks(const GameState&, uint16_t calendar_entry,
                                  const std::vector<MatchGroup>&);

// CycleManagerOptions: option byte 0 of every human manager moves on to the next setting that
// is enabled in the manager record (+0x218, a bit mask of settings 1-8).
void cycle_manager_options(GameState& g);

// MatchModeBits plus the flags added by BuildSideRecords: the "mode bits" passed to the
// attendance. Bits 0-4 a crowd interest value (per competition type, rising towards the
// end of the league season), 0x20 random spread, 0x80 no league match, 0x100 cup of
// class 1, 0x200 other cups.
int match_mode_bits(const GameState& g, const MatchGroup& grp, const MatchdayContext& ctx, MinuteReport& report);

// Attendance: attendance of a match. For a human home club also the gate and kiosk
// income, the attendance statistics of its record and a possible riot (message, damage).
// competition_type = byte 0 of the group descriptor (MatchdaySetupRuntimeField14). Bits 0x180
// select cup gate sharing, including the away human manager's income/statistics.
int attendance(GameState& g, int home, int away, int bits, int temperature, int rain, int competition_type,
               GameRandom& game, WatcomRandom& rand, MinuteReport& report);

// PrepareEvent(ManagerCount, NextDeskManager): shuffles the order of the human managers' pre-match
// desks in place - the order of the matchday before is shuffled again, not 0, 1, ... - and
// returns it (manager indices). One game draw per manager: entry i swaps with entry G(0, n - 1).
std::vector<int> shuffle_manager_order(GameState& g, GameRandom& game);

// The start of FinishEvent up to the first minute: flags, void masks, attendance and side
// records for every match of every group, including the live match choice (AutoLineup) and
// the man marking of human managers (KickOff). Human teams draw from `game` in their
// ratings (team_rating).
void kick_off(GameState& g, std::vector<MatchGroup>& groups, const MatchdayContext& ctx, WatcomRandom& rand,
              GameRandom& game, MinuteReport& report, bool select_sportschau = false,
              uint16_t club_threshold = 90);

}  // namespace bmh

namespace bmh {

// CurrentMatchday = WatcomRandom(0, 10000), drawn by AdvanceTurn after the preparation (kept
// in the savegame).
inline void draw_matchday_number(GameState& g, WatcomRandom& rand) {
    const int v = rand.range(0, 10000);
    if (g.memory.has(::bmh::layout::CurrentMatchday)) g.memory.set32(::bmh::layout::CurrentMatchday, uint32_t(v));
}

}  // namespace bmh

namespace bmh {

// BuildPoolsCoupon, at a human manager's pre-match desk: the pools coupon (block DeskActions_SubmitPoolsCoupon_RuntimeField) - up
// to 11 of today's matches between clubs of similar strength (team_average 0x1ff within
// 17); CareerSession_DispatchToto_RuntimeField = their number.
// Teams without a squad (national teams) draw their average from `rand` (TeamRating).
void build_pools_coupon(GameState& g, const std::vector<MatchGroup>& groups, WatcomRandom* rand = nullptr);

// EvaluatePoolsCoupon(0) at full time of that matchday: the results of the coupon's matches,
// MatchdaySetup_EvaluatePoolsCoupon_RuntimeField[i] = 1 home win, 0 draw, 2 away win.
void evaluate_pools_coupon(GameState& g, const std::vector<MatchGroup>& groups);

// EvaluatePoolsCoupon(1) at the end of the matchday: with 11 matches on the coupon every human
// manager's tip (Coupon + manager * 0x14: stake u32, tips +8..+0x12) is settled - the
// stake is booked (BookMoney2, may fail: "Ihr Tototipp wurde nicht berücksichtigt ..."
// MatchdaySetup_SettlePoolsCoupon_TextPointer), the hits counted (+0x13), from 9 on the stake times the quota
// (MatchdaySetupRuntimeField13: 10, 20, 50) is won (+4) and booked; the stake is cleared. The coupon's
// matches are counted again in the groups of the matchday (CareerSession_DispatchToto_RuntimeField; the outcomes stay
// those read after the second half, PoolsOpenFlag / MatchdaySetup_EvaluatePoolsCoupon_RuntimeField). The toto screen
// it shows (LeagueTableRangeDialog) draws nothing.
void settle_pools_coupon(GameState& g, const std::vector<MatchGroup>& groups);

}  // namespace bmh
