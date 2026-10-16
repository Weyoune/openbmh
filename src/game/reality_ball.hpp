// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "game/rating.hpp"
#include "game/reality_space.hpp"

namespace bmh {

struct RealityBallHistory {
    int current_actor = 0xfff, previous_actor = 0xfff; // afb08/afb06
    uint8_t airborne = 0; // afb72
};
enum class RealityBallBoundary { direct_move, dribble_or_duel };
struct RealityBallDecision {
    RealityBallBoundary boundary = RealityBallBoundary::direct_move;
    int actor = 0, side = 0, previous_actor = 0xfff;
    int action = 0, score = 0, dribble_score = 0; // afb7a:0 shot,1 pass; source shorts
    int goal_distance = 0, shooting = 0, passing = 0, teamplay = 0;
    int original_x = 0, original_y = 0, goal_blockers = 0, speed_ability = 0;
    uint8_t actor_duel = 0, opponent_duel = 0;
    int receiver = -1, receiver_player = 0xfff, target_x = 0, target_y = 0;
    uint8_t airborne = 0, opponent_at_actor = 0, opponent_nearby = 0;
    std::array<uint8_t,2> line_selected{}; // afaec, one-based occupancy IDs
    int scatter_x = 0, scatter_y = 0, receiver_nearby = 0;
    bool intercepted = false, inaccurate = false, blocked_attempt = false;
    bool receiver_offside = false, leading_run = false;
};
// ApplyRealityBall prefix through the shot/pass-vs-dribble decision. Caller must clear
// the actor's old occupancy, as DecideRealityBall does. Uses Source rating context/RNG;
// no fabricated foul/dribble outcome and no reporter text draws.
RealityBallDecision decide_reality_ball(const GameState&, const RealitySetup&, int actor,
                                       const RealityBallHistory&, const RatingContext&, WatcomRandom&);
struct RealityBallOutcome {
    int action = 0, scene_status = 0, restart = 0, shooter = 0xfff, assistant = 0xfff;
    int shooter_row = 0xfff, assistant_row = 0xfff;
    bool received = false, shot_ready = false, needs_reporter = true;
    bool event_stops_scene = false, lost_duel = false;
};
// ApplyRealityBall's common tail for a selected shot/pass. Source Game/Watcom calls
// for airborne reception, boundary handling and pass offside are preserved.
// A dribble/duel decision is rejected before any mutation or additional draws.
RealityBallOutcome apply_reality_ball(const GameState&, RealitySetup&, const RealityBallDecision&,
                                      RealityBallHistory&, const RatingContext&, uint8_t referee,
                                      WatcomRandom&, GameRandom&);

} // namespace bmh
