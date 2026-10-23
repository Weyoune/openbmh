// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Turns the Classic start of a Modern world into the Modern world (docs/new_engine, M3).
//
// The Classic start (startup_state from the world's Classic payload) has the original
// dimensions: 138 league clubs, 2574 players. expand_to_world gives it the dimensions of the
// world: league clubs in the order of the world's league members (international leagues of
// 20/20/18/20 move the ids of every club behind them), the added clubs and players, squads of
// 24 slots for every league club built from the world's squads, the world layout, the league
// sizes and the league dates. Clubs the data set added take the per-club starting values of
// an original club of the same league.

#include <string>
#include <vector>

#include "modern/content/world.hpp"
#include "modern/game/modern_state.hpp"

namespace bmh::modern {

struct ExpandReport {
    int league_clubs = 0, added_clubs = 0, added_players = 0, free_agents = 0;
    int fallback_nationalities = 0;      // players whose nation has no Classic code
    std::vector<std::string> notes;      // moved league dates and similar remarks
};

// Requires the Classic layout (a fresh startup state) and installs the world's layout,
// league sizes and calendar. Throws FormatError when the world does not fit.
ExpandReport expand_to_world(ModernState& g, const World& world);

// Installs the dimensions of the world for a running Modern game: layout, league sizes, the
// per-league fixture tables and the first newcomer club. A new career gets them from
// expand_to_world, a loaded one from the session.
void install_world_dimensions(const World& world);

// Installs the world's training camp catalogue, or the built-in one when the world has none
// (docs/new_engine/trainingslager.md). Classic data keeps the eight original resorts.
void install_training_camps(ModernState& g, const World& world);

// The UTF-8 text of a name in the single-byte encoding of the engine texts (ISO 646-DE;
// letters outside it lose their accents, other characters become '?').
std::string legacy_encoding(const std::string& utf8);

}  // namespace bmh::modern
