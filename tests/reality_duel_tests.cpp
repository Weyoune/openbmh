// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
#include <algorithm>
#include <stdexcept>
#include "game/reality_duel.hpp"

using namespace bmh;
namespace {
void check(bool ok,const char* text) { if (!ok) throw std::runtime_error(text); }
GameState state_fixture() {
    GameState s;s.players.resize(22);s.player_bonus.resize(22);s.team_morale={24,24};s.squad_slots.resize(48,kEmptySlot);
    for (int i=0;i<22;++i) {
        s.players[size_t(i)].w35c=48|(100u<<7)|(100u<<14);s.players[size_t(i)].w368=3u|(3u<<4)|(3u<<6);
        s.squad_slots[size_t((i/11)*24+i%11)]=uint16_t(kSlotInLineup|i);
    }
    return s;
}
void place(RealitySetup& scene,int id,int x,int y) {
    check(scene.grid.place(id,x,y,id/11),"fixture placement");scene.motion.figures[size_t(id)]={true,x,y,x,y,0};
    scene.players[size_t(id)].player=id;scene.players[size_t(id)].squad_slot=(id/11)*24+id%11;
}
}
int main(int argc,char** argv) {
    try {
        if (argc!=2) throw std::runtime_error("usage: reality_duel_tests <Original data>");
        const auto exe=load_le(std::filesystem::path(argv[1])/"BMH.exe");auto state=state_fixture();
        RealitySetup scene;scene.teams={0,1};place(scene,1,20,12);scene.grid.place(22,20,12,2);scene.grid.clear(20,12,0);
        RealityBallHistory history;RatingContext rating;WatcomRandom rand(12345);GameRandom game(1);
        const auto ball=decide_reality_ball(state,scene,1,history,rating,rand);
        check(ball.boundary==RealityBallBoundary::dribble_or_duel,"Source prefers dribble without a viable pass or shot");
        const auto before=rand.calls();const auto dribble=continue_reality_dribble(state,scene,exe,ball,0,true,rand);
        check(rand.calls()==before+1 && dribble.steps==2 && !dribble.lost_duel,"free dribble only draws its speed once and never asks for forced event");
        const auto outcome=apply_reality_dribble(state,scene,dribble,history,rating,0,rand,game);
        check(outcome.action==2 && scene.grid.ball()==std::pair{22,12} && scene.grid.position(1)==std::pair{22,12},
              "Source free dribble moves actor and ground ball to the same path point");
        check(!scene.motion.ball_airborne && !game.calls(),"dribble clears loft and free commit adds no Game draw");
        scene={};scene.teams={0,1};place(scene,1,20,12);scene.grid.place(22,20,12,2);scene.grid.clear(20,12,0);
        auto wrapped=ball;wrapped.speed_ability=-99;rand=WatcomRandom(12345);
        const auto slow=continue_reality_dribble(state,scene,exe,wrapped,0,true,rand);
        check(slow.steps==1,"Source signed-char speed overflow chooses one step without normalization");
        scene={};scene.teams={0,1};place(scene,1,20,12);place(scene,12,20,12);
        scene.grid.place(22,20,12,2);scene.grid.clear(20,12,0);
        auto contested=ball;contested.opponent_at_actor=13;
        auto weak=state;weak.players[1].w35c=127u<<7;rand=WatcomRandom(1);game=GameRandom(1);history={};
        const auto lost=continue_reality_dribble(weak,scene,exe,contested,0,true,rand);
        const auto lost_outcome=apply_reality_dribble(weak,scene,lost,history,rating,0,rand,game);
        check(lost.lost_duel && lost.steps==1 && lost_outcome.lost_duel &&
              scene.grid.position(1)==std::pair{21,12} && scene.grid.ball()==std::pair{20,12} && rand.calls()==4,
              "lost Source duel moves actor one step while leaving ball with opponent, without a synthetic card");

        scene={};scene.teams={0,1};place(scene,1,36,12);place(scene,12,36,12);scene.grid.place(22,36,12,2);
        state.players[12].w35c=127u<<7; // negative balance: Source base foul chance0
        rand=WatcomRandom(12345);auto foul=reality_foul(state,scene,12,true,1,0,true,rand,{});
        check(!foul.result && rand.calls()==1,"zero foul propensity short-circuits after its single Source Watcom roll");
        state.manager_teams={1};state.manager_records.resize(0xb0c);state.tactics.resize(0x840);
        state.tactics[5]=239; // PID12 - squad start24 clamps to row0, not offender row1.
        bool event_seen=false;
        for (int seed=1;seed<=64 && !event_seen;++seed) {
            rand=WatcomRandom{uint32_t(seed)};int called=0;
            foul=reality_foul(state,scene,12,true,1,0,true,rand,[&](int slot,int side) {
                ++called;check(slot==25 && side==1,"forced-event callback receives absolute Source slot and offender side");return uint8_t(0x82);
            });
            if (!called) {
                check((foul.result==2 && rand.calls()==3) || (!foul.result && rand.calls()==1),
                      "Source no-foul or unpunished-foul branches retain their exact draw boundary");continue;
            }
            event_seen=true;
            check(foul.result==3 && foul.restart==2 && foul.event_stops_scene && foul.event_kind==2 && !foul.whistle,
                  "defensive foul yields penalty, forced-event byte and explicit stop while muted sound draw remains");
            check(foul.cues.size()==3 && foul.cues[0].actor==1 && foul.cues[0].code==19 &&
                  foul.cues[1].actor==-1 && foul.cues[1].code==20 && foul.cues[2].actor==12 && foul.cues[2].code==21,
                  "Source foul cues preserve the unassigned actor slot for code20");
            check(rand.calls()==3 && called==1,"forced callback boundary neither duplicates prefix draws nor adds placeholder randomness");
            rand=WatcomRandom{uint32_t(seed)};bool missing=false;
            try { reality_foul(state,scene,12,true,1,0,true,rand,{}); } catch(const FormatError&) { missing=true; }
            check(missing && rand.calls()==3,"missing real booking engine is explicit at exact Source event boundary");
        }
        check(event_seen,"human literal board index reaches real forced-event boundary");
        bool whistle_seen=false;
        for (uint32_t seed=1;seed<=64;++seed) {
            WatcomRandom silent(seed),observed(seed);
            int plain_events=0,heard_events=0;std::vector<int> order;
            const auto plain=reality_foul(state,scene,12,true,1,0,false,silent,
                [&](int,int){++plain_events;return uint8_t(0x82);});
            const auto heard=reality_foul(state,scene,12,true,1,0,false,observed,
                [&](int,int){++heard_events;order.push_back(100);return uint8_t(0x82);},
                [&](uint8_t id){check(id==21,"foul emits only the Original whistle");order.push_back(id);});
            check(silent.seed()==observed.seed() && silent.calls()==observed.calls() &&
                  plain_events==heard_events && plain.result==heard.result && plain.restart==heard.restart &&
                  plain.event_result==heard.event_result && plain.scene_status==heard.scene_status &&
                  plain.whistle==heard.whistle && plain.cues.size()==heard.cues.size(),
                  "sound observer preserves foul decisions, callback count and full Watcom state");
            check(std::count(order.begin(),order.end(),21)==int(heard.whistle),
                  "one sound only when the existing qualified whistle draw succeeds");
            if(heard.whistle) {
                whistle_seen=true;check(order.front()==21,"whistle precedes the forced event UI");
            }
            WatcomRandom muted(seed);int muted_sounds=0;
            const auto quiet=reality_foul(state,scene,12,true,1,0,true,muted,
                [](int,int){return uint8_t(0x82);},[&](uint8_t){++muted_sounds;});
            check(!muted_sounds && !quiet.whistle && muted.seed()==silent.seed() && muted.calls()==silent.calls(),
                  "muted foul retains every Original draw without emitting a whistle");
        }
        check(whistle_seen,"sound observer probe reaches an actual whistle");
        std::puts("PASS: Source free dribble, signed speed, foul rolls/board index, penalty, cues and explicit forced-card callback; no Native card integration claimed");return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
