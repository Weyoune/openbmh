#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "formats/fbm.hpp"
#include "formats/le.hpp"
#include "game/career.hpp"
#include "game/cup.hpp"
#include "game/national.hpp"
#include "game/save.hpp"
#include "game/schedule.hpp"
#include "game/season.hpp"

using namespace bmh;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
GameState load(const std::filesystem::path& path, const LeImage& exe, const TextDb& texts) {
    const auto file=load_savegame(path);
    check(file.checksum_ok && file.version=="V2.24", "native fixture checksum/version");
    const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
    GameState g=GameState::from_save(SaveState(file.stream,counts),counts);
    competition_pointers(g.memory,exe);g.texts=&texts;national_coach_after_load(g);return g;
}

struct Run {
 GameState state; GameRandom game; WatcomRandom rand; MinuteReport report;
 SeasonTransitionOutcome outcome=SeasonTransitionOutcome::NextSeason;
 std::vector<std::string> pauses;
#ifndef CUP_DRAW_BASELINE
 std::vector<SeasonCupDrawEvent> events;
#endif
};
Run run(GameState initial,uint32_t seed,int32_t game_seed,bool observe,bool setup=false) {
 Run r;r.state=std::move(initial);r.game=GameRandom(game_seed);r.rand=WatcomRandom(seed);
#ifndef CUP_DRAW_BASELINE
 Bytes held;uint64_t gc=0,wc=0;int32_t gs=0;uint32_t ws=0;int group_comp=-1,group_ordinal=0;
 auto hook=[&](const GameState& g,const SeasonCupDrawEvent& e){
  check(e.phase==(r.events.empty()||r.events.back().phase==SeasonCupDrawPhase::overview?SeasonCupDrawPhase::draw:SeasonCupDrawPhase::overview),"each source draw is followed by its overview");
  check(e.participants.size()==e.count && e.count>0,"owned event has a complete participant array");
  const uint32_t participant_at=g.memory.pointer(e.entry_address)+uint32_t(e.offset)*2;
  for(size_t i=0;i<e.count;++i) check(e.participants[i]==g.memory.u16(participant_at+i*2),"owned participants copy actual current field order");
  if(e.group_stage) {
   check(e.competition>=4&&e.competition<=7&&!(e.flags&4),"international group stage uses the Source competition and flags");
   const uint8_t expected_flags=e.competition==5||e.competition==7?0xc3:0xc0;
   check(e.flags==expected_flags,"native calendar group flags match the four Source first-draw variants");
   if(e.competition!=group_comp){group_comp=e.competition;group_ordinal=0;}
   check(e.group_count==int(g.memory.u8(e.entry_address+0x1e)&0xf)&&e.group_index==group_ordinal,"group count comes from entry and index follows Source traversal");
   check(cup_group_table(g.memory,e.entry_address).size==int(e.count),"native group standings use the Core legacy routine projection");
  } else {
   check(e.entry_address==kCupTable&&e.competition==0&&e.round==0&&e.offset==0&&e.count==64,"national first-round metadata remains unchanged");
  }
  if(e.phase==SeasonCupDrawPhase::draw) {
   // Source does not clear the overview marker between linked groups or
   // before the following national draw. Only the first draw precedes it.
   check((g.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField)&0x80)==(r.events.empty()?0:0x80),
         "draw retains the marker from preceding Source overviews");
   held=save_stream(g);gc=r.game.calls();wc=r.rand.calls();gs=r.game.seed();ws=r.rand.seed();
  } else {
   const auto& draw=r.events.back();check(draw.phase==SeasonCupDrawPhase::draw&&draw.entry_address==e.entry_address,"overview matches the immediately preceding draw entry");
   check((g.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField)&0x80)!=0,"overview follows manager-screen effect");
   if(e.group_stage)check(g.memory.u8(::bmh::layout::CareerSession_FormatError_RuntimeField8)==e.group_index,"group overview applies the Source group cursor");
   else {auto pairs=[](const std::vector<uint16_t>& v){std::vector<std::pair<uint16_t,uint16_t>> p;for(size_t i=0;i<v.size();i+=2)p.emplace_back(std::min(v[i],v[i+1]),std::max(v[i],v[i+1]));std::sort(p.begin(),p.end());return p;};
    check(pairs(e.participants)==pairs(draw.participants),"national sort retains all original drawn ties");}
   check(r.game.calls()==gc && r.rand.calls()==wc && r.game.seed()==gs && r.rand.seed()==ws,"sorting/overview/observation consume no RNG");
   if(e.group_stage)++group_ordinal;
  }
  const auto before=save_stream(g);r.events.push_back(e);
  check(save_stream(g)==before,"retaining owned event does not mutate state");
  if(std::getenv("CUP_FIXTURE_OUTPUT")) {
   auto dir=std::filesystem::path(std::getenv("CUP_FIXTURE_OUTPUT"));std::filesystem::create_directories(dir);
   auto fixture_rand=r.rand;write_savegame(dir/(e.phase==SeasonCupDrawPhase::draw?"draw.FBM":"overview.FBM"),make_savegame(g,fixture_rand));
  }
 };
#endif
 auto pause=[&](const char* label){r.pauses.emplace_back(label);
#ifndef CUP_DRAW_BASELINE
  if(observe && std::string(label)=="the national cup draw (legacy routine)") {
   check(!r.events.empty()&&r.events.back().phase==SeasonCupDrawPhase::draw&&r.events.back().competition==0&&save_stream(r.state)==held,"national draw event precedes pause and untouched field stays available until return");
   check(r.game.calls()==gc && r.rand.calls()==wc && r.game.seed()==gs && r.rand.seed()==ws,"pause boundary retains both RNGs");
  }
#endif
 };
#ifdef CUP_DRAW_BASELINE
 (void)observe;if(setup)check(career_season_setup(r.state,r.game,r.rand,r.report,pause),"career setup");
 else r.outcome=season_transition(r.state,r.game,r.rand,r.report,pause);
#else
 auto observer=observe?SeasonCupDrawHook(hook):SeasonCupDrawHook{};
 if(setup)check(career_season_setup(r.state,r.game,r.rand,r.report,pause,observer),"career setup");
 else r.outcome=season_transition(r.state,r.game,r.rand,r.report,pause,{},{},{},observer);
#endif
 check(!r.report.unsupported,"native path must complete");return r;
}
void equal(const Run& a,const Run& b) {
 check(a.outcome==b.outcome && save_stream(a.state)==save_stream(b.state) && a.game.seed()==b.game.seed() && a.game.calls()==b.game.calls() && a.rand.seed()==b.rand.seed() && a.rand.calls()==b.rand.calls() && a.pauses==b.pauses,"observer preserves full native bytes, outcome, both RNGs and pauses");
}
}
int main(int argc,char**argv){try {
 if(argc>=2 && std::string(argv[1]).empty()){std::cout<<"SKIP: BMH_DATA not configured\n";return 77;}
 check(argc>=3,"usage: cup_draw_tests <BMH data> <FBM> [Wseed] [Gseed] [outputstream] [setup]");
 auto exe=bmh::test::load_executable(std::filesystem::path(argv[1])/"BMH.exe");auto texts=load_textd(std::filesystem::path(argv[1])/"TEXTD.BLK");auto native=load(argv[2],exe,texts);
 auto seed=argc>3?uint32_t(std::stoul(argv[3])):589388519u;auto gs=argc>4?int32_t(std::stol(argv[4])):665367;bool setup=argc>6;
 Run plain;int total_group_entries=0;
#ifndef CUP_DRAW_BASELINE
 static const int expected_groups[4]={1,2,1,6};
 for(int period=0;period<4;++period) {
  auto initial=native;initial.season=int16_t(period-1); // explicit calendar scalar transform; all teams/entries remain the native SEASON_INPUT data
  initial.memory.set8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField,0); // Source pre-overview marker; outside the persisted save stream.
  initial.memory.set8(::bmh::layout::LeaguePresentationOptions+5*9,0); // keep the unchanged national-draw observer boundary active
  const int comp=7-period;initial.memory.set8(::bmh::layout::LeaguePresentationOptions+(comp+5)*9,0); // select only this source first-draw option
  auto baseline=run(initial,seed,gs,false,setup);auto observed=run(initial,seed,gs,true,setup);equal(baseline,observed);
  int group_entries=0;for(const auto& e:observed.events)if(e.group_stage&&e.phase==SeasonCupDrawPhase::overview){
   check(e.competition==comp,"period selects the source international competition");
   check(e.count==(period==2?6u:4u),"native initial group size matches the four Source variants");++group_entries;
  }
  check(group_entries==expected_groups[period],"native group-chain length matches the four Source calendar variants");
  check(observed.events.size()==size_t(2+2*group_entries),"group chain and unchanged national draw each expose draw/overview");
  auto off=initial;off.memory.set8(::bmh::layout::LeaguePresentationOptions+5*9,1);off.memory.set8(::bmh::layout::LeaguePresentationOptions+(comp+5)*9,1);
  auto hidden_plain=run(off,seed,gs,false,setup);auto hidden=run(off,seed,gs,true,setup);equal(hidden_plain,hidden);
  check(hidden.events.empty(),"Source display-off return suppresses callbacks and preserves disabled-option baseline");
  if(period==0)plain=std::move(baseline);
  total_group_entries+=group_entries;
  std::cout<<"period "<<period<<" competition "<<comp<<" groups "<<group_entries<<" entry teams";
  for(const auto& e:observed.events)if(e.group_stage&&e.phase==SeasonCupDrawPhase::draw){std::cout<<" ["<<e.group_index<<":";for(auto t:e.participants)std::cout<<' '<<t;std::cout<<" ]";}
  std::cout<<"\n";
 }
 std::cout<<"SEASON_INPUT real-ID group chains="<<total_group_entries<<", observer/nonobserver full-stream + G/W seeds/calls, table projection, Source entry effects, option guard OK\n";
#else
 auto initial=native;initial.season=-1;initial.memory.set8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField,0);plain=run(initial,seed,gs,false,setup);
#endif
 std::cout<<"final "<<int(plain.outcome)<<" date "<<plain.state.date<<" season "<<plain.state.season<<" G "<<plain.game.seed()<<'/'<<plain.game.calls()<<" W "<<plain.rand.seed()<<'/'<<plain.rand.calls()<<" pauses "<<plain.pauses.size()<<'\n';
 if(argc>5){auto stream=save_stream(plain.state);std::ofstream f(argv[5],std::ios::binary);f.write(reinterpret_cast<const char*>(stream.data()),stream.size());}
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
