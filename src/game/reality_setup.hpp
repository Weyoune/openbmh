// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "game/reality_motion.hpp"
#include "game/state.hpp"
#include <optional>

namespace bmh {

using RealityFormations = std::array<std::array<std::pair<int,int>,11>,2>;
struct RealityPlacement {
    int player = 0, squad_slot = -1, card_value = 0;
    int center_x = 0, center_y = 0;
    int min_x = 0, max_x = 0, min_y = 0, max_y = 0;
};
struct RealitySetup {
    RealityGrid grid;
    RealityMotionInput motion;
    std::array<RealityPlacement,22> players{};
    std::array<std::array<uint8_t,7>,2> tactics{};
    std::array<int,2> teams{};
    int attacking_side = 0;
    bool ready = false;
};

// RealityMarkedPosition, shared by initialization and subsequent defensive movement.
std::optional<std::pair<int,int>> reality_marked_position(const GameState&, const RealitySetup&, int figure);

// Int2's interleaved prepared positions, supplied separately to the scene.
RealityFormations reality_formations(const GameState&, const std::array<int,2>& teams);
// RealityReporterPanel profile preparation + FormatError4 + initial sprite phases. This is a
// simulation operation: it consumes the real RNGs and can update board fatigue.
// It is not called by a renderer and is not yet connected to MatchEngine::side.
void initialize_reality(GameState&, const std::array<int,2>& teams, int attacking_side,
                        const RealityFormations&, const LeImage&, WatcomRandom&, GameRandom&, RealitySetup&);

} // namespace bmh
