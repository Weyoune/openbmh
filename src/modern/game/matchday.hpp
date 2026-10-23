// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/matchday.hpp"
// What happens to a matchday after the final whistle (BMH.exe V2.24 + supported rule profile patch).
//
// The matchday driver (the function around 0x39xxx that calls EndPhase for every
// phase) continues after the last phase with, per match group and match:
//   1. UpdateClubTable  league record of both clubs (points, goals, streaks, records)
//   2. morale        both teams' morale moves towards the result (Watcom rand)
//   3. ManagerMatchRecord  a human manager's match record, bonuses and rating
//   4. DevelopPlayers  player development: team trends move form/technique/condition and
//                    skills (game RNG), appearances, injuries heal by one matchday,
//                    yellow card bans, card counters per competition
// Before that, the end of every phase calls FinishMatch per match: it stores the end
// minute and lets a human manager's players get used to their places on the tactics
// board and earn their bonus (game RNG).

#include <cstdint>
#include <vector>

#include "game/match_engine.hpp"
#include "game/random.hpp"
#include "game/sportschau_presentation.hpp"
#include "modern/game/modern_state.hpp"

namespace bmh::modern {

// Score of match m of a group (MatchScore, read mode): league result byte or the cup
// result of the current leg. Returns false if the score is not known.
bool match_score(const MatchGroup& g, int m, int& home, int& away);

// BookMoney2: books `amount` from a human manager's account (record +0x314). Without
// `force` a payment larger than balance + credit (+0x31c) is refused (the original shows a
// message). Game level 0 has no money at all.
// With `statistics` (the caller sets FinanceStatisticsEnabled) the amount is also added to the
// income (+0x874, negative amounts) or expense (+0x8a0) statistics.
bool book_money(ModernState& g, int manager, int32_t amount, bool force, bool statistics = false);

// PayBonus: bonus of one player on the tactics board: the board's bonus rate for
// `index` (slot byte 0x12 + index: 0 goal, 1 point, 2 appearance, ...) times the player's
// bonus base (slot +0x50) / 100, five-fold for index > 2. With `pay` the tax (record
// +0x172) is deducted and the amount is paid. Returns the amount.
uint32_t pay_bonus(ModernState& g, int manager, int board_slot, int index, bool pay);

// PlayerStatistic: statistics of one player. kind 0 appearance, 1 goal, 2 assist.
// competition_type = descriptor byte 0; `manager` = human manager of the player's team
// or -1; `slot` = the player's slot relative to the squad start (tactics board index).
void player_statistic(ModernState& g, int competition_type, int player, int manager, int slot, int kind, int minute);

// FinishMatch for match m at the end of a phase lasting `minute` minutes.
void finish_match(ModernState& g, MatchGroup& grp, int m, int minute, GameRandom& game);

// UpdateClubTable: league record of home and away club after a result.
void update_club_table(ModernState& g, int home, int away, int home_goals, int away_goals);

// Morale step of the matchday driver for one side (0 home, 1 away) of a finished match.
void update_morale(ModernState& g, int team, int own_goals, int opp_goals, int side, WatcomRandom& rand);

// DevelopPlayers: player development and bookkeeping for both teams of match m.
void develop_players(ModernState& g, const MatchGroup& grp, int m, GameRandom& game, WatcomRandom& rand);

// Steps 1, 2 and 4 for every match of a group that was played (voided matches, e.g.
// awarded 0:2, are skipped). Human records and bonuses use manager_match_record.
void process_group(ModernState& g, const MatchGroup& grp, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
                   int group_index = -1, const SportschauFixtureHook& sportschau = {},
                   const std::vector<ShowEvent>* show_events = nullptr,
                   const SportschauReplayHooks* replay = nullptr);
// Loaded Source caller retains mutable descriptor award ownership; legacy raw
// probes without executable policy keep their existing immutable input API.
void process_group(ModernState& g, MatchGroup& grp, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
                   int group_index, const SportschauFixtureHook& sportschau,
                   const std::vector<ShowEvent>* show_events, const SportschauReplayHooks* replay,
                   const SportschauRatingTables* ratings);

// ManagerMatchRecord: a human manager's record of match m (last results and fixtures, league
// win bonus, the manager rating via ManagerMatchRecord2 and the results per opponent).
void manager_match_record(ModernState& g, const MatchGroup& grp, int m, int home_goals, int away_goals, MinuteReport& report);
// TablePosition: current table position of a team (-1 before the first round).
int table_position(const ModernState& g, int team);
// ManagerMatchRecord2: adds a match rating (0-200) to a manager's history of competition
// `competition` and stores the weighted average in the manager record (+0x8aa).
void rate_manager(ModernState& g, int manager, int rating, int competition);

// StirRandom: when a result or table screen (LeagueScreenShown) is left, the game stirs its
// random numbers: WatcomRandom(100, 300) game draws are thrown away.
void stir_random(GameRandom& game, WatcomRandom& rand);

// LeagueScreenShown(param_1, param_2): whether the screen is shown at all. param_1 1 = table,
// 2 = results; param_2 = the phase end minute (90 / 120 / 45 ...); league = descriptor
// +0xcc (CareerSession_BeginFriendlyOpponentTable_RuntimeField2). The desk buttons (block LeaguePresentationOptions, 9 bytes per league) switch it off.
bool league_screen_shown(const std::vector<uint8_t>& buttons, int param_1, int param_2, int league);

// The rest of AdvanceTurn after a matchday: for every competition bit of the day (bits
// 0-4 = leagues) the round counter LeagueRoundCounters advances, the table is recomputed and
// each club's rank is added to the position history (*LeagueRankHistory); human managers get their
// season position and strength history updated. This API accepts league bits only;
// finish_event dispatches bits 5+ separately through advance_cup.
void advance_league_rounds(ModernState& g, uint32_t competitions, WatcomRandom& rand, MinuteReport& report,
                           const CompetitionWinnerHook& winner = {});
// TicketPriceDraws: the random draws of a human manager's (disabled) ticket price drift.
void ticket_price_draws(const ModernState& g, int manager, WatcomRandom& rand);

// Modern version of the function sportschau_presentation.hpp declares for Classic.
int sportschau_source_setting_index(const ModernState& state, const MatchGroup& group);

}  // namespace bmh::modern
