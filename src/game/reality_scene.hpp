// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "game/reality_duel.hpp"
#include "game/reality_movement.hpp"
#include "game/reality_reporter.hpp"

namespace bmh {
// Caller-owned persistent Source context. MatchEngine_Minute_ExecutableField2 is not cleared by ApplyRealityBall:
// code20 leaves its actor slot unchanged. Supply the retained five slots;
// carrying these between scenes is the eventual MatchEngine owner's job.
struct RealitySceneContext {
    RatingContext rating;
    uint8_t referee_index = 0, referee_profile = 0; // afb73 vs RefereeProperties[index]
    bool muted = true, numbers = true;
    std::array<uint8_t,5> retained_cue_actors{};
    uint8_t retained_card_kind = 0;
    bool retained_deflection = false; // MatchEngine_Minute_ExecutableField3 survives until the next ball actor
    int retained_distance = 0; // MatchEngine_Minute_ExecutableField4 is also assigned only by a ball actor
    RealitySound sound; // presentation observer, never serialized
};
enum class RealitySceneStop { none, unavailable, goal_mouth, possession_ended, shot, boundary, forced_event, user_exit };
struct RealitySceneResult {
    RealitySceneStop stop = RealitySceneStop::none;
    uint8_t return_flags = 0, scene_status = 0, restart = 0;
    int side = 0, shooter = 0xfff, assistant = 0xfff;
    int shooter_row = 0xfff, assistant_row = 0xfff, distance = 0, keeper = 0xfff;
    bool deflected = false, shot_attempted = false; // MatchEngine_Minute_ExecutableField3 and bdb7b
};
struct RealityScene {
    RealitySetup setup;
    RealitySceneContext context;
    RealityBallHistory ball_history;
    RealityReporterState reporter;
    RealityReporterFrame reporter_frame;
    RealitySceneResult result;
    uint8_t stagnation = 0, defensive_push = 0, return_flags = 0;
    bool first_round = true, finished = false, shot = false, event_stop = false;
    size_t rounds = 0;
};
struct RealitySceneTick {
    size_t round = 0;
    int side = 0, ball_actor = -1;
    std::vector<int> moved_figures; // actual Source order, not a sorted view
    std::vector<RealityReporterResult> reports; // one layout flush per call
    std::optional<RealityMotionResult> motion; // omitted after a forced stop
    RealitySceneResult result;
    bool finished = false;
};
// RealityReporterPanel initialization and DecideRealityBall/RealityReporterPanel control flow. The real forced
// card callback runs at the existing Source boundary; any deferred change must
// be retained by that caller and settled after the returned scene finishes.
// MatchEngine calls this through the fixture-owned Reality adapter.
void initialize_reality_scene(GameState&,const std::array<int,2>&,int side,const RealityFormations&,
    const LeImage&,const RealitySceneContext&,WatcomRandom&,GameRandom&,RealityScene&);
void exit_reality_scene_after_animation(RealityScene&);
RealitySceneTick advance_reality_scene(GameState&,RealityScene&,const LeImage&,WatcomRandom&,GameRandom&,
    const RealityForcedEvent& forced_event = {},bool user_exit = false);
} // namespace bmh
