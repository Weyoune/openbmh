// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "game/reality_space.hpp"

namespace bmh {

// DecideRealityBall's per-iteration observations; no ball action is synthesized here.
struct RealityMoveContext {
    RealityPath goal_path;
    uint16_t attacking_nearby4 = 0, defending_nearby3 = 0;
    uint8_t attacking_nearby2 = 0, attacking_area = 0, defending_nearby2_forward = 0;
    uint8_t blocked_figure = 0, defensive_push = 0; // afb7c (one-based), afb87
    int keeper_player = 0xfff; // RatingRuntimeField8, retained across non-keeper moves
};
RealityMoveContext reality_move_context(const RealitySetup&, const LeImage&);
// DecideRealityBall's first-defender branch, including its short-circuit draw order.
void reality_defensive_push(const RealitySetup&, RealityMoveContext&, WatcomRandom&);
struct RealityMoveResult {
    int steps = 2, score = -100;
    bool returned_to_box = false;
    int reporter_code = 0; // InitializeRealityReporter code17 boundary; reporter RNG is NOT consumed here
};
// RealityMoveAttacker and RealityMoveAttacker2. Caller clears old occupancy as DecideRealityBall does.
// Positions/occupancy/next rendering coordinates and real movement RNG change.
// GameState is read-only. These are not yet called from MatchEngine/Main.
RealityMoveResult reality_move_attacker(const GameState&, RealitySetup&, const LeImage&, int figure,
                                       RealityMoveContext&, WatcomRandom&, GameRandom&);
RealityMoveResult reality_move_defender(const GameState&, RealitySetup&, const LeImage&, int figure,
                                       RealityMoveContext&, WatcomRandom&, GameRandom&);

} // namespace bmh
