#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <cstdio>
#include <stdexcept>
#include "formats/fbm.hpp"
#include "game/lineup.hpp"
#include "game/reality_match.hpp"
#include "game/save.hpp"
using namespace bmh;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
MatchGroup fixture(){MatchGroup g;g.desc.resize(0xd0);g.fixtures={0,1};g.results={0};g.spectators={12000};g.records={70,70,90,90,90,90,80,60,3,70,70,90,90,90,90,80,60,3};return g;}
}
int main(int argc,char** argv){try{
 check(argc==2,"usage: reality_deferred_tests <Original data>");const auto data=std::filesystem::path(argv[1]);const auto exe=bmh::test::load_executable(data/"BMH.exe");
 const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
 auto base=GameState::from_save(SaveState(load_savegame(data/"AUTOSAVE.FBM").stream,counts),counts);
 base.manager_teams.clear();base.live_matches={0};base.team_morale[0]=base.team_morale[1]=128;
 for(int side=0;side<2;++side)for(int row=0;row<24;++row){const int pid=side*24+row;
  base.squad_slots[size_t(pid)]=uint16_t(pid|(row<11?kSlotInLineup:row<17?0x4000:0));
  base.players[size_t(pid)].w35c=48u|(40u<<7)|(100u<<14);base.players[size_t(pid)].w364=uint32_t(row==0?0:1+row%3);base.players[size_t(pid)].w368=3u|(3u<<4)|(3u<<6);
 }
 for(int row=0;row<24;++row){const size_t at=size_t(row)*0x58;
  base.tactics[at+2]=19;base.tactics[at+3]=99;base.tactics[at+4]=99;
  base.tactics[at+0x41]=uint8_t(row?5+row%11:0);base.tactics[at+0x42]=uint8_t(row%9);
  for(int offset:{0x44,0x46}){base.tactics[at+offset]=0xff;base.tactics[at+offset+1]=0x0f;}
 }
 int auto_count=0,human_count=0,no_bench_count=0;bool both_sides[2]={false,false};
 for(int human=0;human<2;++human)for(int side=0;side<2;++side)for(uint32_t seed=1;seed<=256;++seed){
  auto state=base;if(human){state.manager_teams={side};state.manager_options.assign(5,0);state.squad_slots[size_t(side*24+1)]|=0x1000;}
  std::vector<MatchGroup> groups{fixture()};WatcomRandom w(111);GameRandom g(777);MatchEngine engine(state,groups,g,w,{24});RealityMatchScene owner;RealitySceneContext context;
  initialize_reality_match_scene(engine,0,0,side,42,exe,context,owner);MinuteReport report;
  check(owner.scene.setup.ready,"actual Source initialization reaches a scene");
  check(owner.scene.context.referee_index==0&&owner.scene.context.referee_profile==24,"bridge reads actual descriptor referee index/profile");
  w=WatcomRandom(seed);const int slot=side*24+1,player=state.squad_slots[size_t(slot)]&0xfff;
  const auto event=engine.forced_event(groups[0],0,side,42,slot,report);check(!report.unsupported,report.reason.c_str());
  if(!event.change)continue;
  owner.pending=RealityDeferredRequest{*event.change,LiveMatchEvent{0,0,side,side,1-side,player,human?1:0,42,human?0:-1},slot};
  const auto before_early=save_stream(state);const auto before_w=w.seed();const auto before_g=g.seed();const auto wc=w.calls(),gc=g.calls();
  bool early=false;try{settle_reality_match_scene(engine,owner,report);}catch(const FormatError&){early=true;}
  check(early&&owner.pending&&save_stream(state)==before_early&&w.seed()==before_w&&g.seed()==before_g&&w.calls()==wc&&g.calls()==gc,"unfinished scene never consumes queued request, Native or RNG");
  // End through the actual Source round/exit boundary. Requests are normally
  // generated inside that round; this fixture retains its real engine request
  // across the isolated owner test, without fabricating a card return.
  const auto pending=*owner.pending;owner.pending.reset();
  advance_reality_match_scene(engine,owner,exe,report,true);check(owner.scene.finished,"actual post-animation user exit completes Source scene");owner.pending=pending;
  const auto card_native=save_stream(state);const auto records=groups[0].records;
  if(!human){
   auto expected=state;auto ew=w;const auto gs=g.seed();const auto gcall=g.calls();
   const bool changed=substitute(expected,side,slot,event.change->mode,ew);
   check(settle_reality_match_scene(engine,owner,report)&&!owner.pending&&owner.settled,"automatic request attempted and consumed once after scene end");
   check(save_stream(state)==save_stream(expected)&&w.seed()==ew.seed()&&w.calls()==ew.calls()&&g.seed()==gs&&g.calls()==gcall,"entire Native and Watcom state equal existing Source substitution; no Game draw or fabricated replacement");
   check(groups[0].records==records,"deferred tail adds no second Source rerating");
   if(changed)++auto_count;both_sides[side]=true;
   // Legal no-fit bench consumes the attempt once without a change/draw.
   owner.pending=pending;owner.settled=false;
   for(int row=11;row<17;++row)state.squad_slots[size_t(side*24+row)]=kEmptySlot;
   owner.pending->event.player=state.squad_slots[size_t(slot)]&0xfff;
   owner.pending->source.mode=state.players[size_t(owner.pending->event.player)].position()+20;
   const auto no_bench_native=save_stream(state);const auto nws=w.seed();
   check(settle_reality_match_scene(engine,owner,report)&&save_stream(state)==no_bench_native&&w.seed()==nws,"no eligible bench is normal Source no-change, not a retry or invented player");++no_bench_count;
  }else{
   check(!settle_reality_match_scene(engine,owner,report)&&report.unsupported&&owner.pending&&!owner.settled&&save_stream(state)==card_native,"missing human presenter remains explicit and retains real card/request");report={};
   int calls=0;engine.live_event=[&](const LiveMatchEvent& e){++calls;check(owner.scene.finished&&owner.settling&&e.group_index==0&&e.match_index==0&&e.side==side&&e.team==side&&e.opponent==1-side&&e.player==player&&e.kind==1&&e.minute==42&&e.manager_slot==0,"post-scene human event retains actual Source offender/fixture/manager identity");
    bool nested=false;try{settle_reality_match_scene(engine,owner,report);}catch(const FormatError&){nested=true;}check(nested,"reentrant presenter cannot execute deferred request twice");};
   const auto ws=w.seed();const auto gs=g.seed();const auto wn=w.calls(),gn=g.calls();
   check(settle_reality_match_scene(engine,owner,report)&&calls==1&&owner.settled&&!owner.pending,"human presenter dispatches once after scene end");
   check(w.seed()==ws&&g.seed()==gs&&w.calls()==wn&&g.calls()==gn&&groups[0].records==records,"human dispatch itself adds no RNG or duplicate rating");++human_count;
   owner.pending=pending;owner.settled=false;engine.live_event={};engine.leave_screens=true;
   check(settle_reality_match_scene(engine,owner,report)&&!report.unsupported&&!owner.pending,"explicit legacy leave_screens is the only presenter-free human skip");
  }
  const auto final_native=save_stream(state);const auto ws=w.seed();const auto gs=g.seed();
  check(!settle_reality_match_scene(engine,owner,report)&&save_stream(state)==final_native&&w.seed()==ws&&g.seed()==gs,"repeated completion is harmless and drawless");
 }
 check(auto_count&&human_count&&no_bench_count&&both_sides[0]&&both_sides[1],"actual card seeds cover automatic changes, both human sides and no-fit attempts");
 auto state=base;std::vector<MatchGroup> groups{fixture()};WatcomRandom w(111);GameRandom g(777);MatchEngine engine(state,groups,g,w,{24});RealityMatchScene owner;RealitySceneContext context;MinuteReport report;
 initialize_reality_match_scene(engine,0,0,0,42,exe,context,owner);
 const auto native=save_stream(state);const auto ws=w.seed();groups[0].fixtures[0]=2;bool stale=false;
 try{advance_reality_match_scene(engine,owner,exe,report,true);}catch(const FormatError&){stale=true;}
 check(stale&&save_stream(state)==native&&w.seed()==ws,"changed fixture rejected before a round or deferred effect");
 groups[0].fixtures[0]=0;auto replaced=groups;groups.swap(replaced);stale=false;
 try{advance_reality_match_scene(engine,owner,exe,report,true);}catch(const FormatError&){stale=true;}
 check(stale&&save_stream(state)==native&&w.seed()==ws,"replaced fixture objects rejected even when team identities are equal");
 std::printf("PASS: Source deferred automatic=%d human=%d no-fit=%d; actual cards, no early/modal/reentrant/double effect, exact Native/W substitution, no extra rating, explicit human boundary and stale fixture rejection; not Main\n",auto_count,human_count,no_bench_count);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
