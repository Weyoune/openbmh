// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Read-only queries for the game statistics menu of the Modern UI ("Spielstatistik";
// docs/new_engine/statistik.md). They evaluate the football almanac (docs/new_engine/
// almanach.md) of the running career and never change the game.
//
// A request is a JSON object with a `kind`; the answer is {ok: true, ...} or
// {ok: false, error}:
//   {kind: "competitions"}            leagues and cups with recorded results, per season
//   {kind: "winners", competition?}   the winners of every competition by season
//   {kind: "player_stats", competition, season?, sort?, limit?}
//                                     goals, assists, cards per player in one competition,
//                                     of one season ("2026/27") or all seasons

#include "cli/json.hpp"
#include "modern/game/modern_state.hpp"

namespace bmh::modern {

cli::Json query_statistics(const ModernState& g, const cli::Json& request);

}  // namespace bmh::modern
