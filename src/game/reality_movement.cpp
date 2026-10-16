// SPDX-License-Identifier: GPL-3.0-only
#include "game/reality_movement.hpp"

#include <algorithm>
#include <cstdlib>

namespace bmh {
namespace {
int source_short(int value) { return int(int16_t(value)); }
void validate(const GameState& state,const RealitySetup& scene,int figure,const RealityMoveContext& context) {
    if (scene.attacking_side<0 || scene.attacking_side>1 || figure<0 || figure>=22 ||
        !scene.motion.figures[size_t(figure)].active || context.goal_path.count<1 ||
        context.goal_path.count>context.goal_path.points.size())
        throw FormatError("Reality movement: invalid scene/player/path");
    const int player=scene.players[size_t(figure)].player;
    if (player<0 || size_t(player)>=state.players.size())
        throw FormatError("Reality movement: missing Source player attributes");
}
int speed(const GameState& state,const RealitySetup& scene,int figure,WatcomRandom& rand) {
    const auto& player=state.players[size_t(scene.players[size_t(figure)].player)];
    const int ability=int((player.w35c>>14)&127)+int((player.w368>>3)&3)*10;
    return ability<rand.range(0,130) ? 1 : 2;
}
std::pair<int,int> space(RealitySetup& scene,const LeImage& exe,int figure,int side,uint8_t flags,uint8_t mode,
                         const RealityMoveContext& context) {
    const auto target=reality_seek_space(scene,exe,figure,side,flags,mode,context.goal_path);
    if (target.found) scene.grid.stage_position(figure,target.x,target.y);
    return scene.grid.position(figure);
}
void finish_move(RealitySetup& scene,int figure,int side,std::pair<int,int> start,std::pair<int,int> target,
                 int padding,RealityMoveResult& result,GameRandom& game) {
    const auto path=reality_path(start.first,start.second,target.first,target.second);
    const auto candidate=path.points[std::min(size_t(result.steps),path.count-1)];
    // Source updates afb0c/afb23 BEFORE the movement-box/occupancy checks.
    // A failed commit returns home from this candidate, not from the old cell.
    scene.grid.stage_position(figure,candidate.first,candidate.second);
    const auto& box=scene.players[size_t(figure)];
    const bool within=candidate.first>=box.min_x-padding && candidate.first<=box.max_x+padding &&
                      candidate.second>=box.min_y-padding && candidate.second<=box.max_y+padding;
    if ((result.score>=100 || within) && scene.grid.place(figure,candidate.first,candidate.second,side)) {
        auto& motion=scene.motion.figures[size_t(figure)];motion.next_x=candidate.first;motion.next_y=candidate.second;
    } else {
        result.returned_to_box=true;
        reality_return_to_box(scene,figure,side,uint8_t(result.steps),game);
    }
}
} // namespace

RealityMoveContext reality_move_context(const RealitySetup& scene,const LeImage& exe) {
    if (scene.attacking_side<0 || scene.attacking_side>1) throw FormatError("Reality movement: invalid attacking side");
    const int side=scene.attacking_side;const auto [x,y]=scene.grid.ball();RealityMoveContext context;
    context.goal_path=reality_goal_path(x,y,side ? 0 : 42);
    context.attacking_nearby4=uint16_t(reality_nearby(scene,x,y,side,4,false));
    context.attacking_area=uint8_t(reality_penalty_area(scene,exe,side));
    context.attacking_nearby2=uint8_t(reality_nearby(scene,x,y,side,2,false));
    context.defending_nearby3=uint16_t(reality_nearby(scene,x,y,1-side,3,false));
    context.defending_nearby2_forward=uint8_t(reality_nearby(scene,x,y,1-side,2,true));
    return context;
}

void reality_defensive_push(const RealitySetup& scene,RealityMoveContext& context,WatcomRandom& rand) {
    if (scene.attacking_side<0 || scene.attacking_side>1) throw FormatError("Reality movement: invalid attacking side");
    const auto count=context.goal_path.count;
    const bool pushed=count<20 && count>6 && rand.range(0,8-int(context.defensive_push))==0 &&
                      scene.tactics[size_t(1-scene.attacking_side)][5]!=0;
    context.defensive_push=pushed ? 10 : 0;
}

RealityMoveResult reality_move_attacker(const GameState& state,RealitySetup& scene,const LeImage& exe,int figure,
    RealityMoveContext& context,WatcomRandom& rand,GameRandom& game) {
    validate(state,scene,figure,context);const int side=figure/11;
    if (side!=scene.attacking_side) throw FormatError("Reality movement: attacker on defending side");
    const auto start=scene.grid.position(figure),ball=scene.grid.ball();RealityMoveResult result;std::pair<int,int> target;
    if (figure%11==0) {
        target={side*40+1,12};
        if (std::abs(ball.first-start.first)<4 && std::abs(ball.second-start.second)<4) target=ball;
    } else {
        const int depth=42-std::abs(side*42-start.first);
        const int distance=std::max(std::abs(ball.first-start.first),std::abs(ball.second-start.second));
        const bool offside=reality_offside(scene,start.first);
        result.steps=speed(state,scene,figure,rand);
        result.score=!scene.grid.occupant(ball.first,ball.second,side) &&
                     (distance<3 || (distance<15 && !context.attacking_nearby2)) ?
                     100 : source_short(9-int(context.attacking_nearby4)*9);
        target=ball;
        if (!offside) {
            int next_score=depth-int(context.attacking_area)*10+50;
            if (depth<6) next_score=0;
            if (source_short(result.score)<source_short(next_score)) {
                if (depth<9 && context.attacking_area<3) target=context.goal_path.points[context.goal_path.count-1];
                else {
                    target=space(scene,exe,figure,side,0x19,context.attacking_area,context);
                    if (target==start) { target=ball;next_score=100; }
                }
                result.score=next_score;
            }
            if (source_short(result.score)<100) {
                int advanced=0;
                for (int id=side*11;id<side*11+11;++id) {
                    const auto p=scene.grid.position(id);
                    if (scene.motion.figures[size_t(id)].active && p.first>8 && p.second<16) ++advanced;
                }
                next_score=int(context.attacking_nearby4)*4+(scene.tactics[size_t(side)][1]+4)*advanced;
                if (context.goal_path.count>8 && source_short(result.score)<source_short(next_score)) {
                    target=space(scene,exe,figure,side,10,context.attacking_area,context);result.score=next_score;
                }
            }
        } else { target={20,start.second};result.score=200; }
    }
    result.score=source_short(result.score);finish_move(scene,figure,side,start,target,0,result,game);
    const auto end=scene.grid.position(figure);const int depth=42-std::abs(side*42-end.first);
    // InitializeRealityReporter(figure,side,17) is an explicit caller-owned reporter boundary.
    if (game.range(0,3)==0 && depth<15 && !reality_nearby(scene,end.first,end.second,1-side,3,false) &&
        std::abs(end.first-ball.first)>2 && std::abs(end.second-ball.second)>2) result.reporter_code=17;
    return result;
}

RealityMoveResult reality_move_defender(const GameState& state,RealitySetup& scene,const LeImage& exe,int figure,
    RealityMoveContext& context,WatcomRandom& rand,GameRandom& game) {
    validate(state,scene,figure,context);const int side=figure/11;
    if (side==scene.attacking_side) throw FormatError("Reality movement: defender on attacking side");
    const auto start=scene.grid.position(figure),ball=scene.grid.ball();RealityMoveResult result;
    if (context.blocked_figure && context.blocked_figure-1==figure) {
        if (!scene.grid.place(figure,start.first,start.second,side)) throw FormatError("Reality movement: uncleared blocked figure");
        return result;
    }
    int distance=std::max(std::abs(ball.first-start.first),std::abs(ball.second-start.second)),padding=0;
    std::pair<int,int> target=ball;
    if (figure%11==0) {
        const auto nearby=reality_nearby(scene,ball.first,ball.second,side,3,true);
        if (!(distance<3 || (distance<rand.range(10,13) && !nearby &&
            reality_line_occupants(scene,context.goal_path,side,false,rand).count<2)))
            target=context.goal_path.points[context.goal_path.count-1];
        result.score=100;
    } else {
        const uint8_t pressure=scene.tactics[size_t(side)][6];const int depth=std::abs(side*42-start.first);
        if (!pressure || depth<18) result.steps=speed(state,scene,figure,rand);
        const bool occupied=scene.grid.occupant(ball.first,ball.second,side)!=0;
        if ((!pressure && depth>17) || occupied ||
            (distance>2 && (context.defending_nearby2_forward || distance>3) &&
             (distance>8 || context.defending_nearby3)))
            result.score=source_short(18-(9-int(pressure)*6)*int(context.defending_nearby3));
        else result.score=200;
        if (result.score!=200) {
            result.score=source_short(result.score+6-(2-int(pressure))*distance);
            int next_score=depth+20;
            if (depth<std::min(size_t(9),context.goal_path.count)) next_score=-100;
            if (result.score<source_short(next_score)) {
                const auto found=space(scene,exe,figure,side,0x20,1,context);
                target=reality_on_path(context.goal_path,found.first,found.second) ? found : context.goal_path.points[context.goal_path.count-1];
                result.score=source_short(next_score);
            }
            const auto support=reality_support(scene,start.first,start.second,1-side,int16_t(result.score),pressure);
            if (support.found) { target={support.x,support.y};result.score=support.score; }
            if (result.score<200) if (const auto marker=reality_marked_position(state,scene,figure)) { target=*marker;padding=5; }
        }
        if (result.score!=200 && context.defensive_push && std::abs(side*42-target.first)<std::abs(side*42-ball.first)) target.first=ball.first;
    }
    finish_move(scene,figure,side,start,target,padding,result,game);
    if (figure%11==0)
        context.keeper_player=(start.first<2 || start.first>40) && start.second>9 && start.second<15 ?
            scene.players[size_t(figure)].player : 0xfff;
    return result;
}

} // namespace bmh
