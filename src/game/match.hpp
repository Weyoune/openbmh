// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Match simulation - structure of the original, port in progress.
//
// This header records what is known about the original's match engine so the port can
// follow it step by step. Everything below refers to BMH.exe V2.24 function addresses.
//
// ---------------------------------------------------------------------------------------
// Matchday flow (FinishEvent)
// ---------------------------------------------------------------------------------------
//  1. For every competition playing today a "match group" descriptor (0xD0 bytes, up to 6
//     in table MatchdayGroupDescriptors) is set up by FixturesFromGenerateFixtures: the fixtures from generate_fixtures(),
//     the number of matches, and a pointer to the result bytes in block LeagueResults.
//  2. Per match an 18-byte record is allocated (two 9-byte "side" records, home first)
//     and filled by BuildSideRecords3 (see "Side record").
//  3. Teams with fewer than 8 fit players lose 0:2 and are fined 200 000.
//  4. The match loop EndPhase(first_minute, last_minute, max_stoppage, phase) runs
//     for 1..45, 46..90 (+ up to 4 minutes stoppage), and for cup ties 91..105 and
//     106..120. Indoor tournaments (CareerSessionRuntimeField set) play 1..20 and 21..40.
//
// ---------------------------------------------------------------------------------------
// Side record (9 bytes per team and match, BuildSideRecords3 / Uint8T3)
// ---------------------------------------------------------------------------------------
//  [0..5] six ratings from TeamRating(team, weight table at layout field + i * 10): the
//         line-up is walked, every player adds a position-dependent value to one of
//         4 x 3 buckets (goalkeeper / defence / midfield / attack x left/centre/right),
//         the buckets are weighted and averaged; result capped at 110.
//         [1..3] are scaled by (players - 3) / 10 if fewer than 11 players are fit.
//  [2]    additionally reduced by a home/away and fatigue modifier clamped to -10..+10.
//  [6]    (avg condition - avg technique) * 4 + 100, clamped 0..200
//         (team_average with flags 0x1F1 and 0x1F2).
//  [7]    human teams: (manager bonus * 5 + [6]) * 100 / 255, else [6] / 2 (min 50);
//         clamped to 44..80 where it is used as the chance rhythm (MatchRuntimeField2).
//  [8]    read as "local_a4" together with byte 0x11 of the other side.
//
// ---------------------------------------------------------------------------------------
// Per minute, per match, per side (EndPhase, around 0x382xx)
// ---------------------------------------------------------------------------------------
//  - After the regular time every running match ends with probability 1/2 per minute
//    (Watcom rand, WatcomRandom::range(0, 1)).
//  - Random events (injuries, cards) are triggered with WatcomRandom::range(0, 220) <
//    rhythm - 30 and EventImpl.
//  - Chance roll: GameRandom::range(0, pressure * 4 + 44) compared against
//    side[8] + other[0x11] + 3 + 14 * indoor; above means no chance this minute.
//  - Otherwise the chance quality is built from side[2], GameRandom::range(0, rhythm) / 10,
//    minute dependent bonus GameRandom::range(6, 12) while minute < side[6] (50..130),
//    the opponent's [0xB] and [0xF] bytes and GameRandom::range(0, 100); the difference
//    indexes the table at layout field (index 5..24) which gives the goal threshold, tested
//    against GameRandom::range(0, pressure * 4 + 56).
//
// ---------------------------------------------------------------------------------------
// Randomness and reproducibility
// ---------------------------------------------------------------------------------------
//  Goals are decided by GameRandom (GameRandom), but its seed comes from
//  CurrentMatchday = WatcomRandom::range(0, 10000) (AdvanceTurn) or the saved CareerSession_Invalid_RuntimeField2,
//  and WatcomRandom (seeded from the clock at program start) also decides stoppage time
//  and random events. A matchday can therefore only be reproduced bit-exactly when both
//  generator states at kick-off are known (e.g. read from memory in the DOSBox debugger).

#include <array>
#include <cstdint>

#include "game/random.hpp"
#include "game/state.hpp"

namespace bmh {

// The 9-byte record of one team in one match (see "Side record" above).
struct SideRecord {
    std::array<uint8_t, 9> b{};
    uint8_t goalkeeping() const { return b[0]; }
    uint8_t attitude() const { return b[8]; }  // 0 defensive .. 3 all-out attack
};

// Per-match input of the side record setup that comes from the match group descriptor.
struct MatchSetup {
    int home = 0, away = 0;       // team ids
    int match_index = 0;          // position inside the match group (affects the attitude quirk)
    bool climate_effect = false;  // descriptor flag bit 1 (set for league matchdays)
    int temperature = 0;          // descriptor byte 0x30 + match: temperature at the venue in °C
    int spectators = 0;           // result of the spectator calculation (Attendance)
    int month = 0;                // CareerSession_PairingsRuntimeDraw_ExecutableField5, index into the monthly temperature table
};

// Builds both side records of a match like BuildSideRecords does at kick-off: for the home
// side and then the away side it runs BuildSideRecords3 (ratings, home/climate modifier,
// condition-technique balance, rhythm) followed by BuildSideRecords2 at 0:0 (initial attitude).
// Order matters: the home attitude is computed while the away record is still empty.
//
// Draws from `rand` exactly like the original (home bonus roll, climate roll for away
// teams from other regions, and the 1/201 attitude roll per side). Teams of human
// managers also draw from `game` (see team_rating) and may keep their own attitude.
std::array<SideRecord, 2> build_side_records(GameState& g, const MatchSetup& m, WatcomRandom& rand,
                                           GameRandom* game = nullptr);

// BuildSideRecords3 for one side: recomputes bytes 0-7 of a side record (byte 8, the attitude,
// is left alone). Used at kick-off and during the match after line-up changes.
//   previous6  byte 6 of the record MatchRuntimeField points to when the original calls it
//              (at kick-off the previously built record, during the match the side's own)
void rate_side(GameState& g, const MatchSetup& m, int side, int previous6, WatcomRandom& rand, uint8_t* record,
               GameRandom* game = nullptr);

// Attitude of a computer team (BuildSideRecords2), 0 defensive .. 3 all-out attack.
//   own/other      the two side records (byte 2 = attack/midfield strength is compared)
//   side           0 home, 1 away (home starts one step more offensive)
//   match_index    position in the match group; the "much stronger" step is skipped for
//                  match 0 (the original tests the match index where the minute was meant)
//   own_goals/opp_goals, minute: behind by 1-2 -> +1 (+2 after minute 80),
//                  ahead by 1 -> -1 after minute 75, another -1 if weaker or late
// Draws from `rand`: 1/201 "special" roll when no score rule applied, and after minute 60
// a 1/61 roll that asks for a substitution: substitution_mode is then the mode for
// substitute() (special code - 1), otherwise -1.
int attitude_update(const uint8_t* own, const uint8_t* other, int side, int match_index, int own_goals, int opp_goals,
                    int minute, WatcomRandom& rand, int& substitution_mode);

// OpenBMH deviates from the original: uses the enhanced injury selection and suspension rules.
// InjurePlayer(slot, type, match, manager) with its supported rule profile continuation: injures the
// player in the absolute squad slot. The type passed in is replaced by a drawn one
// (WatcomRandom(0, 999)); returns the type. A human manager's club doctor shortens the
// absence. Players with w364 bits 13-15 set are protected.
int injure_player(GameState& g, int slot, int type, bool match, int manager, WatcomRandom& rand);

// Region of a team for the climate effect (TeamRegion): 0 for the top five leagues,
// 1..4 for the four regional leagues, higher values for amateur teams (7 per region).
int team_region(int team);

// Temperature offset of a region (MatchRuntimeField3[region]) and the average temperature of a
// month (MatchRuntimeField4[CareerSession_PairingsRuntimeDraw_ExecutableField5]).
int region_temperature(int region);
int month_temperature(int month);

}  // namespace bmh
