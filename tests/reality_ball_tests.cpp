// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
#include <stdexcept>

#include "game/reality_ball.hpp"

using namespace bmh;
namespace {
void check(bool ok,const char* text) { if (!ok) throw std::runtime_error(text); }
GameState state_fixture() {
    GameState state;state.players.resize(22);state.player_bonus.resize(22);state.team_morale={24,24};
    for (auto& player:state.players) { player.w35c=48u|(100u<<7)|(100u<<14);player.w368=3u|(3u<<4)|(3u<<6); }
    return state;
}
void place(RealitySetup& scene,int figure,int x,int y) {
    check(scene.grid.place(figure,x,y,figure/11),"fixture placement");
    scene.motion.figures[size_t(figure)]={true,x,y,x,y,0};scene.players[size_t(figure)].player=figure;
}
void clear(RealitySetup& scene,int figure) { const auto [x,y]=scene.grid.position(figure);scene.grid.clear(x,y,figure/11); }
RealitySetup scene_fixture(int side) {
    RealitySetup scene;scene.teams={0,1};scene.attacking_side=side;scene.tactics[0][2]=scene.tactics[1][2]=3;
    scene.tactics[0][3]=scene.tactics[1][3]=3;return scene;
}
}
int main(int argc,char** argv) {
    try {
        if (argc!=2) throw std::runtime_error("usage: reality_ball_tests <Original data>");
        const auto exe=load_le(std::filesystem::path(argv[1])/"BMH.exe");const auto state=state_fixture();
        RatingContext rating;rating.stale_condition=100;rating.stale_technique=100;
        for (int side:{0,1}) {
            auto scene=scene_fixture(side);const int actor=side*11+1;
            place(scene,actor,side ? 7 : 35,12);place(scene,side*11+2,side ? 32 : 10,12);
            scene.grid.place(22,side ? 7 : 35,12,2);clear(scene,actor);
            RealityBallHistory history;history.current_actor=side*11+2;WatcomRandom rand(12345);GameRandom game(1);
            const auto decision=decide_reality_ball(state,scene,actor,history,rating,rand);
            check(decision.boundary==RealityBallBoundary::direct_move && decision.action==0 && rand.calls()==7,
                  "unopposed shot retains six prefix draws plus the skipped active teammate's score draw on either side");
            check(decision.shooting==30 && decision.passing==15 && decision.teamplay==30,"actual Source skill masks/scales use existing player_value");
            const auto outcome=apply_reality_ball(state,scene,decision,history,rating,0,rand,game);
            check(outcome.shot_ready && outcome.shooter==actor && outcome.assistant==side*11+2,
                  "direct shot returns actual shooter/assistant identities at outer chance boundary");
            check(scene.grid.ball()==std::pair{side ? 0 : 42,12} && rand.calls()==7 && game.calls()==0,
                  "direct unopposed shot commits Source goal point without additional randomness");
            const auto ws=rand.seed();const auto gs=game.seed();const auto frames=reality_motion(scene.motion,exe);(void)frames;
            check(ws==rand.seed() && gs==game.seed(),"owned ball trajectory render has no simulation RNG access");
        }
        bool found=false;
        for (int seed=1;seed<=32 && !found;++seed) {
            auto scene=scene_fixture(0);place(scene,1,20,12);place(scene,2,35,12);
            place(scene,11,42,12);place(scene,12,41,14);
            scene.grid.place(22,20,12,2);clear(scene,1);
            RealityBallHistory history;WatcomRandom rand{uint32_t(seed)};
            const auto decision=decide_reality_ball(state,scene,1,history,rating,rand);
            if (decision.boundary!=RealityBallBoundary::direct_move || decision.action!=1) continue;
            found=true;check(decision.receiver==2 && decision.receiver_player==2 && decision.airborne==1 && decision.leading_run,
                             "Source selects actual receiver, one-call constant loft range and leading pass");
            GameRandom game(1);const auto calls=rand.calls();const auto outcome=apply_reality_ball(state,scene,decision,history,rating,0,rand,game);
            check(outcome.action==1 && history.previous_actor==1 && scene.motion.ball_airborne && !outcome.shot_ready,
                  "pass commits actor history and airborne ball without fabricating a shot");
            check(scene.grid.previous_ball()==std::pair{20,12} && scene.grid.ball()==std::pair{decision.target_x,decision.target_y},
                  "pass shadow/ball keeps previous point and uses actual Source target");
            check(rand.calls()==calls && !game.calls(),"normal pass tail adds no random draws without reception/boundaries/offside");
        }
        check(found,"ordinary legal Source fixture produces a direct pass");
        auto scene=scene_fixture(0);place(scene,1,20,12);scene.grid.place(22,20,12,2);clear(scene,1);
        RealityBallDecision direct;direct.actor=1;direct.side=0;direct.action=1;direct.target_x=255;direct.target_y=30;direct.blocked_attempt=true;
        RealityBallHistory history;WatcomRandom rand(12345);GameRandom game(1);
        auto outcome=apply_reality_ball(state,scene,direct,history,rating,0,rand,game);
        check(outcome.scene_status==1 && outcome.restart==3 && scene.grid.ball()==std::pair{0,10} && game.calls()==1,
              "negative byte x triggers horizontal boundary first, clamps signed coordinate and replaces y by Game goal-mouth draw");
        scene=scene_fixture(0);place(scene,1,20,12);scene.grid.place(22,20,12,2);clear(scene,1);
        direct.target_x=20;direct.target_y=255;direct.blocked_attempt=false;history={};rand=WatcomRandom(12345);game=GameRandom(1);
        outcome=apply_reality_ball(state,scene,direct,history,rating,0,rand,game);
        check(outcome.scene_status==2 && scene.grid.ball()==std::pair{20,0} && !game.calls(),"vertical boundary clamps signed byte without a Game draw");
        scene=scene_fixture(0);place(scene,1,20,12);scene.grid.place(22,20,12,2);clear(scene,1);
        direct.target_x=30;direct.target_y=12;direct.receiver_offside=true;history={};rand=WatcomRandom(12345);game=GameRandom(1);
        outcome=apply_reality_ball(state,scene,direct,history,rating,0,rand,game);
        check(outcome.scene_status==3 && rand.calls()==1 && !game.calls() && scene.grid.ball()==std::pair{30,12},
              "offside recipient uses referee low bits with one Watcom draw before committing the ball/stopping scene");
        direct.receiver_offside=false;
        scene=scene_fixture(0);place(scene,1,20,12);scene.grid.place(22,20,12,2);clear(scene,1);history={};history.airborne=1;
        auto weak=state;weak.players[1].w35c=48;direct.target_x=30;direct.target_y=12;rand=WatcomRandom(12345);game=GameRandom(1);
        outcome=apply_reality_ball(weak,scene,direct,history,rating,0,rand,game);
        check(outcome.action==3 && !outcome.received && scene.grid.ball()==std::pair{20,12} && !history.airborne && rand.calls()==1,
              "failed airborne reception uses rating6 vs Watcom120 and leaves ball at actor, action3");
        scene=scene_fixture(0);place(scene,1,20,12);scene.grid.place(22,20,12,2);clear(scene,1);
        history={};direct.boundary=RealityBallBoundary::dribble_or_duel;rand=WatcomRandom(12345);game=GameRandom(1);bool rejected=false;
        try { apply_reality_ball(state,scene,direct,history,rating,0,rand,game); } catch(const FormatError&) { rejected=true; }
        check(rejected && !rand.calls() && !game.calls() && scene.grid.ball()==std::pair{20,12} && !scene.grid.occupant(20,12,0),
              "unimplemented dribble/duel cannot silently commit a replacement move");
        std::puts("PASS: Source direct ball decisions, both sides, real ratings, shot/pass, airborne reception, boundaries and explicit dribble limit");return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
