// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/league_pairings.hpp"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "modern/game/modern_state.hpp"
#include "game/random.hpp"

namespace bmh::modern {


// Normal LeagueScreenShown(2,0)->Move(0) only. The App owns the staged Native/W/G
// transaction. No mutator here calls a presenter, advances a day, or saves.

LeaguePairingsOwner open_league_pairings_source(ModernState&,int manager,const LeaguePairingsConstants&);
LeaguePairingsOwner edit_league_pairings_source(ModernState&,const LeaguePairingsOwner&,
    const LeaguePairingsEvent&,const LeaguePairingsConstants&,GameRandom&,WatcomRandom&);
// Pure, bounded Source projection; renderers never call a Native/RNG mutator.
LeaguePairingsView league_pairings_view_source(const ModernState&,const LeaguePairingsOwner&,
    const LeaguePairingsConstants&);
// Read only active process descriptors, never reconstruct weather/referees from
// saved bytes or run event preparation. Last first-pair match follows Move.
std::optional<LeaguePairingsActivePacket> league_pairings_active_packet_source(
    const ModernState&,const LeaguePairingsOwner&,const std::vector<MatchGroup>&,
    std::span<const uint8_t> referee_profiles);
void validate_league_pairings_active_packet_source(const ModernState&,const LeaguePairingsOwner&,
    const LeaguePairingsActivePacket&);
bool league_pairings_played_source(uint8_t raw);
bool league_pairings_postponed_source(uint8_t raw);

} // namespace bmh::modern
