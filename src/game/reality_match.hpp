// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "game/match_engine.hpp"
#include "game/reality_scene.hpp"

namespace bmh {
struct RealityDeferredRequest {
    DeferredRealityChange source; // literal bdb14/16/18
    LiveMatchEvent event; // actual fixed offender, fixture, manager and minute
    int offender_slot = -1;
};
// Owned bridge between one actual fixture and the independent Source scene.
// Requests survive scene end, but do not execute while its figures still move.
struct RealityMatchScene {
    RealityScene scene;
    size_t group = 0;
    int match = -1, minute = 0;
    std::array<int,2> teams{};
    uint32_t date = 0;
    const MatchGroup* fixture_owner = nullptr;
    size_t group_count = 0;
    const GameState* state_owner = nullptr;
    const WatcomRandom* rand_owner = nullptr;
    const GameRandom* game_owner = nullptr;
    std::optional<RealityDeferredRequest> pending;
    bool settled = false, settling = false;
};
void initialize_reality_match_scene(MatchEngine&,size_t group,int match,int side,int minute,
    const LeImage&,RealitySceneContext,RealityMatchScene&);
RealitySceneTick advance_reality_match_scene(MatchEngine&,RealityMatchScene&,const LeImage&,
    MinuteReport&,bool user_exit = false);
// RealityReporterPanel tail. Returns true for an executed request (even a legal automatic
// no-change). Empty/repeated completion is drawless. Missing human presenter
// preserves the request and reports the existing unsupported boundary; only
// explicit leave_screens allows the legacy headless skip.
bool settle_reality_match_scene(MatchEngine&,RealityMatchScene&,MinuteReport&);
} // namespace bmh
