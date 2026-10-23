// SPDX-License-Identifier: GPL-3.0-only
#include "modern/game/source_tip.hpp"

#include <bit>
#include <cstdint>

namespace bmh::modern {

uint8_t source_tip_result(GameRandom& game, const std::array<uint8_t, 10>& gap,
                          int home_rating, int away_rating,
                          int home_secondary, int away_secondary) {
    uint8_t home = uint8_t((away_secondary + 0x14 < home_secondary) + game.range(0, 1));
    uint8_t away = uint8_t((away_secondary < home_secondary) + game.range(0, 1));
    const uint32_t wrapped = uint32_t(home_rating) - uint32_t(away_rating);
    const int32_t signed_difference = std::bit_cast<int32_t>(wrapped);
    int32_t scaled = signed_difference / 10;
    if (scaled < 0) scaled = -scaled;
    uint8_t local_c = uint8_t(scaled);
    if (home_rating > away_rating) {
        if (local_c > 9) local_c = 9;
        home = uint8_t(home + game.range(0, int(gap[local_c]) + 1));
    }
    if (home_rating < away_rating) {
        // ComputeLeagueTableForecast negates the byte before clamping it to the gap table.
        local_c = uint8_t(-int(local_c));
        if (local_c > 9) local_c = 9;
        away = uint8_t(away + game.range(0, int(gap[local_c])));
        if (local_c < 5) home = uint8_t(home + 1);
    }
    for (;;) {
        const int cap = game.range(2, 6);
        if (int(home) + int(away) <= cap) break;
        if (home) --home;
        if (away) --away;
    }
    return uint8_t((home << 4) | away);
}

} // namespace bmh::modern
