// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/club_info.hpp"
#include "modern/game/modern_state.hpp"
namespace bmh::modern {
// ClubInfoFromGame -> LineupScreen(..., param11=1): PrepareLineupEntry before
// drawing, PrepareClubInfoEntry after drawing. No player selection or RNG.
// Applies to computer and human domestic squads and the national squad.
// Invalid team/range/player IDs throw before any mutation.
ClubInfoEntry prepare_club_info_entry(ModernState& g, int team);
// ClubInfoFromGame numeric icon -> SetClubInfoCategory. Returns the applied raw
// category (0..2); may demote another computer club's category2 to1.
uint8_t set_club_info_category(ModernState& g, int team, int requested);
}
