// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/state.hpp"
namespace bmh {
// ClubInfoFromGame -> LineupScreen(..., param11=1): PrepareLineupEntry before
// drawing, PrepareClubInfoEntry after drawing. No player selection or RNG.
// Applies to computer and human domestic squads and the national squad.
// Invalid team/range/player IDs throw before any mutation.
struct ClubInfoEntry {
    std::vector<uint16_t> displayed_slots; // normalized flags before post-draw injury cleanup
};
ClubInfoEntry prepare_club_info_entry(GameState& g, int team);
// ClubInfoFromGame numeric icon -> SetClubInfoCategory. Returns the applied raw
// category (0..2); may demote another computer club's category2 to1.
uint8_t set_club_info_category(GameState& g, int team, int requested);
}
