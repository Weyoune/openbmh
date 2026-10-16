// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
#include <stdexcept>

#include "game/reality_movement.hpp"

using namespace bmh;
namespace {
void check(bool ok,const char* text) { if (!ok) throw std::runtime_error(text); }
GameState players() {
    GameState state;state.players.resize(22);
    for (auto& player:state.players) { player.w35c=127u<<14;player.w368=3u<<3; }
    return state;
}
void place(RealitySetup& scene,int figure,int x,int y) {
    check(scene.grid.place(figure,x,y,figure/11),"fixture occupancy");
    scene.motion.figures[size_t(figure)]={true,x,y,x,y,0};
    scene.players[size_t(figure)]={figure,figure,0,x,y,x-4,x+4,y-4,y+4};
}
void clear(RealitySetup& scene,int figure) { const auto [x,y]=scene.grid.position(figure);scene.grid.clear(x,y,figure/11); }
void put16(Bytes& b,size_t p,uint16_t v) { b.at(p)=uint8_t(v);b.at(p+1)=uint8_t(v>>8); }
}
int main(int argc,char** argv) {
    try {
        if (argc!=2) throw std::runtime_error("usage: reality_movement_tests <Original data>");
        const auto exe=load_le(std::filesystem::path(argv[1])/"BMH.exe");auto state=players();
        RealitySetup scene;place(scene,1,20,12);scene.grid.place(22,21,12,2);
        auto context=reality_move_context(scene,exe);check(context.attacking_nearby4==1 && context.attacking_nearby2==1,
            "iteration context uses Source ball-local counts before caller clears occupancy");
        clear(scene,1);WatcomRandom rand(12345);GameRandom game(1);
        auto move=reality_move_attacker(state,scene,exe,1,context,rand,game);
        check(scene.grid.position(1)==std::pair{21,12} && move.score==100 && !move.returned_to_box,
              "near unoccupied ball is chased by attacking noncarrier");
        check(rand.calls()==1 && game.calls()==1,"attacker speed draws Watcom; terminal commentary test always draws Game");

        scene={};place(scene,1,35,12);place(scene,11,42,12);scene.grid.place(22,30,12,2);
        context=reality_move_context(scene,exe);clear(scene,1);rand=WatcomRandom(12345);game=GameRandom(1);
        move=reality_move_attacker(state,scene,exe,1,context,rand,game);
        check(scene.grid.position(1)==std::pair{33,12} && move.score==200 && move.steps==2,"offside attacker returns toward literal x20");
        state.players[1].w35c=0;state.players[1].w368=0;
        scene={};place(scene,1,35,12);place(scene,11,42,12);scene.grid.place(22,30,12,2);
        context=reality_move_context(scene,exe);clear(scene,1);rand=WatcomRandom(12345);game=GameRandom(1);
        move=reality_move_attacker(state,scene,exe,1,context,rand,game);
        check(scene.grid.position(1)==std::pair{34,12} && move.steps==1,"real Source player speed attributes alter path index");state=players();

        scene={};place(scene,0,0,12);scene.grid.place(22,10,12,2);context=reality_move_context(scene,exe);
        clear(scene,0);rand=WatcomRandom(12345);game=GameRandom(1);move=reality_move_attacker(state,scene,exe,0,context,rand,game);
        check(scene.grid.position(0)==std::pair{1,12} && rand.calls()==0 && game.calls()==1,"attacking keeper takes home anchor1 without speed draw");
        scene={};place(scene,1,30,12);scene.grid.place(22,20,20,2);context=reality_move_context(scene,exe);
        clear(scene,1);rand=WatcomRandom(12345);game=GameRandom(1);move=reality_move_attacker(state,scene,exe,1,context,rand,game);
        check(move.reporter_code==17 && rand.calls()==1 && game.calls()==1,
              "commentary cue17 is returned at the explicit unimplemented reporter/RNG boundary");
        scene={};scene.attacking_side=1;place(scene,12,22,12);scene.grid.place(22,21,12,2);context=reality_move_context(scene,exe);
        clear(scene,12);rand=WatcomRandom(12345);game=GameRandom(1);move=reality_move_attacker(state,scene,exe,12,context,rand,game);
        check(scene.grid.position(12)==std::pair{21,12} && rand.calls()==1 && game.calls()==1,"side1 noncarrier uses the same Source pursuit and streams");

        scene={};place(scene,12,30,12);scene.grid.place(22,10,12,2);context=reality_move_context(scene,exe);
        scene.players[12].min_x=scene.players[12].max_x=34;scene.players[12].min_y=scene.players[12].max_y=12;
        clear(scene,12);rand=WatcomRandom(12345);game=GameRandom(1);move=reality_move_defender(state,scene,exe,12,context,rand,game);
        check(move.returned_to_box && scene.grid.position(12)==std::pair{34,12} && move.score==32,
              "failed box commit returns from the staged candidate32, not old cell30");
        check(rand.calls()==1 && game.calls()==0,"free defensive return consumes speed Watcom only");

        scene={};place(scene,12,30,12);scene.grid.place(22,29,12,2);context=reality_move_context(scene,exe);
        clear(scene,12);rand=WatcomRandom(12345);game=GameRandom(1);move=reality_move_defender(state,scene,exe,12,context,rand,game);
        check(scene.grid.position(12)==std::pair{29,12} && move.score==200 && !move.returned_to_box,
              "near defender chases the unoccupied ball without box limits");

        scene={};scene.teams={0,1};place(scene,1,20,12);place(scene,12,30,12);scene.grid.place(22,10,12,2);
        state.manager_teams={1};state.manager_records.resize(0xb0c);state.tactics.resize(0x840);
        for (int row=0;row<24;++row) { put16(state.tactics,size_t(row)*0x58+0x44,0xfff);put16(state.tactics,size_t(row)*0x58+0x46,0xfff); }
        put16(state.tactics,0x58+0x44,1);const auto board=state.tactics;
        context=reality_move_context(scene,exe);clear(scene,12);rand=WatcomRandom(12345);game=GameRandom(1);
        move=reality_move_defender(state,scene,exe,12,context,rand,game);
        check(scene.grid.position(12)==std::pair{28,12} && !move.returned_to_box,"human marking allows Source movement-box padding5");
        check(state.tactics==board,"movement reads human marking without Native writes");state=players();

        scene={};place(scene,12,30,12);scene.grid.place(22,10,12,2);context=reality_move_context(scene,exe);context.blocked_figure=13;
        clear(scene,12);rand=WatcomRandom(12345);game=GameRandom(1);move=reality_move_defender(state,scene,exe,12,context,rand,game);
        check(scene.grid.position(12)==std::pair{30,12} && rand.calls()==0 && game.calls()==0,"blocked defender stays put before all movement draws");
        scene={};place(scene,11,42,12);scene.grid.place(22,41,13,2);context=reality_move_context(scene,exe);
        clear(scene,11);rand=WatcomRandom(12345);game=GameRandom(1);move=reality_move_defender(state,scene,exe,11,context,rand,game);
        check(scene.grid.position(11)==std::pair{41,13} && context.keeper_player==11 && rand.calls()==0 && game.calls()==0,
              "close keeper chase short-circuits Watcom and preserves keeper identity from old goal-mouth cell");

        scene={};place(scene,12,20,12);scene.tactics[1][6]=1;scene.grid.place(22,10,12,2);context=reality_move_context(scene,exe);
        clear(scene,12);rand=WatcomRandom(12345);game=GameRandom(1);move=reality_move_defender(state,scene,exe,12,context,rand,game);
        check(rand.calls()==0,"pressing defender at depth>=18 bypasses speed RNG");

        scene={};scene.tactics[1][5]=0;context={};context.goal_path=reality_path(10,12,20,12);
        rand=WatcomRandom(12345);reality_defensive_push(scene,context,rand);
        check(rand.calls()==1 && !context.defensive_push,"first-defender branch draws before testing tactic5");
        context.defensive_push=10;scene.tactics[1][5]=1;rand=WatcomRandom(12345);reality_defensive_push(scene,context,rand);
        check(context.defensive_push==10 && rand.calls()==0,"previous push10 preserves Source inverted-range no-draw behavior");
        context.goal_path=reality_path(10,12,11,12);reality_defensive_push(scene,context,rand);
        check(!context.defensive_push && rand.calls()==0,"short path avoids defensive push draw");

        scene={};place(scene,1,20,12);scene.grid.stage_position(1,22,12);
        check(scene.grid.position(1)==std::pair{22,12} && scene.grid.occupant(20,12,0)==2 && !scene.grid.occupant(22,12,0),
              "staged coordinates do not commit occupancy or erase its old cell");
        std::puts("PASS: Source attacker/defender movement, real speed RNG, staged box recovery, marking, keepers and defensive push");return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
