// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <cstdio>
#include <stdexcept>

#include "game/reality_space.hpp"

using namespace bmh;
namespace {
void check(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
void player(RealitySetup& scene,int id,int x,int y) {
    check(scene.grid.place(id,x,y,id/11),"fixture player placement");
    scene.motion.figures[size_t(id)]={true,x,y,x,y,0};
}
}
int main(int argc,char** argv) {
    try {
        if (argc!=2) throw std::runtime_error("usage: reality_space_tests <Original data>");
        const auto exe=load_le(std::filesystem::path(argv[1])/"BMH.exe");
        RealitySetup scene;player(scene,0,18,12);player(scene,1,20,12);player(scene,2,22,12);
        check(reality_nearby(scene,20,12,0,2,false)==3,"symmetric nearby box includes its boundary");
        check(reality_nearby(scene,20,12,0,2,true)==2,"Source side0 directional count uses nonpositive x offsets");
        player(scene,11,18,12);player(scene,12,20,12);player(scene,13,22,12);
        check(reality_nearby(scene,20,12,1,2,true)==2,"Source side1 directional count uses nonnegative x offsets");
        check(reality_nearby(scene,0,0,0,0,false)==8,"inactive coordinate slots are counted by the literal Source scan");
        scene.grid.place(3,20,13,0);
        check(reality_nearby(scene,20,12,0,2,false)==4,"inactive stored coordinate also contributes to Source count");

        scene={};player(scene,11,35,10);player(scene,12,32,14);scene.grid.place(22,25,12,2);
        check(!reality_offside(scene,31) && reality_offside(scene,32),"offside requires two defenders strictly beyond the target");
        check(!reality_offside(scene,19) && !reality_offside(scene,24),"Source midfield and behind-ball exceptions");
        scene={};scene.attacking_side=1;player(scene,0,7,10);player(scene,1,10,14);scene.grid.place(22,17,12,2);
        check(!reality_offside(scene,11) && reality_offside(scene,10),"mirrored offside uses strict defender x comparison");
        check(!reality_offside(scene,21) && !reality_offside(scene,18),"mirrored midpoint and ball exceptions");

        scene={};player(scene,0,5,12);player(scene,1,7,12);player(scene,2,9,12);
        const auto path=reality_path(5,12,9,12);WatcomRandom rand(12345);
        const auto unselected=reality_line_occupants(scene,path,0,false,rand);
        check(unselected.count==2 && !unselected.selected && rand.calls()==0,"obstruction count excludes terminal recipient and needs no draw");
        const auto selected=reality_line_occupants(scene,path,0,true,rand);
        check(selected.count==2 && rand.calls()==1,"first obstruction is selected without RNG; each later occupant uses one Watcom draw");
        check(selected.selected==1 || selected.selected==2,"selected obstruction keeps Source one-based occupancy identity");
        const auto zero=reality_line_occupants(scene,reality_path(5,12,5,12),0,true,rand);
        check(zero.count==0 && zero.selected==0 && rand.calls()==1,"single-cell line excludes its only terminal cell");
        check(reality_on_path(path,9,12) && !reality_on_path(path,9,13),"path membership includes terminal recipient");

        scene={};player(scene,0,20,12);const auto goal=reality_goal_path(20,12,42);
        auto target=reality_seek_space(scene,exe,0,0,0,0,goal);
        check(target.found && target.x==22 && target.y==12 && target.score==47,"Source base scoring prefers depth and same row");
        scene.attacking_side=1;target=reality_seek_space(scene,exe,0,0,0,0,goal);
        check(target.x==18 && target.y==12 && target.score==51,"space scoring uses current attacking side, independently of player side");
        scene.attacking_side=0;target=reality_seek_space(scene,exe,0,0,0x10,0,goal);
        check(target.x==22 && target.y==12 && target.score==83,"forward scan and Source goal-path bonus");
        target=reality_seek_space(scene,exe,0,0,0x10,4,goal);
        check(target.score==73,"mode4 suppresses the local goal-path/area bonus");
        target=reality_seek_space(scene,exe,0,0,8,0,goal);
        check(target.x==19 && target.y==12,"offside filter removes advanced cells without sufficient defenders");
        scene.tactics[0][1]=2;target=reality_seek_space(scene,exe,0,0,2,0,goal);
        check(target.x==22 && target.y==10 && target.score==83,"wing preference preserves x/y scan order on ties");
        player(scene,11,22,12);target=reality_seek_space(scene,exe,0,0,0x20,0,goal);
        check(target.x==22 && target.y==12 && target.score==83,"bit20 allows an opponent cell and favors the global path");
        target=reality_seek_space(scene,exe,0,0,1,0,goal);
        check(!(target.x==22 && target.y==12),"ordinary scored movement excludes occupied opponent cell");

        scene={};player(scene,0,20,12);player(scene,1,24,12);scene.grid.place(22,20,12,2);
        target=reality_support(scene,20,12,0,-100,0);
        check(target.found && target.figure==0 && target.score==112,"unopposed ball cell gets Source32 support bonus");
        check(!reality_support(scene,20,12,0,112,0).found,"support must strictly improve the initial score");
        player(scene,11,20,12);target=reality_support(scene,20,12,0,-100,0);
        check(target.figure==1 && target.score==80,"support excludes contested cells while retaining scan boundary");
        scene={};player(scene,0,30,12);target=reality_support(scene,28,12,0,-100,1);
        check(target.score==156,"pressure/deep-cell Source scoring preserves the greater-than25/38 thresholds");

        const int min_x=exe.u8(::bmh::layout::RealitySpace_RealityPenaltyArea_RuntimeField),max_x=exe.u8(::bmh::layout::RealitySpace_RealityPenaltyArea_RuntimeField2),min_y=exe.u8(::bmh::layout::RealitySpace_RealityPenaltyArea_RuntimeField3),max_y=exe.u8(::bmh::layout::RealitySpace_RealityPenaltyArea_RuntimeField4);
        scene={};player(scene,0,min_x,min_y);player(scene,11,min_x+30,min_y);
        check(reality_penalty_area(scene,exe,0)==1 && reality_penalty_area(scene,exe,1)==1,"Source area scans offset only the side1 x interval");
        check(reality_penalty_area(scene,exe,1,min_x,min_y)==1 && reality_penalty_area(scene,exe,1,max_x+1,max_y)==0,
              "point area membership uses literal unmirrored Source bounds");

        scene={};player(scene,0,10,12);scene.players[0].min_x=12;scene.players[0].max_x=16;
        scene.players[0].min_y=10;scene.players[0].max_y=14;scene.grid.clear(10,12,0);GameRandom game(1);
        reality_return_to_box(scene,0,0,2,game);
        check(scene.grid.position(0)==std::pair{12,12} && game.calls()==0,"return-to-box takes Source path index2 without randomness if free");
        check(scene.motion.figures[0].old_x==10 && scene.motion.figures[0].next_x==12,"return-to-box preserves old rendering position");
        scene={};player(scene,0,10,12);player(scene,1,12,12);scene.players[0]={0,0,0,0,0,12,16,10,14};
        scene.grid.clear(10,12,0);game=GameRandom(1);reality_return_to_box(scene,0,0,2,game);
        check(scene.grid.position(0)==std::pair{11,11} && game.calls()==2 && game.seed()==3095,
              "blocked return uses the real Game generator, x draw followed by y draw");
        scene={};player(scene,11,42,12);scene.players[11].min_x=39;scene.players[11].max_x=52;
        scene.players[11].min_y=11;scene.players[11].max_y=13;scene.grid.clear(42,12,1);game=GameRandom(1);
        reality_return_to_box(scene,11,1,2,game);
        check(scene.grid.position(11)==std::pair{42,11} && game.calls()==2,"Source box center outside pitch is not clipped before path selection");
        RealityPath malformed;malformed.count=52;bool rejected=false;
        try { reality_line_occupants(scene,malformed,1,true,rand); } catch(const FormatError&) { rejected=true; }
        check(rejected,"truncated Source path rejected without unsafe access");
        std::puts("PASS: Source Reality space, offside, nearby/stale slots, obstruction RNG, scored moves, support, area and collision return");return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
