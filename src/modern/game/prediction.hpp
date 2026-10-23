// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/prediction.hpp"
// Table prediction and bonus expectations of human managers (BMH.exe V2.24).
//
//   table_prediction   Void5  which final ranks a club can still reach / has secured
//   expected_bonus     ContractOffer  bonus a player on the tactics board can expect
//   season_bonus       SeasonBonus  the board's season bonus (title, Europe, staying up)
//
// team_rating (rating.cpp) uses them for the players of human managers: a player who can
// expect a large bonus relative to his base pay tries harder (+5 with some probability).
// As a side effect the prediction posts the "still possible" / "no longer possible"
// messages about the season goals (champion, Europe, relegation) to the manager, once per
// goal and season (bits 0-5 of manager record +0x28b).

#include <array>
#include <cstdint>

#include "modern/game/modern_state.hpp"

namespace bmh::modern {

// Void5(team, mode, buf). Fills buf[0..23], one entry per final rank:
//   0     the rank can no longer be reached (at least that many clubs stay ahead)
//   1     the predicted rank (modes other than 1)
//   0xFF  a rank the club cannot fall to any more
// and returns the first rank (1-based) that is not 0, i.e. the best reachable rank.
// mode 1: maximum points = points + 3 per remaining game for every club (and messages);
// mode 2: the club's own points per game projected, the others stay where they are;
// mode 3: every club projected with its own points per game;
// mode >= 4: as 3, but the points per game over the last 5 * (mode / 4) matchdays.
// `size_word` receives the league size, which the original stores as a word in its frame
// (local_2c) unless it returns early (before the second matchday etc.; then it stays
// untouched). The side ratings read that stack word as a stale value, see team_rating.
// Returns 0 before the second matchday and after the last one (and for leagues >= 9),
// 9 for clubs outside the league system (ids from 138 on).
int table_prediction(ModernState& g, int team, int mode, std::array<uint8_t, 24>& buf, int* size_word = nullptr);

// ContractOffer(manager, predicted rank, board slot, player, 0): goal, point and appearance
// bonus of the board slot, each weighted by how often it is earned (goals by position:
// PredictionRuntimeField3, points 34, appearances 25 per season), plus the season bonus for the
// predicted rank, all divided by 12. `rank` 0xFF means: predict it (table_prediction mode 4).
// Only the variant without the fitness correction (last argument 0) is ported.
uint32_t expected_bonus(ModernState& g, int manager, int rank, int board_slot, int player, bool fitness = false);

// Checked FindManagerForClub record4 variant for a club parked during national takeover.
// Read the actual club from record4 without adding it to the active manager list.
uint32_t expected_bonus_parked_club_source(ModernState& g, int rank, int board_slot, int player, bool fitness = false);

// SeasonBonus(manager, board slot, rank, 0): season bonus of the slot for a final rank:
// title (league 0 only, rank 0), top 5, top 10, staying up (bonus indices 3-6 of the slot,
// the first one that is set and applies). Not paid.
uint32_t season_bonus(ModernState& g, int manager, int board_slot, int rank, bool pay = false);

}  // namespace bmh::modern
