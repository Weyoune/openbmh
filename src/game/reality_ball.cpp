// SPDX-License-Identifier: GPL-3.0-only
#include "game/reality_ball.hpp"
#include "game/reality_duel.hpp"

#include <algorithm>
#include <cstdlib>

namespace bmh {
namespace {
int source_char(int v) { return int(int8_t(v)); }
int source_short(int v) { return int(int16_t(v)); }
int value(const GameState& state,const RealitySetup& scene,int figure,uint16_t attrs,int scale,
          int team,int x,int y,const RatingContext& context) {
    return player_value(state,scene.players.at(size_t(figure)).player,attrs,scale,team,context,x+30,y+30);
}
void validate(const GameState& state,const RealitySetup& scene,int actor) {
    if (actor<0 || actor>=22 || scene.attacking_side<0 || scene.attacking_side>1 ||
        actor/11!=scene.attacking_side || !scene.motion.figures[size_t(actor)].active)
        throw FormatError("Reality ball: invalid actor/side");
    for (int id=0;id<22;++id) if (scene.motion.figures[size_t(id)].active) {
        const int player=scene.players[size_t(id)].player;
        if (player<0 || size_t(player)>=state.players.size() || size_t(player)>=state.player_bonus.size())
            throw FormatError("Reality ball: missing player attributes/bonus");
    }
    for (int team:scene.teams) if (team<0 || size_t(team)>=state.team_morale.size())
        throw FormatError("Reality ball: missing team morale");
}
int blockers(const RealitySetup& scene,const RealityPath& path,int side,bool select,WatcomRandom& rand,RealityBallDecision& result) {
    const auto line=reality_line_occupants(scene,path,side,select,rand);
    result.line_selected[size_t(side)]=line.selected;return line.count;
}
} // namespace

RealityBallDecision decide_reality_ball(const GameState& state,const RealitySetup& scene,int actor,
    const RealityBallHistory& history,const RatingContext& rating,WatcomRandom& rand) {
    validate(state,scene,actor);RealityBallDecision result;result.actor=actor;result.side=scene.attacking_side;
    const int side=result.side,team=scene.teams[size_t(side)];const auto start=scene.grid.position(actor);
    const auto ball=scene.grid.ball();const auto goal=reality_goal_path(ball.first,ball.second,side ? 0 : 42);
    result.original_x=start.first;result.original_y=start.second;
    const auto goal_target=goal.points[goal.count-1];const auto& tactics=scene.tactics[size_t(side)];
    result.previous_actor=actor!=history.current_actor ? history.current_actor : history.previous_actor;
    result.target_x=ball.first;result.target_y=ball.second;
    const int distance=42-std::abs(side*42-start.first);result.goal_distance=distance;
    const int own_blocked=blockers(scene,goal,side,true,rand,result);
    const int other_blocked=blockers(scene,goal,1-side,true,rand,result);
    const int blocked=own_blocked+other_blocked;
    result.goal_blockers=blocked;
    result.opponent_nearby=uint8_t(reality_nearby(scene,start.first,start.second,1-side,3,true));
    result.teamplay=source_char(value(state,scene,actor,0x20,30,team,start.first,start.second,rating));
    result.shooting=source_char(value(state,scene,actor,0x80,30,team,start.first,start.second,rating));
    result.passing=source_char(value(state,scene,actor,0x100,15,team,start.first,start.second,rating));
    result.opponent_at_actor=scene.grid.occupant(start.first,start.second,1-side);
    int best=source_short(-2*(distance/(tactics[2]+1))+60+rand.range(10,20));
    const auto& player=state.players[size_t(scene.players[size_t(actor)].player)];
    result.speed_ability=source_char(int((player.w35c>>14)&127)+int((player.w368>>3)&3)*10);
    best+=25-int((player.w368>>4)&3)*3-blocked*10;
    if (!result.opponent_nearby && rand.range(7,9)<distance) best=0;
    const int center=std::abs(start.second-12);best-=center*3;
    // Preserve left-to-right short-circuit order, even when the draw's other
    // spatial predicates will subsequently fail.
    if ((distance<4 && center<6) || (distance<rand.range(5,7) && !blocked && center<8) ||
        distance>36 || actor%11==0) best=100;
    if (result.opponent_at_actor) best-=5;
    if (!distance && start.second!=12) best=0;
    if (distance>int(tactics[2])*3+10) best=0;
    result.airborne=uint8_t(rand.range(0,1)!=0);
    if (result.shooting+3<rand.range(0,int(goal.count)*2+10)) {
        result.scatter_x=rand.range(0,4)-2;result.scatter_y=rand.range(0,4)-2;
    }
    if ((blocked || (history.airborne && rand.range(0,1)!=0)) &&
        (result.opponent_at_actor || blocked>2 || rand.range(0,int(result.airborne)+1)==0) &&
        (!result.opponent_at_actor || rand.range(0,3)==0)) {
        result.blocked_attempt=true;
        result.scatter_x=rand.range(0,4)-2;result.scatter_y=rand.range(0,4)-2;
    }
    if (source_short(best)>0) {
        const auto target=goal.points[goal.count>20 ? 20 : goal.count-1];
        result.target_x=uint8_t(target.first+result.scatter_x);result.target_y=uint8_t(target.second+result.scatter_y);
    }
    if (result.opponent_at_actor && result.shooting+20<rand.range(0,(distance<7 ? 50 : 0)+50)) {
        result.intercepted=true;result.target_x=uint8_t(rand.range(0,8)+ball.first-4);
        result.target_y=uint8_t(rand.range(0,8)+ball.second-4);
    }
    result.inaccurate=result.scatter_x || result.scatter_y;
    int support_bias=source_char(center+result.teamplay-10);
    if (distance<rand.range(4,8)) support_bias=0;
    for (int id=side*11;id<side*11+11;++id) {
        if (id==actor || !scene.motion.figures[size_t(id)].active) continue;
        const auto target=scene.grid.position(id);const int target_distance=42-std::abs(side*42-target.first);
        int score=support_bias+rand.range(3,16-result.passing/2)+(tactics[3]+1)*(distance-target_distance);
        if (target_distance-5>distance) continue;
        if (distance>12) score=source_short(score)+center/2;
        if (distance<9) score-=center;
        const int low=result.passing/2+3,high=result.passing/2+15;
        const int separation=std::abs(distance-target_distance);
        if (separation>rand.range(low,high) || std::abs(start.second-target.second)>rand.range(low,high)) continue;
        const bool offside=reality_offside(scene,target.first);
        if (offside && rand.range(0,3)!=0) continue;
        const auto line=reality_path(start.first,start.second,target.first,target.second);
        const int obstructions=blockers(scene,line,1-side,true,rand,result);
        const int nearby=reality_nearby(scene,target.first,target.second,1-side,2,true);
        score+=33-obstructions*2-nearby*15;
        if (target_distance<8 && target_distance-3<distance) {
            const auto onward=reality_path(target.first,target.second,goal_target.first,goal_target.second);
            score+=12-4*blockers(scene,onward,1-side,false,rand,result);
        }
        if (source_short(best)>=source_short(score)) continue;
        result.receiver=id;result.receiver_player=scene.players[size_t(id)].player;
        const int loft_bias=source_char((3-int(tactics[4]))*2);
        result.airborne=uint8_t(rand.range(separation>loft_bias+6,separation>loft_bias+3));
        const int accuracy_draw=rand.range(0,2);
        if (((((result.airborne!=0 || accuracy_draw==0) && (result.airborne==0 || accuracy_draw!=0)) || !obstructions)) &&
            rand.range(0,40)<result.passing) {
            result.scatter_x=0;result.scatter_y=0;result.inaccurate=false;
        }
        result.receiver_offside=offside;result.action=1;result.leading_run=false;
        int target_x=target.first;const int direction=side ? -1 : 1;
        if (target_distance>6 && result.passing+rand.range(0,1)*8>5) {
            for (int step=0;step<2;++step) {
                result.receiver_nearby=reality_nearby(scene,target.first+step,target.second,1-side,2,true);
                if (result.receiver_nearby<=nearby+1 &&
                    (!step || (target_x+direction>=0 && target_x+direction<=42 &&
                               !scene.grid.occupant(target_x+direction,target.second,1-side)))) {
                    target_x+=direction;result.leading_run=true;score+=10;
                }
            }
        }
        result.target_x=uint8_t(target_x+result.scatter_x);result.target_y=uint8_t(target.second+result.scatter_y);best=score;
    }
    int dribble;
    if (result.opponent_nearby==1 && blocked<=1) dribble=35;
    else dribble=40-int(result.opponent_nearby)*(result.opponent_nearby<2 ? 25 : 20);
    dribble+=center+distance-blocked*5+22;
    if (distance<6) dribble-=10;
    if (result.opponent_at_actor) {
        bool behind=false;
        for (int id=side*11+1;id<side*11+11;++id) {
            const int x=scene.grid.position(id).first;
            if (scene.motion.figures[size_t(id)].active && (side ? start.first<x : x<start.first)) behind=true;
        }
        if (!behind) dribble=5;
        if (result.opponent_nearby>2) dribble-=rand.range(0,8)+15;
        const int own=uint8_t(value(state,scene,actor,0x20e,100,team,start.first,start.second,rating));
        const int opponent=uint8_t(source_char(value(state,scene,result.opponent_at_actor-1,0x20e,100,
            scene.teams[size_t(1-side)],start.first,start.second,rating))+scene.tactics[size_t(1-side)][6]*10);
        result.actor_duel=uint8_t(own);result.opponent_duel=uint8_t(opponent);
        dribble+=(own-opponent)/2;
    }
    if (actor%11==0 && (distance<34 || result.opponent_nearby)) dribble=0;
    result.score=source_short(best);result.dribble_score=source_short(dribble);
    if (result.dribble_score>result.score) result.boundary=RealityBallBoundary::dribble_or_duel;
    return result;
}

namespace {
RealityBallOutcome apply_ball_tail(const GameState& state,RealitySetup& scene,const RealityBallDecision& decision,
    RealityBallHistory& history,const RatingContext& rating,uint8_t referee,WatcomRandom& rand,GameRandom& game,
    const RealityDribbleDecision* dribble) {
    if (!dribble && decision.boundary!=RealityBallBoundary::direct_move)
        throw FormatError("Reality ball: dribble/duel continuation is not implemented");
    validate(state,scene,decision.actor);
    if (decision.side!=scene.attacking_side || decision.action<0 || decision.action>(dribble ? 2 : 1))
        throw FormatError("Reality ball: invalid direct decision");
    const int actor=decision.actor,side=decision.side;
    const auto start=dribble ? std::pair{decision.original_x,decision.original_y} : scene.grid.position(actor);
    const auto old_ball=scene.grid.ball();
    auto target=std::pair{decision.target_x,decision.target_y};RealityBallOutcome outcome;outcome.action=decision.action;
    auto actor_target=dribble ? std::pair{dribble->actor_target_x,dribble->actor_target_y} : start;
    if (dribble) {
        outcome.scene_status=dribble->scene_status;outcome.restart=dribble->restart;
        outcome.event_stops_scene=dribble->event_stops_scene;outcome.lost_duel=dribble->lost_duel;
    }
    uint8_t airborne=decision.airborne;
    if (history.airborne) {
        const int control=source_short(value(state,scene,actor,6,100,scene.teams[size_t(side)],start.first,start.second,rating));
        if (control<rand.range(0,120)) { target=start;actor_target=start;outcome.action=3;airborne=0; }
        else outcome.received=true;
    }
    const auto path=reality_path(start.first,start.second,actor_target.first,actor_target.second);
    const auto point=path.points[std::min(size_t(dribble ? dribble->steps : 2),path.count-1)];
    if (outcome.action==2 && !outcome.lost_duel) target=point;
    if (!scene.grid.place(actor,point.first,point.second,side) && !scene.grid.place(actor,start.first,start.second,side)) {
        outcome.scene_status=3;outcome.needs_reporter=false;return outcome; // Source returns before reporter
    }
    const auto accepted=scene.grid.position(actor);scene.motion.figures[size_t(actor)].next_x=accepted.first;
    scene.motion.figures[size_t(actor)].next_y=accepted.second;
    history.previous_actor=outcome.action==1 ? actor : decision.previous_actor;history.current_actor=actor;
    if (outcome.action==0) outcome.shot_ready=true;
    if (target.first>42) {
        outcome.scene_status=1;target.first=std::clamp(source_char(target.first),0,42);
        target.second=game.range(0,1) ? 14 : 10;
        if (decision.blocked_attempt) outcome.restart=3;
    }
    if (target.second>24) { outcome.scene_status=2;target.second=std::clamp(source_char(target.second),0,24); }
    if (outcome.action==0 && decision.goal_distance<37) {
        outcome.shooter_row=actor;outcome.assistant_row=history.previous_actor;
        outcome.shooter=scene.players[size_t(actor)].player;
        if (history.previous_actor>=0 && history.previous_actor<22 && history.previous_actor/11==side)
            outcome.assistant=scene.players[size_t(history.previous_actor)].player;
    }
    if (decision.receiver_offside && rand.range(0,(referee&3)*2+2)!=0) outcome.scene_status=3;
    if (!scene.grid.place(22,target.first,target.second,2)) throw FormatError("Reality ball: invalid Source final ball");
    auto& shadow=scene.motion.figures[22];shadow={true,old_ball.first,old_ball.second,target.first,target.second,shadow.phase};
    auto& ball=scene.motion.figures[23];ball.active=true;ball.old_x=old_ball.first;ball.old_y=old_ball.second;
    ball.next_x=target.first;ball.next_y=target.second;scene.motion.ball_airborne=airborne!=0;history.airborne=airborne;
    return outcome; // explicit InitializeRealityReporter / reporter-RNG boundary
}
} // namespace

RealityBallOutcome apply_reality_ball(const GameState& state,RealitySetup& scene,const RealityBallDecision& decision,
    RealityBallHistory& history,const RatingContext& rating,uint8_t referee,WatcomRandom& rand,GameRandom& game) {
    return apply_ball_tail(state,scene,decision,history,rating,referee,rand,game,nullptr);
}

RealityBallOutcome apply_reality_dribble(const GameState& state,RealitySetup& scene,const RealityDribbleDecision& decision,
    RealityBallHistory& history,const RatingContext& rating,uint8_t referee,WatcomRandom& rand,GameRandom& game) {
    if (decision.ball.boundary!=RealityBallBoundary::dribble_or_duel || decision.ball.action!=2 ||
        decision.steps<1 || decision.steps>2)
        throw FormatError("Reality dribble: invalid prepared decision");
    return apply_ball_tail(state,scene,decision.ball,history,rating,referee,rand,game,&decision);
}

} // namespace bmh
