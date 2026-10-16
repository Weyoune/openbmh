// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <functional>
#include <vector>
#include "game/reality_ball.hpp"

namespace bmh {
// EventImpl(absolute squad slot,1,figure>10). Must execute the real forced
// card/injury routine and return its byte. No default substitute is supplied.
using RealityForcedEvent = std::function<uint8_t(int slot,int side)>;
using RealitySound = std::function<void(uint8_t)>;
struct RealityFoulCue { int actor = -1, code = 0; }; // -1: Source leaves that actor slot unchanged
struct RealityFoulResult {
    uint8_t result = 0, event_result = 0;
    uint8_t event_kind = 0; // MatchEngine_Minute_ExecutableField, return byte with bit7 stripped
    int scene_status = 0, restart = 0;
    bool whistle = false, event_stops_scene = false;
    std::vector<RealityFoulCue> cues;
};
// RealityFoul up to/through the caller-owned forced-event boundary.
RealityFoulResult reality_foul(const GameState&,const RealitySetup&,int offender,bool defending,
                              int ball_actor,uint8_t referee,bool muted,WatcomRandom&,
                              const RealityForcedEvent&, const RealitySound& sound = {});
struct RealityDribbleDecision {
    RealityBallDecision ball;
    int actor_target_x = 0, actor_target_y = 0, steps = 2;
    bool lost_duel = false;
    int scene_status = 0, restart = 0;
    bool event_stops_scene = false;
    std::vector<RealityFoulCue> cues;
    std::optional<uint8_t> card_kind; // MatchEngine_Minute_ExecutableField changes only on a nonzero forced return
};
// ApplyRealityBall dribble/duel continuation; never reruns the earlier selection draws.
RealityDribbleDecision continue_reality_dribble(const GameState&,RealitySetup&,const LeImage&,const RealityBallDecision&,
                                              uint8_t referee,bool muted,WatcomRandom&,
                                              const RealityForcedEvent& forced_event = {}, const RealitySound& sound = {});
RealityBallOutcome apply_reality_dribble(const GameState&,RealitySetup&,const RealityDribbleDecision&,
                                        RealityBallHistory&,const RatingContext&,uint8_t referee,
                                        WatcomRandom&,GameRandom&);

} // namespace bmh
