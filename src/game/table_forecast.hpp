// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>

#include "game/random.hpp"
#include "game/table.hpp"

namespace bmh {

// Scratch result of the Table screen's one-shot KI projection. `game_after`
// contains Source G after all per-ComputeLeagueTableForecast reseeds/draws; callers commit it
// only with the accepted UI/session transition. Native results and CurrentMatchday are
// never changed here.
struct LeagueTableForecast {
    LeagueTable table;
    GameRandom game_after{0};
    int first_simulated_round = 0;
    int simulated_rounds = 0;
};

// Only the reviewed E37 normal family entry (Source param2 == 0) is supported.
// All work is local: invalid input or a projection exception cannot mutate the
// caller's GameRandom or GameState.
LeagueTableForecast compute_league_table_forecast(
    const GameState& game, int league, int source_param2, int matchdays,
    int flags, int normal_mode, int sort_extra,
    const std::array<uint8_t, 10>& gap, const GameRandom& game_random);

} // namespace bmh
