// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/reality_space.hpp"

#include <algorithm>
#include <cstdlib>

namespace bmh {
namespace {
void side_ok(int side) {
    if (side < 0 || side > 1) throw FormatError("Reality space: invalid side");
}
void path_ok(const RealityPath& path) {
    if (path.count > path.points.size()) throw FormatError("Reality space: truncated path");
    for (size_t i = 0; i < path.count; ++i) {
        const auto [x,y] = path.points[i];
        if (x < 0 || x > 42 || y < 0 || y > 24)
            throw FormatError("Reality space: path outside playable grid");
    }
}
bool playable(int x,int y) { return x >= 0 && x <= 42 && y >= 0 && y <= 24; }

// RealityPath also receives movement-box centers outside the pitch. Unlike
// occupancy/projection, its signed-byte line does not clip those endpoints.
RealityPath box_path(int x,int y,int target_x,int target_y) {
    const int ax=std::abs(target_x-x),ay=std::abs(target_y-y),length=std::max(ax,ay);
    if (target_x < -128 || target_x > 127 || target_y < -128 || target_y > 127 || length > 127)
        throw FormatError("Reality space: unsupported signed-byte box center");
    RealityPath path;const int sx=length ? ax*100/length : 0,sy=length ? ay*100/length : 0;
    const int dx=target_x<x ? -1 : 1,dy=target_y<y ? -1 : 1;int ex=0,ey=0;
    while (x!=target_x || y!=target_y) {
        path.points[path.count++]={x,y};
        if (path.count>49) break;
        if (x!=target_x && (ex+=sx)>=100-sx) { ex-=100;x+=dx; }
        if (y!=target_y && (ey+=sy)>=100-sy) { ey-=100;y+=dy; }
    }
    path.points[path.count++]={x,y};return path;
}
} // namespace

bool reality_on_path(const RealityPath& path,int x,int y) {
    path_ok(path);
    return std::find(path.points.begin(),path.points.begin()+path.count,std::pair{x,y})!=path.points.begin()+path.count;
}

bool reality_offside(const RealitySetup& scene,int x) {
    const int side=scene.attacking_side;side_ok(side);
    const int ball_x=scene.grid.ball().first;
    if ((side==1 && (x>20 || x>ball_x)) || (side==0 && (x<20 || x<ball_x))) return false;
    int defenders=0;
    for (int id=(1-side)*11;id<(1-side)*11+11;++id) {
        if (!scene.motion.figures[size_t(id)].active) continue;
        const int defender_x=scene.grid.position(id).first;
        if ((side==0 && x<defender_x) || (side==1 && defender_x<x)) ++defenders;
        if (defenders==2) return false;
    }
    return true;
}

int reality_nearby(const RealitySetup& scene,int x,int y,int side,int radius,bool forward) {
    side_ok(side);
    if (radius<0 || radius>255) throw FormatError("Reality space: invalid neighborhood radius");
    const int left=forward && side==1 ? 0 : radius,right=forward && side==0 ? 0 : radius;
    int count=0;
    // RealityPenaltyArea deliberately counts all eleven coordinate slots, including
    // inactive/stale ones; neither occupancy nor active flags are consulted.
    for (int id=side*11;id<side*11+11;++id) {
        const auto [px,py]=scene.grid.position(id);
        if (std::abs(py-y)<=radius && px-x>=-left && px-x<=right) ++count;
    }
    return count;
}

int reality_penalty_area(const RealitySetup& scene,const LeImage& exe,int side,int x,int y) {
    side_ok(side);
    const int min_x=exe.u8(::bmh::layout::RealitySpace_RealityPenaltyArea_RuntimeField),max_x=exe.u8(::bmh::layout::RealitySpace_RealityPenaltyArea_RuntimeField2),min_y=exe.u8(::bmh::layout::RealitySpace_RealityPenaltyArea_RuntimeField3),max_y=exe.u8(::bmh::layout::RealitySpace_RealityPenaltyArea_RuntimeField4);
    if (x!=-1) return min_x<=x && x<=max_x && min_y<=y && y<=max_y;
    // Source only offsets the scan; the point-membership branch above uses
    // the unmirrored literal bounds even for side1.
    const int offset=side ? 30 : 0;
    if (!playable(min_x+offset,min_y) || !playable(max_x+offset,max_y))
        throw FormatError("Reality space: invalid Source penalty-area table");
    int count=0;
    for (int px=min_x+offset;px<=max_x+offset;++px)
        for (int py=min_y;py<=max_y;++py) if (scene.grid.occupant(px,py,side)) ++count;
    return count;
}

RealityLineOccupants reality_line_occupants(const RealitySetup& scene,const RealityPath& path,int side,bool select,WatcomRandom& rand) {
    side_ok(side);path_ok(path);RealityLineOccupants result;
    for (size_t i=0;i+1<path.count;++i) {
        const auto [x,y]=path.points[i];const auto occupant=scene.grid.occupant(x,y,side);
        if (!occupant) continue;
        if (select && (!result.selected || rand.range(0,1)!=0)) result.selected=occupant;
        ++result.count;
    }
    return result;
}

RealitySpaceTarget reality_seek_space(const RealitySetup& scene,const LeImage& exe,int figure,int side,
                                     uint8_t flags,uint8_t mode,const RealityPath& goal_path) {
    side_ok(side);side_ok(scene.attacking_side);path_ok(goal_path);
    if (figure<0 || figure>=22) throw FormatError("Reality space: invalid player");
    const auto [x,y]=scene.grid.position(figure);
    int left=2,right=3;
    RealityPath player_goal;
    if (flags&0x10) {
        if (!goal_path.count) throw FormatError("Reality space: missing Source goal path");
        const auto target=goal_path.points[goal_path.count-1];
        player_goal=reality_path(x,y,target.first,target.second);
        if (!(flags&8)) { if (side==0) left=0;else right=1; }
    }
    RealitySpaceTarget result;int best=-100;
    for (int px=x-left;px<x+right;++px) for (int py=y-2;py<y+3;++py) {
        if (!playable(px,py) || scene.grid.occupant(px,py,side)) continue;
        int score=std::abs(scene.attacking_side*42-px)*2+(py>=5 && py<=19);
        if (py==y) score+=2;
        if (flags) {
            if ((flags&8) && reality_offside(scene,px)) continue;
            if (!(flags&0x20)) {
                if (scene.grid.occupant(px,py,1-side)) continue;
                score+=10-3*reality_nearby(scene,px,py,1-side,2,false);
            }
            score+=20-4*reality_nearby(scene,px,py,side,3,false);
            if ((flags&0x10) && px!=x && mode<4)
                score+=10*reality_on_path(player_goal,px,py)+15*reality_penalty_area(scene,exe,1-side,px,py);
        }
        if (flags&2) score+=std::abs(12-py)*(scene.tactics[size_t(side)][1]+4);
        if (flags&0x20) score+=20*reality_on_path(goal_path,px,py);
        const int source_score=int(int16_t(score));
        if (source_score>best) { best=source_score;result={true,px,py,source_score,-1}; }
    }
    return result;
}

RealitySpaceTarget reality_support(const RealitySetup& scene,int x,int y,int side,int16_t minimum_score,uint8_t pressure) {
    side_ok(side);RealitySpaceTarget result;int best=minimum_score;
    for (int px=x-4;px<x+5;++px) for (int py=y-4;py<y+5;++py) {
        if (!playable(px,py)) continue;
        const int occupant=scene.grid.occupant(px,py,side);
        if (!occupant || scene.grid.occupant(px,py,1-side)) continue;
        int depth=std::abs(side*42-px);if (depth>25) depth+=10;
        int score=depth+40;
        if (!pressure) score+=depth;else if (depth>38) score=depth+120;
        score-=2*std::max(std::abs(x-px),std::abs(y-py));
        if (scene.grid.ball()==std::pair{px,py}) score+=32;
        if (score>best) { best=score;result={true,px,py,score,occupant-1}; }
    }
    return result;
}

void reality_return_to_box(RealitySetup& scene,int figure,int side,uint8_t steps,GameRandom& game) {
    side_ok(side);
    if (figure<0 || figure>=22) throw FormatError("Reality space: invalid player");
    const auto& box=scene.players[size_t(figure)];const auto old=scene.grid.position(figure);
    const int cx=int(int8_t((box.min_x+box.max_x)/2)),cy=int(int8_t((box.min_y+box.max_y)/2));
    const auto path=box_path(old.first,old.second,cx,cy);
    auto [x,y]=path.points[std::min(size_t(steps),path.count-1)];
    while (!scene.grid.place(figure,x,y,side)) {
        const int next_x=int(int8_t(x+game.range(0,2)-1));
        const int next_y=int(int8_t(y+game.range(0,2)-1));
        x=std::clamp(next_x,0,42);y=std::clamp(next_y,0,24);
    }
    auto& motion=scene.motion.figures[size_t(figure)];motion.next_x=x;motion.next_y=y;
}

} // namespace bmh
