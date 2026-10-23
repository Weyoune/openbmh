// SPDX-License-Identifier: GPL-3.0-only
#include "modern/game/modern_state.hpp"

#include "modern/game/matchday.hpp"
#include "modern/game/prediction.hpp"

namespace bmh::modern {

namespace {

// The shared match core passes the GameState base; Modern games always hold a ModernState.
void statistic(GameState& g, int competition_type, int player, int manager, int slot, int kind, int minute) {
    player_statistic(static_cast<ModernState&>(g), competition_type, player, manager, slot, kind, minute);
}

int prediction(GameState& g, int team, int mode, std::array<uint8_t, 24>& buf, int* size_word) {
    return table_prediction(static_cast<ModernState&>(g), team, mode, buf, size_word);
}

}  // namespace

const SeasonHooks& season_hooks() {
    static const SeasonHooks hooks{&statistic, &prediction};
    return hooks;
}

}  // namespace bmh::modern
