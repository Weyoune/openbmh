#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <cstdio>
#include <stdexcept>
#include "formats/fbm.hpp"
#include "formats/textd.hpp"
#include "game/messages.hpp"
#include "game/reality_scene.hpp"
#include "game/save.hpp"
using namespace bmh;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
GameState fixture(const std::filesystem::path& data,const LeImage& exe,const TextDb& texts) {
 const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
 auto state=GameState::from_save(SaveState(load_savegame(data/"AUTOSAVE.FBM").stream,counts),counts);
 state.texts=&texts;
 state.manager_teams.clear();state.team_morale[0]=state.team_morale[1]=24;
 for(int side=0;side<2;++side)for(int row=0;row<24;++row){const int id=side*24+row;
  state.squad_slots[size_t(id)]=uint16_t(id|(row<11?kSlotInLineup:0));
  state.players[size_t(id)].w35c=48u|(100u<<7)|(100u<<14);state.players[size_t(id)].w368=3u|(3u<<4)|(3u<<6);
 }
 return state;
}
void place(RealityScene& scene,int figure,int x,int y) {
 check(scene.setup.grid.place(figure,x,y,figure/11),"test occupancy");scene.setup.motion.figures[size_t(figure)]={true,x,y,x,y,0};
 const int pid=(figure/11)*24+figure%11;scene.setup.players[size_t(figure)]={pid,pid,0,x,y,0,42,0,24};
}
RealityScene ready(const LeImage& exe) {
 RealityScene scene;scene.setup.ready=true;scene.setup.teams={0,1};scene.reporter=initialize_reality_reporter(exe);
 scene.context.retained_cue_actors.fill(0);scene.result.side=0;return scene;
}
void ball(RealityScene& scene,int x,int y) {
 scene.setup.grid.place(22,x,y,2);scene.setup.motion.figures[22]={true,x,y,x,y,0};scene.setup.motion.figures[23]=scene.setup.motion.figures[22];
}
}
int main(int argc,char** argv){try{
 check(argc==2,"usage: reality_scene_tests <Original data>");const auto data=std::filesystem::path(argv[1]);const auto exe=bmh::test::load_executable(data/"BMH.exe");const auto texts=load_textd(data/"TEXTD.BLK");auto state=fixture(data,exe,texts);
 auto empty=state;for(auto& slot:empty.squad_slots)slot=kEmptySlot;empty.squad_slots[0]=kSlotInLineup;
 RealityScene unavailable;RealitySceneContext initial;WatcomRandom iw(12345);GameRandom ig(67890);
 initialize_reality_scene(empty,{0,1},0,reality_formations(empty,{0,1}),exe,initial,iw,ig,unavailable);
 check(unavailable.finished&&unavailable.result.stop==RealitySceneStop::unavailable&&unavailable.result.scene_status==3&&!unavailable.result.return_flags,"unavailable initialization retains Source initial status3 and returns no shot");
 auto scene=ready(exe);place(scene,1,20,12);place(scene,12,30,12);ball(scene,20,12);
 auto native=save_stream(state);WatcomRandom w(12345);GameRandom g(1);
 const auto first=advance_reality_scene(state,scene,exe,w,g);
 check(first.ball_actor==-1&&first.moved_figures==std::vector<int>{12}&&first.reports.empty(),"first Source round only moves defenders, no early ball or reporter draw");
 check(scene.setup.grid.position(1)==std::pair{20,12}&&scene.setup.grid.position(12)==std::pair{32,12},"first defensive round keeps attacker waiting and advances exact defender position");
 check(scene.stagnation==1&&!scene.first_round&&scene.rounds==1&&first.motion&&w.calls()==1&&!g.calls(),"first-round phase boundary, stagnation and Source RNG counts");
 check(save_stream(state)==native,"normal round has no fabricated Native effect");
 {
  auto copy=fixture(data,exe,texts);auto with_sound=ready(exe);
  place(with_sound,1,20,12);place(with_sound,12,30,12);ball(with_sound,20,12);
  std::vector<uint8_t> sounds;with_sound.context.sound=[&](uint8_t id){sounds.push_back(id);};
  WatcomRandom ow(12345);GameRandom og(1);
  const auto heard=advance_reality_scene(copy,with_sound,exe,ow,og);
  check(sounds==std::vector<uint8_t>{16} && heard.motion && heard.moved_figures==first.moved_figures,
        "one Original motion cue per committed round, with identical presentation movement");
  check(ow.seed()==w.seed() && ow.calls()==w.calls() && og.seed()==g.seed() && og.calls()==g.calls() &&
        save_stream(copy)==native && with_sound.setup.grid.position(12)==scene.setup.grid.position(12),
        "sound observer preserves full Native output and both RNG states");
 }
 const auto second=advance_reality_scene(state,scene,exe,w,g);
 check(second.ball_actor==1&&scene.setup.grid.ball()==scene.setup.grid.position(1)&&scene.setup.grid.ball().first>20,"next round executes actual carrier dribble before remaining figures");
 check(!second.reports.empty()&&scene.reporter_frame.incoming_bh==0&&scene.stagnation==0,"actual ball report uses qualified zero BH and resets stagnant counter");
 check(second.moved_figures==std::vector<int>{12},"carrier is excluded from later same-round movement");

 scene=ready(exe);scene.setup.attacking_side=1;scene.result.side=1;
 for(int id=0;id<22;++id)place(scene,id,(id/11)*22+(id%11)*2,2+(id%11)*2);
 ball(scene,20,12);w=WatcomRandom(12345);g=GameRandom(1);
 const auto away=advance_reality_scene(state,scene,exe,w,g);
 check(away.moved_figures==std::vector<int>({10,9,8,7,6,5,4,3,2,1,0})&&away.ball_actor==-1,"away Source round traverses21 down to0 and moves defenders10 down to0");

 bool accepted=false,rejected=false;
 for(uint32_t seed=1;seed<=32;++seed){scene=ready(exe);place(scene,12,20,12);ball(scene,20,12);
  const int minimum=scene.setup.players[0].min_x;w=WatcomRandom(seed);g=GameRandom(1);
  const auto tick=advance_reality_scene(state,scene,exe,w,g);
  if(tick.finished){rejected=true;check(scene.result.stop==RealitySceneStop::possession_ended&&!scene.result.return_flags&&w.calls()==1&&!g.calls(),"rejected possession handoff ends before figure draws");
   check(tick.reports.size()==1&&tick.reports[0].text==std::vector<std::string>{message_text(state,::bmh::layout::RealityScene_AdvanceRealityScene_TextPointer)}&&!tick.motion,"possession-ended message is actual Source text, no animation");}
  else{accepted=true;check(scene.setup.attacking_side==1&&scene.return_flags==0x80&&scene.setup.players[0].min_x==minimum+int8_t(exe.u8(::bmh::layout::FixtureRealityHandoffSignedBias)),"accepted handoff toggles bit80 and shifts all movement boxes by Original signed bias");}
 }
 check(accepted&&rejected,"both actual Watcom possession-handoff branches reached");
 scene=ready(exe);ball(scene,20,12);w=WatcomRandom(12345);g=GameRandom(1);
 for(int i=0;i<4;++i){const auto tick=advance_reality_scene(state,scene,exe,w,g);check(!tick.finished,"first four carrierless rounds continue");}
 const auto ended=advance_reality_scene(state,scene,exe,w,g);
 check(ended.finished&&scene.result.stop==RealitySceneStop::possession_ended&&scene.rounds==4&&!w.calls()&&!g.calls(),"fifth carrierless attempt ends without a substitute RNG roll or synthetic iteration cap");
 const auto wc=w.calls(),gc=g.calls();bool refused=false;
 try{advance_reality_scene(state,scene,exe,w,g);}catch(const FormatError&){refused=true;}
 check(refused&&w.calls()==wc&&g.calls()==gc,"finished scene cannot advance or draw again");
 scene=ready(exe);ball(scene,42,12);w=WatcomRandom(12345);g=GameRandom(1);
 check(advance_reality_scene(state,scene,exe,w,g).finished&&scene.result.stop==RealitySceneStop::goal_mouth&&!scene.rounds&&!w.calls()&&!g.calls(),"goal-mouth boundary precedes actor lookup and movement");
 scene=ready(exe);ball(scene,20,12);scene.result.shooter_row=1;scene.result.assistant_row=12;scene.setup.players[1].player=1;
 w=WatcomRandom(12345);g=GameRandom(1);const auto exit=advance_reality_scene(state,scene,exe,w,g,{},true);
 check(exit.motion&&exit.finished&&scene.result.stop==RealitySceneStop::user_exit&&scene.result.shooter==1&&scene.result.assistant==0xfff&&scene.result.return_flags==1,"exit is checked after animation; final shooter mapped, cross-side assist discarded, Source shot-return bit retained");
 scene=ready(exe);ball(scene,20,12);scene.result.shooter_row=12;scene.result.assistant_row=13;scene.setup.players[12].player=24;scene.setup.players[13].player=25;
 w=WatcomRandom(12345);g=GameRandom(1);advance_reality_scene(state,scene,exe,w,g,{},true);
 check(scene.result.shooter==24&&scene.result.assistant==25&&scene.result.shooter_row==1&&scene.result.assistant_row==2,"final same-side figures convert to actual PIDs and zero-based side rows");
 check(save_stream(state)==native,"owned Scene loop never invents changes to ordinary Native state");
 std::puts("PASS: Source round order and waiting, carrier-before-movement, qualified caller BH, real possession handoff/termination, stagnant and goal-mouth boundaries, post-animation exit, final PID/assist mapping; no Main/deferred settlement claim");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
