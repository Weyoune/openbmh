// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/cups.hpp"
#include "game/random.hpp"

namespace bmh::world12 {
// Rank order is supplied by the table/season owner, never inferred from club IDs.
using CountryCupRanking=std::array<std::vector<ClubId>,5>;
// Installs the country's initial main cup(s), excluding winner-dependent supercups.
// RNG publication is atomic: invalid source rules or ranking leave it unchanged.
std::vector<KnockoutStage> initial_domestic_cup_fields(const World&,Country,
    const CountryCupRanking&,const LeImage&,GameRandom&);
// Returns a complete receipt with an Original next-round/successor draw when
// required. First legs and terminal finals consume no draw randomness.
CupLegResult draw_after_cup_leg(const World&,const KnockoutStage&,const std::vector<uint16_t>&,
    const LeImage&,WatcomRandom&);
}
