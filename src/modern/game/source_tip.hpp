// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "modern/game/modern_state.hpp"
#include "game/source_tip.hpp"

#include <array>
#include <cstdint>

#include "game/random.hpp"

namespace bmh::modern {

// ComputeLeagueTableForecast's pure score arithmetic after the caller has obtained the four
// QuoteNationalFriendlyMatch ratings. Each invocation consumes the same G draws, including
// the final goal-cap draw that terminates the loop.
uint8_t source_tip_result(GameRandom& game, const std::array<uint8_t, 10>& gap,
                          int home_rating, int away_rating,
                          int home_secondary, int away_secondary);

} // namespace bmh::modern
