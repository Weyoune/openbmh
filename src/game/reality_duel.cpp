// SPDX-License-Identifier: GPL-3.0-only
#include "game/reality_duel.hpp"

#include <algorithm>
#include <cstdlib>

namespace bmh {
namespace {
int source_manager(const GameState& state,int team) {
    const int m=state.manager_of(team);if (m>=0) return m;
    if (!state.manager_teams.empty() && state.manager_teams[0]==kNoTeam &&
        state.manager_records.size()>=4*0xb0c+0x292 && le16(state.manager_records,4*0xb0c+0x290)==team) return 4;
    return -1;
}
}

RealityFoulResult reality_foul(const GameState& state,const RealitySetup& scene,int offender,bool defending,
    int ball_actor,uint8_t referee,bool muted,WatcomRandom& rand,const RealityForcedEvent& forced_event, const RealitySound& sound) {
    if (offender<0 || offender>=22 || ball_actor<0 || ball_actor>=22)
        throw FormatError("Reality foul: invalid figure");
    const auto& placement=scene.players[size_t(offender)];const int slot=placement.squad_slot;
    if (slot<0 || size_t(slot)>=state.squad_slots.size()) throw FormatError("Reality foul: missing Source squad slot");
    const int player=state.squad_slots[size_t(slot)]&0xfff;
    if (player==0xfff || size_t(player)>=state.players.size()) throw FormatError("Reality foul: missing Source player");
    const int team=scene.teams[size_t(offender/11)],manager=source_manager(state,team);
    const auto& p=state.players[size_t(player)];
    int chance=std::clamp((int((p.w35c>>14)&127)-int((p.w35c>>7)&127))*4+100,0,200);
    // Literal RealityFoul indexes by player ID minus squad start, not roster row.
    const int board_row=std::clamp(player-int(squad_range(team).first),0,24);
    if (manager>=0) {
        const size_t at=size_t(manager)*0x840+size_t(board_row)*0x58+5;
        if (at>=state.tactics.size()) throw FormatError("Reality foul: Source board row outside supplied memory");
        chance=(int(state.tactics[at])+chance)*100/239;
    }
    RealityFoulResult result;if (rand.range(0,100)>=chance) return result;
    result.cues.push_back({ball_actor,defending ? 19 : 18});
    result.whistle=rand.range(0,1)==0 && !muted;
    if (result.whistle && sound) sound(21); // RealityFoul, before the forced event.
    result.result=2;
    if (rand.range(0,(referee&3)+1)==0) return result;
    result.cues.push_back({-1,20}); // RealityFoul does not assign MatchEngine_Minute_ExecutableField2 for this cue.
    if (!forced_event) throw FormatError("Reality foul: real forced card/injury engine callback required");
    result.event_result=forced_event(slot,offender>10);
    result.event_kind=result.event_result&0x7f;
    result.result=3;
    if (result.event_result) {
        result.cues.push_back({defending ? offender : ball_actor,21});result.event_stops_scene=true;
    }
    if (!defending) result.scene_status=4;
    else {
        const auto ball=scene.grid.ball();const int distance=42-std::abs(scene.attacking_side*42-ball.first);
        result.restart=distance<7 && ball.second>3 && ball.second<21 ? 2 : 1;
    }
    return result;
}

RealityDribbleDecision continue_reality_dribble(const GameState& state,RealitySetup& scene,const LeImage& exe,const RealityBallDecision& decision,
    uint8_t referee,bool muted,WatcomRandom& rand,const RealityForcedEvent& forced_event, const RealitySound& sound) {
    if (decision.boundary!=RealityBallBoundary::dribble_or_duel || decision.actor<0 || decision.actor>=22 ||
        decision.side!=scene.attacking_side) throw FormatError("Reality dribble: invalid continuation");
    RealityDribbleDecision result;result.ball=decision;result.ball.action=2;result.ball.airborne=0;
    result.ball.receiver_offside=false;
    if (decision.speed_ability<rand.range(0,130)) result.steps=1;
    auto foul=[&](int offender,bool defending) {
        const auto f=reality_foul(state,scene,offender,defending,decision.actor,referee,muted,rand,forced_event,sound);
        if (f.scene_status) result.scene_status=f.scene_status;if (f.restart) result.restart=f.restart;
        result.event_stops_scene=result.event_stops_scene||f.event_stops_scene;
        if(f.event_result)result.card_kind=f.event_kind;
        result.cues.insert(result.cues.end(),f.cues.begin(),f.cues.end());return f.result;
    };
    if (decision.opponent_at_actor) {
        bool attacking_foul=false;const int roll=rand.range(0,3);
        if (roll==0) attacking_foul=true;
        else if (roll!=3) {
            const int own_roll=rand.range(0,10);const int other_roll=rand.range(0,10);
            if (decision.actor_duel+own_roll<decision.opponent_duel+other_roll) attacking_foul=true;
        }
        if (attacking_foul) {
            if (rand.range(0,1)!=0 || !(foul(decision.actor,false)&2)) result.lost_duel=true;
        } else if (decision.goal_distance<rand.range(12,15) &&
                   (foul(decision.opponent_at_actor-1,true)&2)) result.lost_duel=true;
        if (result.lost_duel) result.steps=1;
    }
    const auto ball=scene.grid.ball();const auto goal=reality_goal_path(ball.first,ball.second,decision.side ? 0 : 42);
    auto target=goal.points[goal.count-1];
    if (!(decision.goal_blockers<2 && decision.goal_distance<12 && decision.opponent_nearby<2)) {
        const auto space=reality_seek_space(scene,exe,decision.actor,decision.side,0x11,0,goal);
        target=space.found ? std::pair{space.x,space.y} : scene.grid.position(decision.actor);
    }
    scene.grid.stage_position(decision.actor,target.first,target.second);
    result.actor_target_x=target.first;result.actor_target_y=target.second;
    result.ball.target_x=result.lost_duel ? ball.first : target.first;
    result.ball.target_y=result.lost_duel ? ball.second : target.second;
    return result;
}

} // namespace bmh
