#include "runtime_fixture_tables.hpp"
#include "test_inputs.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include "app/controller.hpp"
#include "formats/fbm.hpp"
#include "game/league_pairings.hpp"
#include "game/messages.hpp"
#include "game/save.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"
#include "ui/desk_state.hpp"
#include "ui/league_pairings.hpp"
#include "ui/strengths_state.hpp"
using namespace bmh; namespace fs=std::filesystem;
namespace {
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class F> void rejects(F f,const char* why){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,why);}
struct Oracle {uint32_t w;int32_t g;uint64_t wc,gc;
 void stir(){++wc;w=w*0x41c64e6du+0x3039;const int n=int(((w>>16)&32767)*201u/32768)+100;
 for(int k=0;k<n;++k){++gc;g=int32_t(uint32_t(g)*16u+167u)%4000000;}}
};
bool same_pairings_checkpoint(const app::CareerSession::LeaguePairingsCheckpoint& a,
    const app::CareerSession::LeaguePairingsCheckpoint& b) {
 if(save_stream(a.state)!=save_stream(b.state)||a.runtime!=b.runtime||a.game.seed()!=b.game.seed()||
    a.game.calls()!=b.game.calls()||a.rand.seed()!=b.rand.seed()||a.rand.calls()!=b.rand.calls()||
    a.draft.has_value()!=b.draft.has_value())return false;
 if(!a.draft)return true;
 const auto& x=*a.draft;const auto& y=*b.draft;
 return save_stream(x.state)==save_stream(y.state)&&x.game.seed()==y.game.seed()&&x.game.calls()==y.game.calls()&&
  x.rand.seed()==y.rand.seed()&&x.rand.calls()==y.rand.calls()&&x.runtime==y.runtime&&
  x.original_runtime==y.original_runtime&&x.owner==y.owner&&x.identity==y.identity&&
  x.original_stream==y.original_stream&&x.original_rand_seed==y.original_rand_seed&&
  x.original_game_seed==y.original_game_seed&&x.original_rand_calls==y.original_rand_calls&&
  x.original_game_calls==y.original_game_calls&&x.active_packet==y.active_packet&&
  x.original_active_groups==y.original_active_groups&&x.original_referee_profiles==y.original_referee_profiles;
}
bool same_groups(const std::vector<MatchGroup>& a,const std::vector<MatchGroup>& b) {
 if(a.size()!=b.size())return false;
 for(size_t n=0;n<a.size();++n){const auto& x=a[n];const auto& y=b[n];
  if(x.desc!=y.desc||x.fixtures!=y.fixtures||x.results!=y.results||x.cup_results!=y.cup_results||
   x.cup_other_leg!=y.cup_other_leg||x.cup_result_address[0]!=y.cup_result_address[0]||
   x.cup_result_address[1]!=y.cup_result_address[1]||x.phase!=y.phase||x.records!=y.records||
   x.end_minute!=y.end_minute||x.spectators!=y.spectators||x.log!=y.log||x.ended!=y.ended||
   x.kickoff_void_mask!=y.kickoff_void_mask)return false;
 }return true;
}
MatchGroup declared_pairings_group(int league,int round) {
 MatchGroup group;group.desc.resize(0xd0);
 const auto fixtures=league_fixtures(league,round+1);
 for(size_t n=0;n<fixtures.size();++n){group.fixtures.push_back(fixtures[n].home);group.fixtures.push_back(fixtures[n].away);
  group.desc[0x10+n]=uint8_t(n+1);group.desc[0x30+n]=uint8_t(int(n)-7);group.desc[0x50+n]=uint8_t(n%8);}
 group.results.assign(fixtures.size(),0xcc);return group;
}
void active_packet_core(const fs::path& data,const fs::path& input) {
 app::CareerSession live(data,731,input);auto g=live.state();g.league_rounds.assign(9,0);g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,1);
 const auto constants=ui::league_pairings_constants(bmh::test::load_executable(data/"BMH.exe"));
 auto owner=open_league_pairings_source(g,0,constants);
 g.master_database_metadata.assign(100,0);g.referee_names={'S',0};
 std::vector<uint8_t> profiles(50,0);for(size_t n=1;n<profiles.size();++n)profiles[n]=uint8_t(0x80|(n&3)|((n%4)<<3));
 auto first=declared_pairings_group(owner.league,owner.round);auto last=first;last.desc[0x50]=7;
 std::vector<MatchGroup> groups{first,last};const auto original_groups=groups;const auto native=save_stream(g);
 const auto game=live.game();const auto watcom=live.rand();
 auto packet=league_pairings_active_packet_source(g,owner,groups,profiles);
 check(packet&&packet->group_index==1&&packet->rows.size()==9&&packet->rows[0].weather==7,
  "Original scan binds the last matching active descriptor, not the first");
 owner.active_group=packet->group_index;validate_league_pairings_active_packet_source(g,owner,*packet);
 for(size_t n=0;n<packet->rows.size();++n){const auto& r=packet->rows[n];const auto f=league_fixtures(owner.league,owner.round+1)[n];
  check(r.source_round==owner.round&&r.fixture_index==int(n)&&r.home_runtime_team==f.home&&r.away_runtime_team==f.away&&
   r.home_club_identity==g.club_ids[size_t(f.home)]&&r.away_club_identity==g.club_ids[size_t(f.away)]&&
   r.referee_index==n+1&&r.referee_profile==profiles[n+1]&&r.referee_name=="S"&&r.temperature==int(n)-7,
   "active packet owns ordered Native fixture identities, signed temperature and live referee profile/name");}
 check(save_stream(g)==native&&same_groups(groups,original_groups)&&live.game().seed()==game.seed()&&
  live.game().calls()==game.calls()&&live.rand().seed()==watcom.seed()&&live.rand().calls()==watcom.calls(),
  "active packet projection leaves all Native/groups/W/G fields unchanged");
 auto broken=groups;broken.back().fixtures[4]^=1;
 rejects([&]{league_pairings_active_packet_source(g,owner,broken,profiles);},"first pair cannot conceal later mismatched fixture order");
 broken=groups;broken.back().fixtures.pop_back();
 rejects([&]{league_pairings_active_packet_source(g,owner,broken,profiles);},"truncated matching fixture vector rejected");
 broken=groups;broken.back().desc.resize(0x50);
 rejects([&]{league_pairings_active_packet_source(g,owner,broken,profiles);},"truncated active descriptor rejected");
 auto bad=g;bad.master_database_metadata.resize(2);
 rejects([&]{league_pairings_active_packet_source(bad,owner,groups,profiles);},"missing referee offset slot rejected");
 bad=g;bad.master_database_metadata[2]=255;bad.master_database_metadata[3]=255;
 rejects([&]{league_pairings_active_packet_source(bad,owner,groups,profiles);},"out of pool referee offset rejected");
 bad=g;bad.referee_names={'S'};
 rejects([&]{league_pairings_active_packet_source(bad,owner,groups,profiles);},"unterminated referee name rejected");
 rejects([&]{league_pairings_active_packet_source(g,owner,groups,std::span<const uint8_t>{});},"missing live referee profiles rejected");
 auto forged=*packet;forged.rows[1].away_club_identity^=1;
 rejects([&]{validate_league_pairings_active_packet_source(g,owner,forged);},"active packet stable identity must match current Native clubs");
 forged=*packet;forged.rows[0].referee_name="X";
 rejects([&]{validate_league_pairings_active_packet_source(g,owner,forged);},"active packet referee name must match its heap offset");
 GameRandom G=game;WatcomRandom W=watcom;
 auto lower=edit_league_pairings_source(g,owner,{LeaguePairingsAction::select_league,5},constants,G,W);
 groups={declared_pairings_group(5,lower.round)};
 auto six=league_pairings_active_packet_source(g,lower,groups,profiles);check(six&&six->rows.size()==6&&six->rows.back().fixture_index==5,
  "six-row leagues use actual descriptor indexes and ordinary league fixtures");
 lower.active_group=six->group_index;validate_league_pairings_active_packet_source(g,lower,*six);
 auto armed=edit_league_pairings_source(g,lower,{LeaguePairingsAction::filter_by_club},constants,G,W);
 check(armed.filter_team==-2&&league_pairings_active_packet_source(g,armed,groups,profiles),"armed Mode4 -2 retains normal active group projection");
 auto selected=edit_league_pairings_source(g,armed,{LeaguePairingsAction::filter_club,six->rows.front().home_runtime_team,
  six->rows.front().home_club_identity},constants,G,W);
 check(!league_pairings_active_packet_source(g,selected,groups,profiles),"selected club-filter clears the active descriptor");
 auto reset=edit_league_pairings_source(g,selected,{LeaguePairingsAction::reset},constants,G,W);
 auto postponed=edit_league_pairings_source(g,reset,{LeaguePairingsAction::postponed_list},constants,G,W);
 check(!league_pairings_active_packet_source(g,postponed,groups,profiles),"Mode8 clears the active descriptor");
}

void active_career_trigger(const fs::path& data) {
 ui::StartupFlow flow(data);flow.click(150,215);flow.click(5,225,true);flow.click(5,225,true);
 const auto start=flow.click(90,169);std::unique_ptr<app::CareerSession> session;
 uint64_t revision=0;openbmh::audio::Playback audio;audio.set_muted(true);std::optional<uint64_t> audio_session;
 const auto serve=[&](const ui::FlowResult& request){
  const auto message=app::serve_flow_request(flow,request,session,data,data,0x1789abcd,revision,audio,audio_session,false);
  check(message.empty(),message.empty()?"actual career controller request":message.c_str());};
 serve(start);
 const auto menu=[&](int kind){const auto& items=flow.desktop()->state().items;
  const auto at=std::find_if(items.begin(),items.end(),[&](const auto& item){return item.kind==kind&&!item.disabled;});
  check(at!=items.end(),"actual career exposes requested desk menu");return flow.click(at->x+at->width/2,at->y+at->height/2);};
 const auto constants=ui::league_pairings_constants(bmh::test::load_executable(data/"BMH.exe"));bool matching=false;
 for(int turn=0;turn<24;++turn){auto staged=session->state();
  const auto owner=open_league_pairings_source(staged,int(staged.memory.u8(::bmh::layout::ActiveManager)),constants);
  const auto fixtures=league_fixtures(owner.league,owner.round+1);
  for(const auto& group:session->manager_join_checkpoint().driver.groups)
   if(group.fixtures.size()>=2&&group.fixtures[0]==fixtures[0].home&&group.fixtures[1]==fixtures[0].away)matching=true;
  if(matching)break;serve(menu(12));if(flow.in_results_view())serve(flow.send_key("ESC"));
 }
 check(matching,"bounded actual seeded career reaches a current-round active group trigger");
 const auto driver=session->manager_join_checkpoint().driver;const auto before=session->state();
 const auto runtime=session->club_modal_runtime_checkpoint();
 const auto G=session->game();const auto W=session->rand();const auto rev=revision;
 const auto family=session->strengths_open_request(revision);
 check(family.has_value(),"actual menu family entry has source-bound manager identity");
 auto expected_entry=before;auto expected_runtime=runtime;
 const auto& identity=family->identity;
 const uint32_t family_word=::bmh::layout::LeagueTableDisplayModes+uint32_t(identity.manager)*2;
 const auto executable=bmh::test::load_executable(data/"BMH.exe");
 const uint8_t points_mode=executable.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(identity.league));
 expected_entry.memory.set16(family_word,ui::strengths_family_word_source(
  expected_entry.memory.u16(family_word),points_mode));
 expected_runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2]=uint8_t(identity.league);
 expected_runtime[::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField]=uint8_t(identity.league_group);
 expected_runtime[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]=points_mode;
 expected_runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField]=0; // legacy routine: Pairings selector index.
 expected_runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2]=0;
 serve(menu(1));const auto* page=flow.league_pairings();
 check(page&&page->state().active_packet&&page->state().source.owner.active_group>=0&&!page->state().source.owner.active_phase&&
  !page->state().rows.front().active&&!page->state().rows.front().details[0].empty()&&!page->state().rows.front().details[1].empty(),
  "actual controller active group now opens with Original phase-zero rank/strength details");
 check(save_stream(session->state())==save_stream(expected_entry)&&session->club_modal_runtime_checkpoint()==expected_runtime&&
  session->game().seed()==G.seed()&&session->game().calls()==G.calls()&&session->rand().seed()==W.seed()&&
  session->rand().calls()==W.calls()&&revision==rev&&same_groups(driver.groups,session->manager_join_checkpoint().driver.groups),
  "actual active opening applies only Source family-entry Native/runtime words and preserves W/G/revision/full groups");
}

uint16_t staged_pairings_strength(const app::CareerSession::LeaguePairingsDraft& draft) {
 const auto byte=[&](uint32_t at){return draft.state.memory.has(at)?draft.state.memory.u8(at):draft.runtime.at(at);};
 return uint16_t(byte(::bmh::layout::CareerSession_FormatError_ExecutableField3)|unsigned(byte(::bmh::layout::CareerSession_FormatError_ExecutableField4))<<8);
}
void active_app(const fs::path& data,const fs::path& input,bool mixed) {
 app::CareerSession session(data,731,input);auto setup=session.manager_join_checkpoint();
 const int league=team_league(setup.state.manager_teams.at(0));const int round=league_matchdays(league)/2;
 setup.state.league_rounds[size_t(league)]=uint8_t(round);setup.state.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,1);
 const auto fixtures=league_fixtures(league,round+1);
 for(size_t n=0;n<fixtures.size();++n){setup.state.results[result_index(league,round+1,int(n))]=0xcc;
  setup.state.results[result_index(league,round+1-league_matchdays(league)/2,int(n))]=0x12;}
 if(mixed){setup.state.results[result_index(league,round+1,2)]=0x21;setup.state.results[result_index(league,round+1,4)]=0xdd;
  setup.state.memory.set16(::bmh::layout::Postponed,17);setup.state.memory.set8(::bmh::layout::Postponed+2,uint8_t(round));
  setup.state.memory.set8(::bmh::layout::Postponed+3,uint8_t(league));setup.state.memory.set8(::bmh::layout::Postponed+4,4);}
 setup.driver.groups={declared_pairings_group(league,round)};
 // Controlled signed profile variants retain the actual Native name pools.
 setup.driver.referees.at(1)=0x9b;
 session.restore_manager_join_checkpoint(setup);
 uint64_t revision=1;std::string error;const auto native=save_stream(session.state());
 const auto runtime=session.club_modal_runtime_checkpoint();const auto G=session.game();const auto W=session.rand();
 auto stable=[&]{return save_stream(session.state())==native&&session.club_modal_runtime_checkpoint()==runtime&&
  session.game().seed()==G.seed()&&session.game().calls()==G.calls()&&session.rand().seed()==W.seed()&&
  session.rand().calls()==W.calls()&&revision==1&&same_groups(setup.driver.groups,session.manager_join_checkpoint().driver.groups);};
 auto page=session.open_league_pairings({ui::DeskRequestKind::open_menu,0,0,1,0,revision},revision,error);
 check(page&&page->active_packet&&!page->source.owner.active_phase&&stable(),"controlled return-round active page opens staged in phase zero");
 for(const auto& row:page->source.rows)check(row.first_leg_result.has_value(),"phase zero retains every existing played first-leg result");
 const auto phase_zero=*page;
 auto sentinel=session.league_pairings_checkpoint();
 for(int n=0;n<2;++n){const uint32_t at=::bmh::layout::CareerSession_FormatError_ExecutableField3+uint32_t(n);const uint8_t value=uint8_t(0xbeef>>(n*8));
  if(sentinel.draft->state.memory.has(at))sentinel.draft->state.memory.set8(at,value);else sentinel.draft->runtime[at]=value;}
 session.restore_league_pairings_checkpoint(sentinel);
 const ui::LeaguePairingsRequest tick{page->identity,{LeaguePairingsAction::presentation_redraw,1}};
 auto changed=session.dispatch_league_pairings(tick,revision,error);
 check(changed&&!changed->closed&&changed->page.source.owner.active_phase&&stable(),"phase-one redraw is a staged modal transaction only");
 *page=changed->page;const auto& active=page->active_packet->rows.front();
 const auto exe=bmh::test::load_executable(data/"BMH.exe");const auto& state=session.league_pairings_owner()->state;
 const auto referee=active.referee_name+exe.cstring(::bmh::layout::LeaguePairings_FormatError_TextPointer2)+message_text(state,::bmh::layout::LeaguePairings_FormatError_TextPointer6+uint32_t(active.referee_profile&3)*4)+
  exe.cstring(::bmh::layout::LeaguePairings_FormatError_TextPointer3)+message_text(state,::bmh::layout::LeaguePairings_FormatError_TextPointer7+uint32_t((int(int8_t(active.referee_profile))>>3)&3)*4)+exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer11);
 check(page->rows.front().details[0]==referee&&page->rows.front().details[1].empty()&&page->rows.front().active&&
  page->rows.front().temperature==format_number(state,-7,0x81)+message_text(state,::bmh::layout::LeaguePairings_FormatError_TextPointer14),
  "unplayed phase one uses signed temperature and Original referee name/profile delimiters on home only");
 for(size_t n=0;n<page->rows.size();++n){check(!page->source.rows[n].first_leg_result&&!page->rows[n].first_leg.size(),
  "phase one suppresses every first-leg annotation including played rows");
  if(mixed&&(n==2||n==4))check(!page->rows[n].active&&page->rows[n].details==phase_zero.rows[n].details&&
   page->rows[n].result==phase_zero.rows[n].result,"played/postponed rows retain ordinary details and result/date");}
 const auto& draft=*session.league_pairings_owner();
 const uint16_t expected=mixed?uint16_t(team_average(draft.state,fixtures[4].away,0x1ff).best_half_sum):uint16_t(0xbeef);
 check(staged_pairings_strength(draft)==expected,mixed?"mixed phase-one scratch comes from last ordinary away row, not final unplayed row":
  "all-unplayed phase-one skips strength scratch and preserves previous value");
 const auto accepted=session.league_pairings_checkpoint();
 check(!session.dispatch_league_pairings(tick,revision,error)&&same_pairings_checkpoint(accepted,session.league_pairings_checkpoint())&&stable(),
  "replayed old timed request cannot redraw, mutate scratch or advance a sequence");
 auto back=session.dispatch_league_pairings({page->identity,{LeaguePairingsAction::presentation_redraw,0}},revision,error);
 check(back&&!back->page.source.owner.active_phase&&back->page.rows.front().details==phase_zero.rows.front().details&&stable(),
  "phase-zero redraw restores both ordinary rank/strength details");
 page=back->page;
 check(staged_pairings_strength(*session.league_pairings_owner())==uint16_t(team_average(state,fixtures.back().away,0x1ff).best_half_sum),
  "phase-zero redraw restores final ordinary away strength scratch");
 for(const auto& row:page->source.rows)check(row.first_leg_result.has_value(),"phase-zero redraw restores every first-leg result");
 if(!mixed){
  auto forecast=session.dispatch_league_pairings({page->identity,{LeaguePairingsAction::computer_tip}},revision,error);
  check(forecast&&forecast->page.source.owner.computer_tip_active&&stable(),"active Computer Tip remains staged");page=forecast->page;
  const auto forecast_G=session.league_pairings_owner()->game;const auto forecast_W=session.league_pairings_owner()->rand;
  auto expire=session.dispatch_league_pairings({page->identity,{LeaguePairingsAction::presentation_redraw,1}},revision,error);
  check(expire&&!expire->page.source.owner.computer_tip_active&&expire->page.rows.front().active&&stable()&&
   session.league_pairings_owner()->game.seed()==forecast_G.seed()&&session.league_pairings_owner()->game.calls()==forecast_G.calls()&&
   session.league_pairings_owner()->rand.seed()==forecast_W.seed()&&session.league_pairings_owner()->rand.calls()==forecast_W.calls(),
   "timer redraw expires one-display forecast, returns to Native CC overlay and draws no additional RNG");page=expire->page;
 }
 const auto before_nested=session.league_pairings_checkpoint();
 auto nested=session.dispatch_league_pairings({page->identity,{LeaguePairingsAction::team_info,fixtures[0].home,
  state.club_ids[size_t(fixtures[0].home)]}},revision,error,[&](app::ClubModalServices& services,int,const Image&){
   const auto& offsets=services.state().master_database_metadata;const size_t ref=active.referee_index;
   const size_t name=size_t(offsets[ref*2])|size_t(offsets[ref*2+1])<<8;
   services.state().referee_names[name]^=1;return true;});
 check(!nested&&same_pairings_checkpoint(before_nested,session.league_pairings_checkpoint())&&stable(),
  "nested TeamInfo cannot silently change active packet name ownership");
 auto stale=session.league_pairings_checkpoint();stale.draft->active_packet->rows[0].weather^=1;session.restore_league_pairings_checkpoint(stale);
 const auto stale_before=session.league_pairings_checkpoint();
 check(!session.dispatch_league_pairings({page->identity,{LeaguePairingsAction::presentation_redraw,page->source.owner.active_phase?0:1}},revision,error)&&
  same_pairings_checkpoint(stale_before,session.league_pairings_checkpoint())&&stable(),"forged staged packet rejected without any transaction effect");
 session.restore_league_pairings_checkpoint(before_nested);
 auto changed_driver=session.manager_join_checkpoint();changed_driver.driver.groups[0].desc[0x50]^=1;
 session.restore_manager_join_checkpoint(changed_driver);const auto live_stale=session.league_pairings_checkpoint();
 check(!session.dispatch_league_pairings({page->identity,{LeaguePairingsAction::presentation_redraw,page->source.owner.active_phase?0:1}},revision,error)&&
  same_pairings_checkpoint(live_stale,session.league_pairings_checkpoint()),"changed live descriptor invalidates pending active packet before mutation");
 session.restore_manager_join_checkpoint(setup);
 const auto final=*session.league_pairings_owner();Oracle oracle{final.rand.seed(),final.game.seed(),final.rand.calls(),final.game.calls()};oracle.stir();
 auto close=session.dispatch_league_pairings({page->identity,{LeaguePairingsAction::close}},revision,error);
 check(close&&close->closed&&revision==2&&session.rand().seed()==oracle.w&&session.rand().calls()==oracle.wc&&
  session.game().seed()==oracle.g&&session.game().calls()==oracle.gc&&same_groups(setup.driver.groups,session.manager_join_checkpoint().driver.groups),
  "active family closes with one existing Source W/G stir and no full-group changes");
}
void core(const fs::path& data,const fs::path& input){
 app::CareerSession live(data,731,input);auto g=live.state();const auto exe=bmh::test::load_executable(data/"BMH.exe");const auto constants=ui::league_pairings_constants(exe);
 check(constants.domestic_league_count==5&&constants.calendar_class[0]==0&&constants.calendar_class[3]==2&&constants.calendar_class[4]==3,"EXE five calendar classes");
 g.league_rounds.assign(9,0);g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,0x11);g.memory.set16(::bmh::layout::LeagueTableDisplayModes,0xfabc);
 for(int day=0;day<366;++day)g.memory.set16(::bmh::layout::CompetitionCalendar+uint32_t(day)*2,0);
 g.memory.set16(::bmh::layout::CompetitionCalendar,1);g.memory.set16(::bmh::layout::CompetitionCalendar+14,1);g.memory.set16(::bmh::layout::CompetitionCalendar+730,1);
 auto owner=open_league_pairings_source(g,0,constants);
 check(owner.league==0&&owner.round==0&&owner.title_round==0&&owner.league_group==0,"Source initial manager league and group0");
 check(g.memory.u16(::bmh::layout::LeagueTableDisplayModes)==uint16_t((0xfabc&0xfc0f)|0x1f0)&&g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)==1,"Source opening flags and cleared Tip");
 auto page=league_pairings_view_source(g,owner,constants);
 check(page.rows.size()==9&&page.match_date==(g.season_start&0xffff),"Source first calendar match includes day0");
 for(int k=0;k<9;++k)check(page.rows[size_t(k)].home.runtime_team==k*2&&page.rows[size_t(k)].away.runtime_team==k*2+1,"literal first round fixture order");
 auto strength_control=g;const auto range=squad_range(0);for(size_t n=0;n<range.count;++n)strength_control.squad_slots[range.first+n]=0xfff;
 strength_control.team_morale[0]=25;
 for(int n=0;n<11;++n){strength_control.squad_slots[range.first+size_t(n)]=uint16_t(0x8000|n);auto& p=strength_control.players[size_t(n)];p.w35c=50u|(40u<<7)|(60u<<14);p.w364=uint32_t(n%4);}
 const auto controlled=league_pairings_view_source(strength_control,owner,constants);
 check(controlled.rows[0].home.strength==std::array<int,5>{60,40,50,50,50},"independent eleven-player KO/TE/FO/MOR/overall literal oracle");
 auto incomplete=g;incomplete.results.resize(1);rejects([&]{league_pairings_view_source(incomplete,owner,constants);},"incomplete results rejected");
 incomplete=g;incomplete.memory=StaticMemory{};rejects([&]{open_league_pairings_source(incomplete,0,constants);},"missing Native preferences/calendar rejected");
 auto national=g;national.manager_teams[0]=138;rejects([&]{open_league_pairings_source(national,0,constants);},"national or unowned manager rejected");
 GameRandom G(-1234567);WatcomRandom W(731);const auto native=save_stream(g);const auto gc=G.calls(),wc=W.calls();
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::next},constants,G,W);
 check(league_pairings_view_source(g,owner,constants).match_date==((g.season_start+7)&0xffff),"Source second calendar occurrence");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::previous},constants,G,W);
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::previous},constants,G,W);check(owner.round==33,"34-round previous wrap");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::next},constants,G,W);check(owner.round==0,"34-round next wrap");
 check(G.calls()==gc&&W.calls()==wc&&save_stream(g)==native,"navigation draws no RNG or repeated outer flag initialization");
 for(int raw=0;raw<256;++raw)check(league_pairings_played_source(uint8_t(raw))==((raw&0xcf)<0xcc),"all256 Source classifier equivalence");
 g.results[result_index(0,1,0)]=0xd0;g.results[result_index(0,1,1)]=0xdd;
 for(int k=0;k<40;++k)g.memory.set16(::bmh::layout::Postponed+uint32_t(k)*6,0);
 g.memory.set16(::bmh::layout::Postponed,7);g.memory.set8(::bmh::layout::Postponed+2,0);g.memory.set8(::bmh::layout::Postponed+3,1);g.memory.set8(::bmh::layout::Postponed+4,1);
 g.memory.set16(::bmh::layout::FixturePairingsSecondDescriptor,11);g.memory.set8(::bmh::layout::FixturePairingsSecondDescriptor+2,0);g.memory.set8(::bmh::layout::FixturePairingsSecondDescriptor+3,0);g.memory.set8(::bmh::layout::FixturePairingsSecondDescriptor+4,1);
 g.memory.set16(::bmh::layout::FixturePairingsThirdDescriptor,13);g.memory.set8(::bmh::layout::FixturePairingsThirdDescriptor+2,0);g.memory.set8(::bmh::layout::FixturePairingsThirdDescriptor+3,0);g.memory.set8(::bmh::layout::FixturePairingsThirdDescriptor+4,1);
 page=league_pairings_view_source(g,owner,constants);check(!page.rows[0].postponed_date&&page.rows[1].postponed_date==g.season_start+11,"13:0 played; first matching postponed record only");
 auto display=ui::league_pairings_from_game(g,owner,exe,8,1,0);check(display.rows[0].result=="13:0"&&display.rows[1].result_x==255,"Source result and postponed x255");
 g.league_rounds[0]=9;const size_t rankword=(0*13+2)*2;g.position_history[rankword]=0x24;g.position_history[rankword+1]=0x26;
 page=league_pairings_view_source(g,owner,constants);check(page.rows[0].home.previous_rank==10,"packed2624 round9 raw rank9 displayed10");
 owner.round=17;owner.title_round=17;g.results[result_index(0,1,0)]=0x21;
 display=ui::league_pairings_from_game(g,owner,exe,8,1,0);check(display.rows[0].first_leg.starts_with("1:2"),"return fixture reverses first-leg score");
 g.league_rounds[0]=0;owner.round=0;owner.title_round=0;
 auto regional=owner;regional.league=2;regional.round=0;regional.title_round=0;
 check(ui::league_pairings_from_game(g,regional,exe,8,1,0).title==message_text(g,::bmh::layout::LeaguePairings_FormatError_TextPointer10)+message_text(g,::bmh::layout::FixtureRegionalLeagueLabelPointer)+" A","Original shared Regionalliga A title");
 auto lower=edit_league_pairings_source(g,owner,{LeaguePairingsAction::select_league,5},constants,G,W);
 check(ui::league_pairings_from_game(g,lower,exe,8,1,0).title==message_text(g,::bmh::layout::LeaguePairings_FormatError_TextPointer10)+g.club_name(358),"Original lower league Native country title");
 page=league_pairings_view_source(g,lower,constants);check(page.rows.size()==6&&page.rows[0].home.runtime_team==90&&page.rows[5].away.runtime_team==101,"literal lower league 12-team order");
 lower=edit_league_pairings_source(g,lower,{LeaguePairingsAction::previous},constants,G,W);check(lower.round==21,"22-round previous wrap");
 g.manager_records[0x210]=3;g.memory.set8(::bmh::layout::CalendarState_FormatError_RuntimeField2,0);owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::toggle_group},constants,G,W);check(owner.league==5&&owner.league_group==1,"Source secondary group starts5");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::toggle_group},constants,G,W);check(owner.league==3&&owner.league_group==0,"Source primary group uses manager +210");
 g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,0);owner=open_league_pairings_source(g,0,constants);owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::next},constants,G,W);check(owner.round==0,"Source zero preferences supported; no selected round arrow movement");
 g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,3);owner=open_league_pairings_source(g,0,constants);
 check(!owner.filter_active&&owner.search_first==-1&&league_pairings_view_source(g,owner,constants).rows.size()==9,
  "Source-legal saved low nibble3 retains ordinary pairings ownership");
 g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,4);const auto bad=save_stream(g);rejects([&]{open_league_pairings_source(g,0,constants);},"saved mode4 explicitly unsupported");check(save_stream(g)==bad,"unsupported opening is Native-pure");
 for(int r=0;r<league_matchdays(0);++r)for(size_t n=0;n<league_fixtures(0,r+1).size();++n)g.results[result_index(0,r+1,int(n))]=0xcc;
 g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,8);owner=open_league_pairings_source(g,0,constants);
 check(owner.postponed_active&&owner.postponed_offset==0&&league_pairings_view_source(g,owner,constants).rows.empty(),
  "exact saved low nibble8 opens a valid empty postponed projection");g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,1);
 owner=open_league_pairings_source(g,0,constants);auto stale=owner;stale.club_identity^=1;rejects([&]{league_pairings_view_source(g,stale,constants);},"stale stable identity rejected");
 rejects([&]{edit_league_pairings_source(g,owner,{LeaguePairingsAction::team_info,50,g.club_ids[50]},constants,G,W);},"nonfixture team rejected");
 const auto beforeclose=save_stream(g);Oracle oracle{W.seed(),G.seed(),W.calls(),G.calls()};oracle.stir();owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::close},constants,G,W);
 check(owner.closed&&W.seed()==oracle.w&&G.seed()==oracle.g&&W.calls()==oracle.wc&&G.calls()==oracle.gc,"Source final exit manual signed G/W oracle exactly once");
 check(save_stream(g)==beforeclose,"ordinary final exit preserves other Native bytes");rejects([&]{edit_league_pairings_source(g,owner,{LeaguePairingsAction::close},constants,G,W);},"duplicate final exit rejected");
}

void national_source(const fs::path& data) {
 const auto fixture=fs::path(__FILE__).parent_path()/"fixtures/national_coach_desk/NATWK001.FBM";
 app::CareerSession live(data,731,fixture);auto g=live.state();
 check(g.manager_teams==std::vector<int>{kNoTeam}&&g.club_ids.size()>size_t(kNoTeam),
  "actual saved national desk owns the native 0x165 club identity slot");
 const auto exe=bmh::test::load_executable(data/"BMH.exe");const auto constants=ui::league_pairings_constants(exe);
 g.league_rounds.assign(9,0);g.memory.set8(::bmh::layout::ActiveManager,0);g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,1);
 GameRandom G=live.game();WatcomRandom W=live.rand();
 auto owner=open_league_pairings_source(g,0,constants);
 check(owner.runtime_club==kNoTeam&&owner.club_identity==g.club_ids.at(size_t(kNoTeam))&&
  owner.league==0&&owner.round==0,"Source national entry maps active 0x165 to league 0 and its actual Native club_ids identity");
 auto page=league_pairings_view_source(g,owner,constants);
 const auto native_after_open=save_stream(g);
 check(page.rows.size()==9&&page.rows.front().home.runtime_team!=kNoTeam&&
  page.rows.front().away.runtime_team!=kNoTeam,"national manager sees ordinary league-zero fixture rows only");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::next},constants,G,W);
 check(owner.runtime_club==kNoTeam&&owner.league==0&&owner.round==1,
  "national owner can navigate the original league-zero round");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::select_league,5},constants,G,W);
 check(owner.runtime_club==kNoTeam&&owner.league==5&&league_pairings_view_source(g,owner,constants).rows.size()==6,
  "national owner may select another Original league and project its six fixtures");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::select_league,0},constants,G,W);
 check(owner.runtime_club==kNoTeam&&owner.league==0&&save_stream(g)==native_after_open&&
  G.seed()==live.game().seed()&&G.calls()==live.game().calls()&&
  W.seed()==live.rand().seed()&&W.calls()==live.rand().calls(),
  "national navigation preserves Native state and both Source generators");
 auto invalid=g;invalid.manager_teams[0]=138;
 rejects([&]{open_league_pairings_source(invalid,0,constants);},"invalid managed runtime team 138 remains rejected");
 invalid=g;invalid.manager_teams[0]=166;
 rejects([&]{open_league_pairings_source(invalid,0,constants);},"unowned runtime team 166 remains rejected");
}

void national_app(const fs::path& data) {
 const auto fixture=fs::path(__FILE__).parent_path()/"fixtures/national_coach_desk/NATWK001.FBM";
 app::CareerSession session(data,731,fixture);const auto exe=bmh::test::load_executable(data/"BMH.exe");
 uint64_t revision=1;std::string error;
 const auto native=save_stream(session.state());const auto runtime=session.club_modal_runtime_checkpoint();
 const uint8_t preferences_before=session.state().memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField);
 const auto live_game=session.game();const auto live_rand=session.rand();
 const auto unchanged=[&]{return save_stream(session.state())==native&&
  session.club_modal_runtime_checkpoint()==runtime&&session.game().seed()==live_game.seed()&&
  session.game().calls()==live_game.calls()&&session.rand().seed()==live_rand.seed()&&
  session.rand().calls()==live_rand.calls()&&revision==1;};
 const ui::DeskRequest origin{ui::DeskRequestKind::open_menu,0,0,1,0,revision};
 auto page=session.open_league_pairings(origin,revision,error);
 check(page.has_value()&&error.empty()&&page->identity.runtime_club==kNoTeam&&
  page->identity.league==0&&page->source.rows.size()==9&&unchanged(),
  "actual loaded no-fixture national desk opens league-zero pairings as a staged view");
 auto dispatch=[&](ui::LeaguePairingsState& current,LeaguePairingsEvent event) {
  const auto result=session.dispatch_league_pairings({current.identity,event},revision,error);
  check(result.has_value()&&error.empty()&&!result->closed&&unchanged(),
   "national pairings navigation stays staged and leaves live Native/W/G untouched");
  current=result->page;
 };
 dispatch(*page,{LeaguePairingsAction::next});
 dispatch(*page,{LeaguePairingsAction::select_league,5});
 check(page->identity.runtime_club==kNoTeam&&page->identity.league==5&&page->source.rows.size()==6,
  "actual national session navigates from its Source league-zero default to league five");
 dispatch(*page,{LeaguePairingsAction::select_league,0});
 const auto staged=session.league_pairings_checkpoint();
 check(staged.draft&&staged.draft->owner.runtime_club==kNoTeam&&
  staged.draft->owner.league==0&&staged.draft->state.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)==uint8_t(preferences_before&0xef),
  "national staged owner returns to the initial Original league without mutating live preferences");
 Oracle expected{staged.draft->rand.seed(),staged.draft->game.seed(),
  staged.draft->rand.calls(),staged.draft->game.calls()};expected.stir();
 auto expected_state=staged.draft->state;auto expected_owner=staged.draft->owner;
 auto expected_game=staged.draft->game;auto expected_rand=staged.draft->rand;
 (void)edit_league_pairings_source(expected_state,expected_owner,{LeaguePairingsAction::close},
  ui::league_pairings_constants(exe),expected_game,expected_rand);
 const auto close=session.dispatch_league_pairings({page->identity,{LeaguePairingsAction::close}},revision,error);
 check(close&&close->closed&&revision==2&&error.empty()&&
  save_stream(session.state())==save_stream(expected_state),
  "actual national close commits the staged Native preference writes once");
 check(session.rand().seed()==expected.w&&session.rand().calls()==expected.wc&&
  session.game().seed()==expected.g&&session.game().calls()==expected.gc,
  "actual national Source close performs exactly the final W/G stir from staged generators");
}

void computer_tip_core(const fs::path& data,const fs::path& input) {
 app::CareerSession live(data,119,input);auto g=live.state();g.league_rounds.assign(9,0);
 const auto exe=bmh::test::load_executable(data/"BMH.exe");const auto constants=ui::league_pairings_constants(exe);
 const std::array<uint8_t,10> gap_table{0,0,1,1,1,1,2,2,2,3};
 check(constants.computer_tip_gap==gap_table,"Original a454d KI-Tipp gap table");
 g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,1);g.memory.set32(::bmh::layout::CurrentMatchday,100);
 for(int row=0;row<9;++row) {
  const auto fixture=league_fixtures(0,1).at(size_t(row));
  for(int team:{fixture.home,fixture.away}) {
   const auto range=squad_range(team);check(range.count>=11,"literal KI-Tipp test squad capacity");
   for(int n=0;n<11;++n) {
    g.squad_slots[range.first+size_t(n)]=uint16_t(0x8000|n);
    auto& p=g.players.at(size_t(n));p.w35c=5u|(20u<<7)|(20u<<14);p.w364=1;
   }
  }
  g.results[result_index(0,1,row)]=0xcc;
 }
 const size_t base=result_index(0,1,0);g.results[base+9]=0x7b; // Source ten-byte local backup padding.
 const auto original_results=g.results;
 std::array<uint8_t,240> queue_before{};
 for(size_t n=0;n<queue_before.size();++n)queue_before[n]=g.memory.u8(::bmh::layout::Postponed+uint32_t(n));
 GameRandom G(999);WatcomRandom W(321);(void)G.range(0,1);const auto calls_before=G.calls();
 const auto W_before=W;auto owner=open_league_pairings_source(g,0,constants);
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::search},constants,G,W);
 const int first_before=owner.search_first,second_before=owner.search_second;
 const auto w_seed=W.seed();const auto w_calls=W.calls();
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::computer_tip,4},constants,G,W);
 const std::array<uint8_t,9> expected{0,16,1,1,1,16,0,16,17};
 check(owner.computer_tip_active&&owner.computer_tip_league==0&&owner.computer_tip_round==0&&
  owner.search_first==first_before&&owner.search_second==second_before&&
  owner.computer_tip_original[9]==0x7b,"KI-Tipp retains search locals and original 10-byte padding");
 for(size_t n=0;n<expected.size();++n)
  check(owner.computer_tip_results[n].predicted_result==expected[n],"independent nine-pair KI-Tipp literal scores");
 check(G.seed()==41239&&G.calls()==calls_before+27,"KI-Tipp reset seed100, 27 G draws and final seed41239");
 check(W.seed()==w_seed&&W.calls()==w_calls&&g.results==original_results,
  "KI-Tipp preserves W and Native result/round-padding bytes");
 for(size_t n=0;n<queue_before.size();++n)
  check(g.memory.u8(::bmh::layout::Postponed+uint32_t(n))==queue_before[n],"KI-Tipp preserves postponed queue bytes");
 check(g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)==0x12,"KI-Tipp sets only the one-redraw Source bit over Search mode");
 const auto page=ui::league_pairings_from_game(g,owner,exe,7,9,2);
 check(page.source.rows[0].raw_result==0xcc&&page.source.rows[0].computer_tip_result==0&&
  page.rows[0].result=="0:0"&&page.buttons[4]==message_text(g,::bmh::layout::FixturePairingsFifthButtonText),
  "KI-Tipp forecast overlays the original result cell and retains Original button label");
 ui::LeaguePairingsScreen screen(data,page);const auto json=screen.state_json();
 check(json.find("\"computer_tip_active\":true")!=std::string::npos&&
  json.find("\"computer_tip_result\":0")!=std::string::npos,
  "KI-Tipp projection exposes transient typed score, not Native result mutation");

 const auto forecast_game=g;const auto forecast_owner=owner;
 const std::array<LeaguePairingsEvent,6> invalidators{{
  {LeaguePairingsAction::reset},{LeaguePairingsAction::previous},{LeaguePairingsAction::next},
  {LeaguePairingsAction::search},
  {LeaguePairingsAction::search_team,forecast_owner.computer_tip_results.front().home_runtime_team,
   forecast_owner.computer_tip_results.front().home_club_identity},
  {LeaguePairingsAction::select_league,5}}};
 for(const auto& event:invalidators) {
  auto state=forecast_game;auto current=forecast_owner;GameRandom draw(999);WatcomRandom watcom(321);
  const auto results_before=state.results;const auto g_seed=draw.seed();const auto g_calls=draw.calls();
  const auto w_seed=watcom.seed();const auto w_calls=watcom.calls();
  current=edit_league_pairings_source(state,current,event,constants,draw,watcom);
  check(!current.computer_tip_active&&current.computer_tip_results.empty()&&
   (state.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)&0x10)==0&&state.results==results_before&&
   draw.seed()==g_seed&&draw.calls()==g_calls&&watcom.seed()==w_seed&&watcom.calls()==w_calls,
   "every ordinary redraw/selection invalidates the transient forecast without Native/RNG effects");
 }
 auto ordinary=forecast_game;ordinary.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,1);
 auto ordinary_owner=open_league_pairings_source(ordinary,0,constants);GameRandom ordinary_g(77);WatcomRandom ordinary_w(88);
 ordinary_owner=edit_league_pairings_source(ordinary,ordinary_owner,
  {LeaguePairingsAction::computer_tip,4},constants,ordinary_g,ordinary_w);
 const auto ordinary_results=ordinary.results;const auto ordinary_g_seed=ordinary_g.seed();
 const auto ordinary_g_calls=ordinary_g.calls();const auto ordinary_w_seed=ordinary_w.seed();
 const auto ordinary_w_calls=ordinary_w.calls();
 ordinary_owner=edit_league_pairings_source(ordinary,ordinary_owner,
  {LeaguePairingsAction::team_info,ordinary_owner.computer_tip_results.front().home_runtime_team,
   ordinary_owner.computer_tip_results.front().home_club_identity},constants,ordinary_g,ordinary_w);
 check(!ordinary_owner.computer_tip_active&&ordinary_owner.computer_tip_results.empty()&&
  (ordinary.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)&0x10)==0&&ordinary.results==ordinary_results&&
  ordinary_g.seed()==ordinary_g_seed&&ordinary_g.calls()==ordinary_g_calls&&
  ordinary_w.seed()==ordinary_w_seed&&ordinary_w.calls()==ordinary_w_calls,
  "ordinary fixture TeamInfo redraw invalidates a forecast without Native/RNG effects");

 const auto first_backup=owner.computer_tip_original;const auto calls_after_first=G.calls();
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::computer_tip,4},constants,G,W);
 check(owner.computer_tip_original==first_backup&&G.seed()==41239&&G.calls()==calls_after_first+27&&
  owner.computer_tip_results[8].predicted_result==17,
  "repeated KI-Tipp reuses first backup, resets G seed without resetting cumulative calls");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::next},constants,G,W);
 check(!owner.computer_tip_active&&owner.computer_tip_results.empty()&&
  (g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)&0x10)==0,"round redraw invalidates forecast and clears temporary Source bit");

 // A six-fixture league exercises the literal 6-row source path.
 auto six=live.state();six.league_rounds.assign(9,0);six.manager_teams[0]=90;
 six.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,1);six.memory.set32(::bmh::layout::CurrentMatchday,100);
 for(int row=0;row<6;++row) {
  const auto fixture=league_fixtures(5,1).at(size_t(row));
  for(int team:{fixture.home,fixture.away}) {const auto range=squad_range(team);
   for(int n=0;n<11;++n) {six.squad_slots[range.first+size_t(n)]=uint16_t(0x8000|n);
    auto& p=six.players.at(size_t(n));p.w35c=5u|(20u<<7)|(20u<<14);p.w364=1;}}
  six.results[result_index(5,1,row)]=0xcc;
 }
 GameRandom G6(8);WatcomRandom W6(9);auto owner6=open_league_pairings_source(six,0,constants);
 owner6=edit_league_pairings_source(six,owner6,{LeaguePairingsAction::computer_tip,4},constants,G6,W6);
 for(size_t n=0;n<6;++n)check(owner6.computer_tip_results[n].predicted_result==expected[n],
  "independent six-pair KI-Tipp literal scores");
 check(G6.calls()==18&&G6.seed()==2082071,"six-row KI-Tipp uses exactly eighteen G draws");

 // Independent Source MATH-ORACLES fixtures exercise the asymmetric strength
 // branch, including its byte-negated away table index. Ratings are literal
 // synthetic players and do not claim a generated career schedule.
 auto math_case=[&](bool home_stronger,uint8_t expected_first) {
  auto state=g;state.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,1);state.memory.set32(::bmh::layout::CurrentMatchday,100);
  const auto fixture=league_fixtures(0,1).front();
  auto assign=[&](int team,int primary,int secondary) {
   const auto range=squad_range(team);check(range.count>=11,"math oracle squad capacity");
   const size_t base=team==fixture.home?0u:11u;check(state.players.size()>=base+11,"math oracle player records");
   for(int n=0;n<11;++n) {
    const int form=n==10?secondary-10*(secondary/11):secondary/11;
    const int tech_condition=primary/11;
    state.players[base+size_t(n)]=Player{"",uint32_t(form)|(uint32_t(tech_condition)<<7)|
      (uint32_t(tech_condition)<<14),0,1};
    state.squad_slots[range.first+size_t(n)]=uint16_t(0x8000|uint16_t(base+size_t(n)));
   }
  };
  if(home_stronger) {assign(fixture.home,330,90);assign(fixture.away,110,55);}
  else {assign(fixture.home,110,55);assign(fixture.away,330,90);}
  GameRandom test_game(77);WatcomRandom test_w(88);auto test_owner=open_league_pairings_source(state,0,constants);
  test_owner=edit_league_pairings_source(state,test_owner,{LeaguePairingsAction::computer_tip,4},constants,test_game,test_w);
  check(test_owner.computer_tip_results.front().predicted_result==expected_first,
   "literal one-pair KI-Tipp home/away strength-branch oracle");
 };
 math_case(true,0x11);math_case(false,0x00);

 // Ineligible Mode4, Mode8, mixed-result, past-round and signed-seed cases
 // reject before changing Native state or either RNG.
 auto rejected_tip=[&](GameState state,LeaguePairingsOwner current,const char* why) {
  GameRandom test_game(4321);WatcomRandom test_w(765);const auto gs=test_game.seed();
  const auto gc=test_game.calls();const auto ws=test_w.seed();const auto wc=test_w.calls();const auto native=save_stream(state);
  rejects([&]{edit_league_pairings_source(state,current,{LeaguePairingsAction::computer_tip,4},constants,test_game,test_w);},why);
  check(save_stream(state)==native&&test_game.seed()==gs&&test_game.calls()==gc&&
   test_w.seed()==ws&&test_w.calls()==wc,"rejected KI-Tipp preserves full staged Native/W/G");
 };
 auto mode4=live.state();mode4.league_rounds.assign(9,0);mode4.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,1);
 auto o4=open_league_pairings_source(mode4,0,constants);GameRandom g4(1);WatcomRandom w4(1);
 o4=edit_league_pairings_source(mode4,o4,{LeaguePairingsAction::filter_by_club},constants,g4,w4);
 rejected_tip(mode4,o4,"Mode4 KI-Tipp rejected");
 auto mode8=live.state();mode8.league_rounds.assign(9,0);mode8.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,8);
 auto o8=open_league_pairings_source(mode8,0,constants);rejected_tip(mode8,o8,"Mode8 KI-Tipp rejected");
 auto mixed=live.state();mixed.league_rounds.assign(9,0);mixed.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,1);
 mixed.results[result_index(0,1,0)]=0x21;auto om=open_league_pairings_source(mixed,0,constants);
 rejected_tip(mixed,om,"mixed/non-cc KI-Tipp rejected");
 auto past=live.state();past.league_rounds.assign(9,1);past.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,1);
 auto op=open_league_pairings_source(past,0,constants);GameRandom gp(1);WatcomRandom wp(1);
 op=edit_league_pairings_source(past,op,{LeaguePairingsAction::previous},constants,gp,wp);
 rejected_tip(past,op,"past-round KI-Tipp rejected");
 auto signed_seed=live.state();signed_seed.league_rounds.assign(9,0);signed_seed.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,1);
 signed_seed.memory.set32(::bmh::layout::CurrentMatchday,0xffffffffu);auto os=open_league_pairings_source(signed_seed,0,constants);
 rejected_tip(signed_seed,os,"unqualified signed Native KI-Tipp seed rejected");
 (void)W_before;
}

void postponed_core(const fs::path& data,const fs::path& input) {
 app::CareerSession live(data,119,input);auto g=live.state();g.league_rounds.assign(9,0);
 const auto exe=bmh::test::load_executable(data/"BMH.exe");const auto constants=ui::league_pairings_constants(exe);
 g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,0x18);for(int r=0;r<34;++r)for(size_t n=0;n<league_fixtures(0,r+1).size();++n)
  g.results[result_index(0,r+1,int(n))]=0xcc;
 for(int k=0;k<40;++k)for(int b=0;b<6;++b)g.memory.set8(::bmh::layout::Postponed+uint32_t(k)*6+uint32_t(b),0);
 auto owner=open_league_pairings_source(g,0,constants);GameRandom G(-7331);WatcomRandom W(119);
 check(owner.postponed_active&&owner.postponed_offset==0&&(g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)&15)==8,
  "saved Mode8 opens with isolated offset zero owner");
 const std::array<uint8_t,12> qualifying{0xcd,0xce,0xcf,0xdd,0xde,0xdf,0xed,0xee,0xef,0xfd,0xfe,0xff};
 for(int raw=0;raw<256;++raw)check(league_pairings_postponed_source(uint8_t(raw))==
   (std::find(qualifying.begin(),qualifying.end(),uint8_t(raw))!=qualifying.end()),
   "all256 literal postponed predicate bytes");
 auto fixtures_for=[&](int count) {
  for(int r=0;r<34;++r)for(size_t n=0;n<league_fixtures(0,r+1).size();++n)
   g.results[result_index(0,r+1,int(n))]=0xcc;
  int written=0;
  for(int r=0;r<34&&written<count;++r) {const auto fs=league_fixtures(0,r+1);for(size_t n=0;n<fs.size()&&written<count;++n) {
    g.results[result_index(0,r+1,int(n))]=0xcd;++written;}}
 };
 auto page=league_pairings_view_source(g,owner,constants);
 check(page.rows.empty()&&page.total_rows==0&&page.match_date==0xffff,"zero postponed matches are valid and have no Source header date");
 for(int count:{1,9,10,11,20,21}) {
  fixtures_for(count);owner.postponed_offset=0;page=league_pairings_view_source(g,owner,constants);
  check(page.total_rows==count&&page.rows.size()==std::min(count,10),"Source-order postponed scan count and first page");
  int seen=0;for(int round=0;round<34;++round)for(size_t slot=0;slot<league_fixtures(0,round+1).size();++slot)
   if(league_pairings_postponed_source(g.results[result_index(0,round+1,int(slot))])) {
    if(seen<10)check(page.rows[size_t(seen)].source_round==round&&page.rows[size_t(seen)].fixture_index==int(slot),
       "rows retain actual ascending Native round and fixture slot");++seen;}
  if(count>10) {owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::next},constants,G,W);
   page=league_pairings_view_source(g,owner,constants);check(owner.postponed_offset==10&&page.rows.size()==std::min(10,count-10),
    "Mode8 right arrow projects bounded second source page");
   if(count>=20){owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::next},constants,G,W);
    page=league_pairings_view_source(g,owner,constants);
    check(owner.postponed_offset==(count==20?10:20)&&page.rows.size()==(count==20?10:1),
     "Mode8 full-page tail is bounded without Source non-progress loop");}
   owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::previous},constants,G,W);
   if(count>=20&&count==21){check(owner.postponed_offset==10,"Mode8 partial tail returns to retained full page");
    owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::previous},constants,G,W);}
   check(owner.postponed_offset==0,"Mode8 previous returns to first page");}
  else if(count==10) {const auto before=save_stream(g);const auto saved=owner;
   rejects([&]{edit_league_pairings_source(g,owner,{LeaguePairingsAction::next},constants,G,W);},
    "exact-ten non-progress edge rejects as explicit bounded deviation");
   check(save_stream(g)==before&&owner==saved&&G.calls()==0&&W.calls()==0,
    "exact-ten rejection preserves Native/owner/RNG");}
 }
 fixtures_for(255);owner.postponed_offset=250;page=league_pairings_view_source(g,owner,constants);
 check(page.total_rows==255&&page.page_offset==250&&page.rows.size()==5,
  "Source byte counter maximum255 supports its reachable final partial window");
 fixtures_for(256);owner.postponed_offset=0;
 const auto overflow_native=save_stream(g);const auto overflow_owner=owner;
 rejects([&]{league_pairings_view_source(g,owner,constants);},"256 qualifying rows exceed Source byte-counter boundary");
 check(save_stream(g)==overflow_native&&owner==overflow_owner&&G.calls()==0&&W.calls()==0,
  "overflow boundary rejects without Native/owner/RNG changes");
 fixtures_for(0);owner.postponed_offset=10;const auto empty_native=save_stream(g);const auto empty_owner=owner;
 rejects([&]{league_pairings_view_source(g,owner,constants);},"empty Mode8 cannot own offset10");
 check(save_stream(g)==empty_native&&owner==empty_owner,"stale empty offset rejection is pure");
 fixtures_for(1);owner.postponed_offset=10;const auto one_native=save_stream(g);const auto one_owner=owner;
 rejects([&]{league_pairings_view_source(g,owner,constants);},"one-result Mode8 cannot own offset10");
 check(save_stream(g)==one_native&&owner==one_owner,"stale one-row offset rejection is pure");
 owner.postponed_offset=std::numeric_limits<int>::max()/10*10;
 rejects([&]{league_pairings_view_source(g,owner,constants);},"max aligned stale offset is rejected before projection");owner.postponed_offset=0;
 // The first duplicate matching nonzero queue entry wins; day zero is skipped.
 fixtures_for(1);g.results[result_index(0,1,1)]=0xce;
 int target_round=-1,target_slot=-1;for(int r=0;r<34&&target_round<0;++r)for(size_t n=0;n<league_fixtures(0,r+1).size();++n)
  if(league_pairings_postponed_source(g.results[result_index(0,r+1,int(n))])){target_round=r;target_slot=int(n);break;}
 check(target_round==0&&target_slot==0,"literal first source fixture selected for postponed queue fixture");
 auto putq=[&](int k,uint16_t day,int r,int league,int slot){const uint32_t at=::bmh::layout::Postponed+uint32_t(k)*6;
  g.memory.set16(at,day);g.memory.set8(at+2,uint8_t(r));g.memory.set8(at+3,uint8_t(league));g.memory.set8(at+4,uint8_t(slot));g.memory.set8(at+5,0xa5);};
 putq(0,0,target_round,0,target_slot);putq(1,17,target_round,0,target_slot);putq(2,23,target_round,0,target_slot);
 putq(38,9,target_round,0,2);putq(39,31,target_round,0,target_slot);
 owner.postponed_offset=0;page=league_pairings_view_source(g,owner,constants);
 check(page.rows.size()==2&&page.rows[0].postponed_queue_slot==1&&page.rows[0].postponed_day==17&&
  page.rows[0].postponed_date==g.season_start+17&&!page.rows[1].postponed_date&&
  !page.rows[1].postponed_queue_slot,"queue skips zero, takes first nonzero duplicate, scans through slot39, and keeps orphan rows date-blank; queue-only record creates no row");
 auto display=ui::league_pairings_from_game(g,owner,exe,1,1,0);
 check(display.title==message_text(g,::bmh::layout::LeaguePairings_FormatError_TextPointer12)+league_name(g,0)&&display.subtitle.empty()&&display.date.empty()&&
  display.buttons[3]==message_text(g,::bmh::layout::FixturePairingsFourthButtonText)&&display.rows[0].result_x==255&&!display.rows[0].result.empty()&&
  display.rows[0].scheduled_date.empty()&&display.rows[0].scheduled_ordinal.empty()&&display.rows[0].first_leg.empty()&&
  display.rows[1].result.empty()&&display.rows[1].result_x==265,
  "Mode8 Original title/button/date projection omits Mode4 fields");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::select_league,5},constants,G,W);
 check(!owner.postponed_active&&owner.postponed_offset==0&&(g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)&0x0f)==1&&owner.league==5,
  "actual league selection clears Mode8 and restores ordinary mode");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::reset},constants,G,W);
 check(!owner.postponed_active&&(g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)&0x0f)==1,"Mode8 reset clears owned window");
}
struct Context {
 fs::path data,dir;ui::StartupFlow flow;std::unique_ptr<app::CareerSession> session;uint64_t rev=0;openbmh::audio::Playback audio;std::optional<uint64_t> audio_session;
 Context(const fs::path& d,const fs::path& input,bool postponed=false,bool postponed_row=false,bool single_postponed_row=false,bool computer_tip=false):data(d),dir(fs::temp_directory_path()/("pairings-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),flow(d,dir){
 fs::create_directories(dir);auto save=load_savegame(input);auto g=GameState::from_save(SaveState(save.stream));
 // Declared nonlive final-round control on the canonical two-manager career.
 g.league_rounds[0]=34;g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,postponed?8:1);
 if(postponed){const int league=team_league(g.manager_teams[0]);for(int r=0;r<league_matchdays(league);++r)
   for(size_t n=0;n<league_fixtures(league,r+1).size();++n)
    g.results[result_index(league,r+1,int(n))]=0xcc;
  for(int k=0;k<40;++k)g.memory.set16(::bmh::layout::Postponed+uint32_t(k)*6,0);
  if(postponed_row){auto qput=[&](int qslot,uint16_t day,int fixture){const uint32_t q=::bmh::layout::Postponed+uint32_t(qslot)*6;
    g.memory.set16(q,day);g.memory.set8(q+2,0);g.memory.set8(q+3,uint8_t(league));g.memory.set8(q+4,uint8_t(fixture));};
   if(single_postponed_row){g.results[result_index(league,1,0)]=0xcd;qput(39,17,0);}
   else {for(int slot=0;slot<3;++slot)g.results[result_index(league,1,slot)]=uint8_t(0xcd+slot);
    qput(37,19,3);qput(38,17,0);qput(39,18,1);}}}
 if(computer_tip){const int league=team_league(g.manager_teams[0]);const int round=0;
  g.league_rounds.at(size_t(league))=uint8_t(round);g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,1);g.memory.set32(::bmh::layout::CurrentMatchday,100);
  for(size_t n=0;n<league_fixtures(league,round+1).size();++n){const auto f=league_fixtures(league,round+1)[n];
   g.results[result_index(league,round+1,int(n))]=0xcc;
   for(int team:{f.home,f.away}){const auto range=squad_range(team);
    for(int k=0;k<11;++k){g.squad_slots[range.first+size_t(k)]=uint16_t(0x8000|k);
     auto& player=g.players.at(size_t(k));player.w35c=5u|(20u<<7)|(20u<<14);player.w364=1;}}}}
 WatcomRandom encryption(1);write_savegame(dir/"AUTOSAVE.FBM",make_savegame(g,encryption));
 audio.set_muted(true);flow.click(214,215);check(serve(flow.send_key("ENTER")).empty()&&session&&rev==1,"controller Native load");
 if(postponed){auto services=session->live_club_modal_services(rev);services.set_runtime_byte(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField4,0x77);services.set_runtime_byte(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField5,0x77);
  services.set_runtime_byte(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField6,0x66);services.set_runtime_byte(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField2,0x55);services.set_runtime_byte(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField3,0x44);}
 check(serve(flow.send_key("L")).empty()&&flow.screen_name()=="league_pairings"&&session->league_pairings_owner(),"actual Desk L opens staged Source page");}
 ~Context(){std::error_code ec;fs::remove_all(dir,ec);}
 std::string serve(const ui::FlowResult& request,app::TransferOfferTeamInfoPresenter team={}){return app::serve_flow_request(flow,request,session,data,dir,1,rev,audio,audio_session,false,{},{},{},{},{},{},{},{},{},app::PendingOfferEntryPolicy::legacy_unpresented,{},app::HumanBuyerMarketEntryPolicy::legacy_unpresented,std::move(team));}
};

void computer_tip_app_ui(const fs::path& data,const fs::path& input) {
 Context c(data,input,false,false,false,true);const auto live_native=save_stream(c.session->state());
 const auto original_results=c.session->state().results;const uint32_t original_source_seed=c.session->state().memory.u32(::bmh::layout::CurrentMatchday);
 const auto live_runtime=c.session->club_modal_runtime_checkpoint();const auto live_W=c.session->rand();const auto live_G=c.session->game();
 auto stable=[&]{return save_stream(c.session->state())==live_native&&
  c.session->club_modal_runtime_checkpoint()==live_runtime&&c.session->rand().seed()==live_W.seed()&&
  c.session->rand().calls()==live_W.calls()&&c.session->game().seed()==live_G.seed()&&
  c.session->game().calls()==live_G.calls()&&c.rev==1;};

 // Force a staged arithmetic exception after the genuine physical control is
 // translated; App must retain the complete staged checkpoint and live state.
 const auto before_down=c.flow.render().pixels;
 auto physical=c.flow.pointer_down(75,180);check(physical.request==ui::FlowRequest::none&&physical.changed&&
  c.flow.render().pixels!=before_down&&stable(),
  "Original KI-Tipp DOWN only holds the existing button and leaves live state untouched");
 auto request=c.flow.click(75,180);check(request.league_pairings_action.event.kind==LeaguePairingsAction::computer_tip&&stable(),
  "KI-Tipp activates on paired release only and leaves live session untouched");
 auto good_checkpoint=c.session->league_pairings_checkpoint();auto broken=good_checkpoint;
 auto& draft=*broken.draft;const auto f=league_fixtures(draft.owner.league,draft.owner.round+1).front();
 const auto squad=squad_range(f.home);draft.state.squad_slots[squad.first]=0x8fff;
 c.session->restore_league_pairings_checkpoint(broken);const auto broken_checkpoint=c.session->league_pairings_checkpoint();
 check(!c.serve(request).empty()&&stable()&&c.flow.screen_name()=="league_pairings"&&
  !c.flow.pending_league_pairings(),"KI-Tipp exception rejects without live revision/RNG and clears the pending request");
 const auto after_failure=c.session->league_pairings_checkpoint();
 check(same_pairings_checkpoint(after_failure,broken_checkpoint),
  "KI-Tipp exception preserves complete live/draft Native/runtime/owner/identity/original-guard/W/G checkpoint");

 c.session->restore_league_pairings_checkpoint(good_checkpoint);
 physical=c.flow.pointer_down(75,180);request=c.flow.click(75,180);
 check(c.serve(request).empty()&&stable(),"successful KI-Tipp remains a fully staged action");
 const auto& forecast=c.flow.league_pairings()->state();
 check(forecast.source.owner.computer_tip_active&&forecast.identity.league==0&&forecast.source.rows.size()==9&&
  (forecast.source.preferences&0x10)!=0&&forecast.identity.sequence==1,
  "App acknowledges typed forecast with transient button state and scoped sequence");
 const auto staged=c.session->league_pairings_checkpoint();
 const auto draft_byte=[](const app::CareerSession::LeaguePairingsDraft& draft,uint32_t address) {
  if(draft.state.memory.has(address))return draft.state.memory.u8(address);
  const auto at=draft.runtime.find(address);if(at==draft.runtime.end())throw std::runtime_error("missing Source scratch byte");
  return at->second;
 };
 const auto& accepted_draft=*staged.draft;
 const size_t original_at=result_index(forecast.identity.league,forecast.identity.round+1,0);
 bool backup_matches=true;for(size_t n=0;n<accepted_draft.owner.computer_tip_original.size();++n)
  backup_matches&=accepted_draft.owner.computer_tip_original[n]==original_results.at(original_at+n);
 check(staged.draft->state.results==original_results&&staged.draft->state.memory.u32(::bmh::layout::CurrentMatchday)==original_source_seed&&
  (staged.draft->state.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)&0x10)==0&&staged.draft->owner.computer_tip_active&&
  staged.draft->game.seed()==41239&&staged.draft->game.calls()==live_G.calls()+27&&
  staged.draft->rand.seed()==live_W.seed()&&staged.draft->rand.calls()==live_W.calls()&&
  accepted_draft.original_stream==live_native&&accepted_draft.original_runtime==live_runtime&&
  accepted_draft.original_game_seed==live_G.seed()&&accepted_draft.original_game_calls==live_G.calls()&&
  accepted_draft.original_rand_seed==live_W.seed()&&accepted_draft.original_rand_calls==live_W.calls()&&
  accepted_draft.identity.scope==good_checkpoint.draft->identity.scope&&accepted_draft.identity.sequence==1&&
  backup_matches&&draft_byte(accepted_draft,::bmh::layout::CareerSession_FormatError_ExecutableField3)==20&&draft_byte(accepted_draft,::bmh::layout::CareerSession_FormatError_ExecutableField4)==0,
  "forecast commits only scoped G(100,+27), retains W/Native/guards, clears the transient bit, backs up ten bytes, and redraws final strength20");
 const std::array<uint32_t,6> date_scratch{::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField,::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField4,::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField5,::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField6,::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField2,::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField3};
 for(uint32_t address:date_scratch)
  check(draft_byte(accepted_draft,address)==draft_byte(*good_checkpoint.draft,address),
   "KI-Tipp redraw retains the header date scratch from the existing App date path");
 check(forecast.source.match_date!=0xffff,"KI-Tipp synthetic App fixture has a real league header date");
 const auto header=ui::desk_date(forecast.source.match_date,bmh::test::load_executable(data/"BMH.exe"));
 check(draft_byte(accepted_draft,::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField4)==header.day&&draft_byte(accepted_draft,::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField5)==header.month&&
  draft_byte(accepted_draft,::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField6)==header.weekday&&draft_byte(accepted_draft,::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField2)==uint8_t(header.year)&&
  draft_byte(accepted_draft,::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField3)==uint8_t(header.year>>8),
  "KI-Tipp App projection retains the literal Source header-date fields");
 const auto stale_request=request.league_pairings_action;const auto accepted_checkpoint=c.session->league_pairings_checkpoint();
 std::string error;check(!c.session->dispatch_league_pairings(stale_request,c.rev,error)&&!error.empty()&&stable()&&
  c.flow.screen_name()=="league_pairings"&&!c.flow.pending_league_pairings(),
  "stale repeated KI-Tipp request is rejected without changing live or accepted staged state");
 check(same_pairings_checkpoint(c.session->league_pairings_checkpoint(),accepted_checkpoint),
  "stale KI-Tipp preserves the complete accepted Native/runtime/owner/identity/original-guard/W/G checkpoint");

 auto coherent=c.session->league_pairings_checkpoint();auto failed=coherent;++failed.draft->state.date;
 c.session->restore_league_pairings_checkpoint(failed);const auto failed_before=c.session->league_pairings_checkpoint();
 const uint64_t revision_before_close=c.rev;auto failed_close=c.flow.pointer_down(200,100,true);
 check(!c.serve(failed_close).empty()&&c.rev==revision_before_close&&c.flow.screen_name()=="league_pairings"&&
  !c.flow.pending_league_pairings()&&same_pairings_checkpoint(c.session->league_pairings_checkpoint(),failed_before),
  "active KI forecast failed close-preparation ACK restores the complete staged checkpoint and revision");
 c.session->restore_league_pairings_checkpoint(coherent);

 const auto before_close=*c.session->league_pairings_owner();Oracle close_oracle{
  before_close.rand.seed(),before_close.game.seed(),before_close.rand.calls(),before_close.game.calls()};
 close_oracle.stir();
 const auto close_request=c.flow.pointer_down(200,100,true);
 check(c.serve(close_request).empty()&&c.flow.screen_name()=="desktop"&&c.rev==2&&
  !c.session->league_pairings_owner(),"right-DOWN closes KI-Tipp family once and commits one revision");
 check(c.session->game().seed()==close_oracle.g&&c.session->game().calls()==close_oracle.gc&&
  c.session->rand().seed()==close_oracle.w&&c.session->rand().calls()==close_oracle.wc,
  "final close stir runs exactly once after staged Computer Tip G draws");
 check(save_stream(c.session->state())==save_stream(coherent.draft->state)&&c.session->state().results==original_results&&
  c.session->state().memory.u32(::bmh::layout::CurrentMatchday)==original_source_seed&&
  (c.session->state().memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)&0x10)==0,
  "final close commits no forecast result/G seed alias and clears transient Native bit");
 check(c.flow.click(200,100,true).request==ui::FlowRequest::none&&c.rev==2,
  "queued right-UP after KI-Tipp close remains unowned");
}
void search_core(const fs::path& data,const fs::path& input) {
 app::CareerSession live(data,83,input);auto g=live.state();g.league_rounds.assign(9,0);
 const auto exe=bmh::test::load_executable(data/"BMH.exe");const auto constants=ui::league_pairings_constants(exe);
 g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,0xa1);auto owner=open_league_pairings_source(g,0,constants);
 GameRandom G(-170001);WatcomRandom W(83);const auto results=g.results;
 auto edit=[&](LeaguePairingsEvent e){owner=edit_league_pairings_source(g,owner,e,constants,G,W);};
 auto select=[&](int team){edit({LeaguePairingsAction::search_team,team,g.club_ids.at(size_t(team))});};
 edit({LeaguePairingsAction::search});
 check(g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)==0xa2&&owner.search_first==-2&&owner.search_second==-2&&!owner.search_second_click,"Source search arms2 preserving high nibble, reset sentinels");
 select(0);check(owner.search_first==0&&owner.search_second==-1&&owner.search_second_click&&owner.round==0,"first fixture DOWN chooses first team without navigating");
 edit({LeaguePairingsAction::previous});check(owner.round==33&&owner.search_first==0&&owner.search_second_click,"search previous wrap preserves pending selection");
 edit({LeaguePairingsAction::next});check(owner.round==0&&owner.search_first==0&&owner.search_second_click,"search next wrap preserves pending selection");
 select(1);check(owner.round==0&&owner.title_round==0&&owner.search_first==0&&owner.search_second==1&&!owner.search_second_click,"literal0-home1-away directed first-leg round0");
 select(1);check(owner.round==0&&owner.search_first==1&&owner.search_second==-1&&owner.search_second_click,"third click after success replaces first and clears second");
 select(0);check(owner.round==17&&owner.search_first==1&&owner.search_second==0&&!owner.search_second_click,"literal1-home0-away directed return-leg round17");
 select(0);select(0);check(owner.round==17&&owner.search_first==0&&owner.search_second==0&&owner.search_second_click,"same-team no match retains round, highlights and second phase");
 select(1);check(owner.round==0&&!owner.search_second_click,"no-match permits later valid second choice");
 check(g.results==results&&G.calls()==0&&W.calls()==0,"complete query/navigation is result/Native-RNG pure except preference");
 auto expected=live.state();expected.league_rounds.assign(9,0);expected.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,0xa1);open_league_pairings_source(expected,0,constants);expected.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,0xa2);
 check(save_stream(g)==save_stream(expected),"search Native effects exactly declared preference byte");
 const auto before=g;const auto owned=owner;
 rejects([&]{edit_league_pairings_source(g,owner,{LeaguePairingsAction::search_team,50,g.club_ids[50]},constants,G,W);},"nonvisible typed selection rejected");
 rejects([&]{edit_league_pairings_source(g,owner,{LeaguePairingsAction::search_team,0,g.club_ids[0]^1},constants,G,W);},"wrong stable query identity rejected");
 rejects([&]{edit_league_pairings_source(g,owner,{LeaguePairingsAction::team_info,0,g.club_ids[0]},constants,G,W);},"armed search cannot request invented nested TeamInfo");
 auto forged=owner;forged.search_first_identity^=1;rejects([&]{league_pairings_view_source(g,forged,constants);},"stale first selection identity rejected");
 check(save_stream(g)==save_stream(before)&&owner==owned&&G.calls()==0&&W.calls()==0,"failed query has no Native/owner/RNG effect");
 // format field overrides the selected0xfb default for managed/special teams.
 // AlltimeTable_TeamColor_RuntimeField is an optional process global, not a fabricated Native save alias.
 g.memory.add_block(::bmh::layout::AlltimeTable_TeamColor_RuntimeField,2);g.memory.set16(::bmh::layout::AlltimeTable_TeamColor_RuntimeField,0xfffe);g.manager_teams[0]=1;g.manager_records[0xa8]=1;
 g.memory.set8(::bmh::layout::DeskPosition,0);g.memory.set8(::bmh::layout::ManagerCount,2);
 auto display=ui::league_pairings_from_game(g,owner,exe,1,1,0);
 check(display.rows[0].name_colors[0]==0xfb&&display.rows[0].name_colors[1]==0x7a,"search highlight default and active manager override");
 g.memory.set16(::bmh::layout::AlltimeTable_TeamColor_RuntimeField,0);display=ui::league_pairings_from_game(g,owner,exe,1,1,0);check(display.rows[0].name_colors[0]==0xf7,"special team overrides search highlight");
 edit({LeaguePairingsAction::search});check(owner.search_first==-2&&owner.search_second==-2&&!owner.search_second_click&&(g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)&15)==2,"repeated search restarts2 rather than toggling normal");
 select(0);edit({LeaguePairingsAction::select_league,5});check((g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)&15)==1&&owner.search_first==-1&&owner.search_second==-1&&!owner.search_second_click,"changed league clears selections and exits search");
 edit({LeaguePairingsAction::search});select(90);select(91);check(owner.round==0,"literal lower league90-home91-away first leg");
 select(91);select(90);check(owner.round==11,"literal lower league91-home90-away return leg");
 edit({LeaguePairingsAction::reset});check(owner.search_first==-1&&(g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)&15)==1,"Paarungen exits search and resets normal round");
 // An already saved bit2 does not recreate Source's local60 search sentinel.
 g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,2);owner=open_league_pairings_source(g,0,constants);
 check(owner.search_first==-1,"saved mode2 starts with ordinary Source row ownership");
 rejects([&]{select(0);},"saved2 unarmed row cannot choose query team");
 g.league_rounds[0]=34;owner=open_league_pairings_source(g,0,constants);check(owner.title_round==34&&owner.round==33,"initial seasonend title before redraw clamp");
 edit({LeaguePairingsAction::search});check(owner.title_round==33,"search redraw uses already clamped Source local34");
 Oracle oracle{W.seed(),G.seed(),W.calls(),G.calls()};oracle.stir();edit({LeaguePairingsAction::close});
 check(owner.closed&&(g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)&15)==1&&G.seed()==oracle.g&&G.calls()==oracle.gc&&W.seed()==oracle.w&&W.calls()==oracle.wc,"search final close resets1 and only established once-only stir");
}
void club_filter_core(const fs::path& data,const fs::path& input) {
 app::CareerSession live(data,97,input);auto g=live.state();g.league_rounds.assign(9,0);
 const auto exe=bmh::test::load_executable(data/"BMH.exe");const auto constants=ui::league_pairings_constants(exe);
 g.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,0xa1);
 for(int k=0;k<40;++k)g.memory.set16(::bmh::layout::Postponed+uint32_t(k)*6,0);
 int result_marker=0;
 for(int round=0;round<34;++round) {
  const auto fixtures=league_fixtures(0,round+1);
  for(size_t slot=0;slot<fixtures.size();++slot)if(fixtures[slot].home==0||fixtures[slot].away==0) {
   g.results[result_index(0,round+1,int(slot))]=uint8_t(0x10+(result_marker++%0x0e));
  }
 }
 check(result_marker==34,"18-team Source fixture oracle has one match per round for club0");
 g.results[result_index(0,2,6)]=0xcd;
 g.memory.set16(::bmh::layout::Postponed,11);g.memory.set8(::bmh::layout::Postponed+2,1);g.memory.set8(::bmh::layout::Postponed+3,0);g.memory.set8(::bmh::layout::Postponed+4,6);
 // First-leg score bytes are indexed by their original round/fixture slot.
 const auto return_fixtures=league_fixtures(0,18);
 size_t return_slot=0;while(return_slot<return_fixtures.size()&&return_fixtures[return_slot].home!=0&&return_fixtures[return_slot].away!=0)++return_slot;
 check(return_slot<return_fixtures.size()&&(return_fixtures[return_slot].home==0||return_fixtures[return_slot].away==0),"18-team return-leg fixture containing club0 exists");
 g.results[result_index(0,1,int(return_slot))]=0x12;
 const auto scheduled_fixtures=league_fixtures(0,3);
 const auto scheduled_slot=std::find_if(scheduled_fixtures.begin(),scheduled_fixtures.end(),[](const auto& f){return f.home==0||f.away==0;});
 check(scheduled_slot!=scheduled_fixtures.end(),"18-team scheduled-date test fixture exists");
 const int scheduled_fixture_slot=int(scheduled_slot-scheduled_fixtures.begin());
 g.results[result_index(0,3,scheduled_fixture_slot)]=0xcc;
 auto owner=open_league_pairings_source(g,0,constants);GameRandom G(-99123);WatcomRandom W(97);
 const auto native_before=save_stream(g);const auto calls_g=G.calls(),calls_w=W.calls();
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::filter_by_club},constants,G,W);
 auto expected_native=g;expected_native.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,0xa4);
 // Compare against the state before the single declared Source preference byte changes.
 auto before_filter=GameState::from_save(SaveState(native_before));
 before_filter.memory.set8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField,0xa4);
 check(save_stream(g)==save_stream(before_filter)&&owner.filter_active&&owner.filter_team==-2&&
  owner.search_first==-1&&owner.search_second==-1&&owner.filter_offset==0&&g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)==0xa4,
  "Source TEXTD97 arms a separate mode4 filter and changes only its preference byte");
 auto page=league_pairings_view_source(g,owner,constants);
 check(page.rows.size()==9&&page.total_rows==9&&page.page_offset==0,"unselected mode4 displays the current round normally");
 auto selected=edit_league_pairings_source(g,owner,{LeaguePairingsAction::filter_club,page.rows[0].home.runtime_team,
  page.rows[0].home.club_identity},constants,G,W);
 owner=selected;page=league_pairings_view_source(g,owner,constants);
 check(owner.filter_team==0&&owner.filter_identity==g.club_ids[0]&&owner.search_first==-1&&owner.search_second==-1,
  "first visible row click pins a typed single-club identity, separate from Search2");
 check(page.total_rows==34&&page.rows.size()==10&&page.page_offset==0,"18-team filter spans all 34 rounds with ten rows per page");
 const auto scheduled=std::find_if(page.rows.begin(),page.rows.end(),[](const auto& row){return row.raw_result==0xcc;});
 auto scheduled_round_owner=owner;scheduled_round_owner.round=scheduled==page.rows.end()?0:scheduled->source_round;
 const auto scheduled_round_date=league_pairings_view_source(g,scheduled_round_owner,constants).match_date;
 check(scheduled!=page.rows.end()&&scheduled->source_round==2&&scheduled->scheduled_date&&
  *scheduled->scheduled_date==scheduled_round_date&&!scheduled->postponed_date,
  "selected-mode Source raw0xcc carries its real round date scratch");
 auto selected_display=ui::league_pairings_from_game(g,owner,exe,1,1,0);
 const size_t scheduled_index=size_t(scheduled-page.rows.begin());
 auto day=ui::desk_date(*scheduled->scheduled_date,exe);char day_text[20];
 std::snprintf(day_text,sizeof day_text,"%02d.%02d.%04d",day.day,day.month,day.year);
 check(selected_display.date.empty()&&selected_display.rows[scheduled_index].scheduled_ordinal==
  std::string("%3")+message_text(g,::bmh::layout::CupDraw_FormatError_TextPointer6)&&selected_display.rows[scheduled_index].scheduled_date==
  exe.cstring(::bmh::layout::LeaguePairings_FormatError_TextPointer)+day_text+exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer11),
  "selected Mode4 omits upper date and renders the Source ordinal plus parenthesized real row date");
 ui::LeaguePairingsScreen selected_screen(data,selected_display);
 const auto scheduled_json=selected_screen.state_json();
 check(scheduled_json.find("\"source_round\":2")!=std::string::npos&&
  scheduled_json.find("\"fixture_index\":"+std::to_string(scheduled_fixture_slot))!=std::string::npos&&
  scheduled_json.find("\"scheduled_date\":"+std::to_string(*scheduled->scheduled_date))!=std::string::npos,
  "Mode4 diagnostics retain actual Native round/fixture/date coordinates");
 std::vector<std::pair<int,int>> expected_slots;
 for(int round=0;round<34;++round) {
  const auto fixtures=league_fixtures(0,round+1);
  for(size_t slot=0;slot<fixtures.size();++slot)if(fixtures[slot].home==0||fixtures[slot].away==0)
   expected_slots.emplace_back(round,int(slot));
 }
 check(expected_slots.size()==34,"independent fixture-order oracle contains all home and away dates");
 bool home=false,away=false,postponed=false;
 for(size_t i=0;i<page.rows.size();++i) {
  const auto& row=page.rows[i];const auto [round,slot]=expected_slots[i];
  check(row.source_round==round&&row.fixture_index==slot&&
   (row.home.runtime_team==0||row.away.runtime_team==0)&&
   row.raw_result==g.results[result_index(0,round+1,slot)],
   "filtered visible row retains actual Source round, fixture slot and result byte");
  home|=row.home.runtime_team==0;away|=row.away.runtime_team==0;
  if(round==1&&slot==6)postponed=row.raw_result==0xcd&&row.postponed_date==g.season_start+11&&!row.scheduled_date;
 }
 check(home&&away,"18-team filtered fixture order includes both home and away legs");
 check(postponed,"18-team filter retains the Source postponement record for its original fixture slot");
 const auto no_native=save_stream(g);const auto before_round=owner.round;
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::next},constants,G,W);
 page=league_pairings_view_source(g,owner,constants);
 const auto return_row=std::find_if(page.rows.begin(),page.rows.end(),[](const auto& row){return row.source_round==17;});
 check(return_row!=page.rows.end()&&return_row->fixture_index==int(return_slot)&&!return_row->first_leg_result,
  "selected Mode4 suppresses Source first-leg output while preserving the real fixture slot");
 check(owner.filter_offset==10&&page.page_offset==10&&page.rows.size()==10&&
  std::pair{page.rows[0].source_round,page.rows[0].fixture_index}==expected_slots[10],
  "mode4 next arrow pages ten fixture rows in Source order");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::next},constants,G,W);
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::next},constants,G,W);
 page=league_pairings_view_source(g,owner,constants);
 check(owner.filter_offset==30&&page.rows.size()==4&&
  std::pair{page.rows[0].source_round,page.rows[0].fixture_index}==expected_slots[30],
  "mode4 third page preserves original fixture order and tail length");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::next},constants,G,W);
 check(owner.filter_offset==30,"mode4 next arrow stops at the real final-page boundary");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::previous},constants,G,W);
 check(owner.filter_offset==20,"mode4 previous arrow moves one ten-row page without wrapping");
 page=league_pairings_view_source(g,owner,constants);const int replacement=page.rows[0].home.runtime_team==0?
  page.rows[0].away.runtime_team:page.rows[0].home.runtime_team;
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::filter_club,replacement,
  g.club_ids.at(size_t(replacement))},constants,G,W);
 check(owner.filter_team==replacement&&owner.filter_identity==g.club_ids.at(size_t(replacement))&&owner.filter_offset==20,
  "later row click replaces single filtered club and retains a still-valid Source page offset");
 const auto saved_state=save_stream(g);const auto stable_owner=owner;
 rejects([&]{edit_league_pairings_source(g,owner,{LeaguePairingsAction::filter_club,50,g.club_ids[50]},constants,G,W);},
  "non-visible filter team rejected");
 rejects([&]{edit_league_pairings_source(g,owner,{LeaguePairingsAction::filter_club,replacement,g.club_ids[replacement]^1},constants,G,W);},
  "stale stable filter identity rejected");
 check(save_stream(g)==saved_state&&owner==stable_owner&&G.calls()==calls_g&&W.calls()==calls_w,
  "rejected filter selections preserve Native, typed owner and both RNG streams");
 check(owner.round==before_round&&save_stream(g)==no_native,"filter pagination is not matchday or Native mutation");
 // Switching to an already supported league resets mode4; the 12-team lower league has 22 dates.
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::select_league,5},constants,G,W);
 check(!owner.filter_active&&owner.filter_team==-1&&(g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)&0x0f)==1,
  "league selector follows Source reset and clears club filter");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::filter_by_club},constants,G,W);
 page=league_pairings_view_source(g,owner,constants);
 const int first_lower=page.rows[0].home.runtime_team;
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::filter_club,first_lower,g.club_ids[first_lower]},constants,G,W);
 page=league_pairings_view_source(g,owner,constants);
 check(owner.league==5&&page.total_rows==22&&page.rows.size()==10,"12-team filter spans 22 Source dates");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::next},constants,G,W);owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::next},constants,G,W);
 page=league_pairings_view_source(g,owner,constants);
 check(owner.filter_offset==20&&page.rows.size()==2,"12-team final filter page contains two rows");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::next},constants,G,W);
 check(owner.filter_offset==20,"12-team page boundary does not wrap");
 auto display=ui::league_pairings_from_game(g,owner,exe,1,1,0);
 check(display.title==message_text(g,::bmh::layout::LeaguePairings_FormatError_TextPointer11)+g.club_name(0x166)&&
  display.subtitle==g.club_name(first_lower),"selected filter uses Source Spielplan title and selected club footer");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::search},constants,G,W);
 check(!owner.filter_active&&owner.filter_team==-1&&owner.search_first==-2&&
  (g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)&0x0f)==2,"mode2 Search remains separate and clears mode4 owner fields");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::filter_by_club},constants,G,W);
 check(owner.filter_active&&owner.filter_team==-2&&owner.search_first==-1&&
  (g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)&0x0f)==4,"entering mode4 clears Search2 sentinels");
 owner=edit_league_pairings_source(g,owner,{LeaguePairingsAction::reset},constants,G,W);
 check(!owner.filter_active&&owner.filter_team==-1&&owner.filter_offset==0&&
  (g.memory.u8(::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField)&0x0f)==1&&G.calls()==calls_g&&W.calls()==calls_w,
  "Paarungen reset exits filter without RNG; only the established final close may stir");
}
void search_app_ui(const fs::path& data,const fs::path& input) {
 Context c(data,input);const auto original=save_stream(c.session->state());const auto runtime=c.session->club_modal_runtime_checkpoint();const auto W=c.session->rand();const auto G=c.session->game();
 auto stable=[&]{return save_stream(c.session->state())==original&&c.session->club_modal_runtime_checkpoint()==runtime&&c.session->rand().seed()==W.seed()&&c.session->rand().calls()==W.calls()&&c.session->game().seed()==G.seed()&&c.session->game().calls()==G.calls()&&c.rev==1;};
 c.flow.pointer_down(10,190);check(!c.flow.pending_league_pairings(),"search button DOWN holds without dispatch");
 auto search=c.flow.click(10,190);check(search.league_pairings_action.event.kind==LeaguePairingsAction::search&&c.serve(search).empty()&&stable(),"paired search button ACK publishes staged page only");
 check(c.flow.league_pairings()->state().identity.sequence==1&&c.flow.league_pairings()->state().source.owner.search_first==-2&&c.flow.league_pairings()->state().subtitle.find("34")!=std::string::npos,"search transition sequence and clamped last round title");
 const auto armed=c.flow.league_pairings()->state();ui::LeaguePairingsScreen release(data,armed);release.pointer_down(10,190);release.move(110,80);release.release(10,190);check(!release.pending(),"leaving search button cancels paired action");
 ui::LeaguePairingsScreen home_edge(data,armed),away_edge(data,armed);home_edge.pointer_down(120,29);away_edge.pointer_down(121,29);
 check(home_edge.pending()->event.value==16&&away_edge.pending()->event.value==0,"Source x120 home and x121 away row-side boundary");
 std::string error;check(!c.session->dispatch_league_pairings(search.league_pairings_action,c.rev,error)&&stable(),"stale search sequence rejected");
 auto first=c.flow.pointer_down(5,29);check(first.league_pairings_action.event.kind==LeaguePairingsAction::search_team&&first.league_pairings_action.event.value==16,"literal last-round home selection acts on DOWN");
 check(c.serve(first).empty()&&stable()&&c.session->league_pairings_owner()->owner.search_first==16&&c.session->league_pairings_owner()->owner.search_second_click,"search row needs no nested presenter and stages first choice");
 auto up=c.flow.click(5,29);check(up.request==ui::FlowRequest::none&&c.flow.league_pairings()->state().identity.sequence==2,"paired row UP cannot select second team");
 auto second=c.flow.pointer_down(132,29);check(second.league_pairings_action.event.value==0&&c.serve(second).empty()&&stable(),"literal last-round away DOWN matches directed16-home0-away");
 check(c.session->league_pairings_owner()->owner.round==33&&!c.session->league_pairings_owner()->owner.search_second_click&&c.flow.league_pairings()->state().identity.sequence==3,"query match ACK once leaves last round");
 const auto coherent=c.session->league_pairings_checkpoint();
 auto bad=coherent;bad.draft->owner.search_first_identity^=1;c.session->restore_league_pairings_checkpoint(bad);
 c.flow.pointer_down(10,190);auto restart=c.flow.click(10,190);check(!c.serve(restart).empty()&&stable()&&!c.flow.pending_league_pairings(),"stale search owner rejection rolls back parent and revision");
 const auto after=c.session->league_pairings_checkpoint();check(after.draft->owner==bad.draft->owner&&after.draft->identity==bad.draft->identity&&save_stream(after.draft->state)==save_stream(bad.draft->state)&&after.draft->runtime==bad.draft->runtime&&after.draft->rand.seed()==bad.draft->rand.seed()&&after.draft->game.seed()==bad.draft->game.seed(),"failed search dispatch retains exact owner/Native/runtime/W/G");
 c.session->restore_league_pairings_checkpoint(coherent);
 auto pixels=c.flow.render().pixels;check(c.flow.render().pixels==pixels&&stable(),"query render purity");
 c.flow.pointer_down(10,180);check(c.serve(c.flow.click(10,180)).empty()&&stable()&&c.session->league_pairings_owner()->owner.search_first==-1,"actual Paarungen paired click returns normal row ownership");
 bool presented=false;auto info=c.flow.pointer_down(5,29);check(info.league_pairings_action.event.kind==LeaguePairingsAction::team_info&&c.serve(info,[&](app::ClubModalServices&,int,const Image&){presented=true;return true;}).empty()&&presented&&stable(),"real nested TeamInfo remains reachable after search reset");
 const auto stage=*c.session->league_pairings_owner();Oracle oracle{stage.rand.seed(),stage.game.seed(),stage.rand.calls(),stage.game.calls()};oracle.stir();
 check(c.serve(c.flow.pointer_down(200,100,true)).empty()&&c.rev==2&&c.flow.screen_name()=="desktop"&&c.session->game().seed()==oracle.g&&c.session->rand().seed()==oracle.w,"search family final DOWN exits with Source stir/revision once");
 check(c.flow.click(200,100,true).request==ui::FlowRequest::none&&c.rev==2,"right UP retains accepted final ownership");
}
void app_ui(const fs::path& data,const fs::path& input){
 Context c(data,input);const auto original=save_stream(c.session->state());const auto runtime=c.session->club_modal_runtime_checkpoint();const auto w=c.session->rand();const auto g=c.session->game();
 auto same=[&]{return original==save_stream(c.session->state())&&runtime==c.session->club_modal_runtime_checkpoint()&&w.calls()==c.session->rand().calls()&&w.seed()==c.session->rand().seed()&&g.calls()==c.session->game().calls()&&g.seed()==c.session->game().seed()&&c.rev==1;};
 check(same(),"opening stages complete Native/W/G/runtime and no revision");const auto pixels=c.flow.render().pixels;check(c.flow.render().pixels==pixels&&same(),"repeated actual render is pure");
 check(c.flow.league_pairings()->state().subtitle.find(bmh::test::expected_text("league_pairings_tests-external-1").c_str())!=std::string::npos,"unclamped Source title end-of-season");
 const auto page=c.flow.league_pairings()->state();ui::LeaguePairingsScreen button(data,page);
 check(!button.release(10,180)&&!button.pending(),"unowned UP cannot activate Source button");button.pointer_down(10,190);check(!button.pending()&&button.render().pixels!=pixels,"button held image before release");button.move(200,30);button.release(10,190);check(!button.pending(),"Source button cancels after leaving rectangle");
 button.pointer_down(90,190);button.release(90,190);check(!button.pending(),"Original ellipsis remains visible no-op");
 ui::LeaguePairingsScreen edge(data,page);edge.pointer_down(176,200);edge.move(175,200);edge.release(175,200);check(edge.pending()&&edge.pending()->event.kind==LeaguePairingsAction::next,"right held arrow keeps inclusive overlapping x175 boundary");
 for(int n=0;n<8;++n){ui::LeaguePairingsScreen control(data,page);const int x=1+n/4*74+5,y=176+n%4*13+5;control.pointer_down(x,y);check(!control.pending(),"all Original buttons wait for paired release");control.release(x,y);if(n==5)check(!control.pending(),"ellipsis no Source action");else check(control.pending()&&control.pending()->event.kind==(n==0?LeaguePairingsAction::reset:n==1?LeaguePairingsAction::search:n==2?LeaguePairingsAction::filter_by_club:n==3?LeaguePairingsAction::postponed_list:n==4?LeaguePairingsAction::computer_tip:LeaguePairingsAction::unsupported),"all eight Original actions remain visible and qualified");}
 bool selected=false;for(int y=201;y<209&&!selected;++y)for(int x=225;x<233&&!selected;++x){ui::LeaguePairingsScreen selector(data,page);selector.pointer_down(x,y);if(selector.pending()){check(selector.pending()->event.kind==LeaguePairingsAction::select_league&&selector.pending()->event.value==2,"literal Source primary selector row2 left icon league2");selected=true;}}check(selected,"Original selector pixels have a physical hit");
 ui::LeaguePairingsScreen group(data,page);group.pointer_down(270,180);check(group.pending()&&group.pending()->event.kind==LeaguePairingsAction::toggle_group,"Source right selector changes league group");
 check(c.serve(c.flow.send_key("ESC")).empty()&&c.flow.screen_name()=="league_pairings"&&same(),"ESC is not Source event8");
 {
  Context family(data,input);
  family.flow.pointer_down(84,206);
  const auto to_table=family.flow.click(84,206);
  check(to_table.request==ui::FlowRequest::league_family_action&&
        to_table.league_family_action.kind==ui::LeagueFamilyRequestKind::navigate&&
        to_table.league_family_action.target_child==1&&family.serve(to_table).empty()&&
        family.flow.screen_name()=="league_table",
        "Source Pairings TABELLE footer navigates to the supported family Table child");
  const auto checkpoint=family.session->league_family_checkpoint();
  auto unchanged=[&]{const auto now=family.session->league_family_checkpoint();
   const app::CareerSession::LeaguePairingsCheckpoint now_pairings{
    now.state,now.game,now.rand,now.runtime,now.pairings,now.family,now.next_family_id};
   const app::CareerSession::LeaguePairingsCheckpoint old_pairings{
    checkpoint.state,checkpoint.game,checkpoint.rand,checkpoint.runtime,checkpoint.pairings,
    checkpoint.family,checkpoint.next_family_id};
   return save_stream(now.state)==save_stream(checkpoint.state)&&now.runtime==checkpoint.runtime&&
    now.game.seed()==checkpoint.game.seed()&&now.game.calls()==checkpoint.game.calls()&&
    now.rand.seed()==checkpoint.rand.seed()&&now.rand.calls()==checkpoint.rand.calls()&&
    now.family==checkpoint.family&&now.next_strengths_scope==checkpoint.next_strengths_scope&&
    now.next_family_id==checkpoint.next_family_id&&same_pairings_checkpoint(now_pairings,old_pairings)&&
    !now.strengths&&!checkpoint.strengths&&family.rev==1;
  };
  ui::LeagueFamilyRequest invalid_control;
  invalid_control.identity=*family.flow.league_family_identity();
  invalid_control.kind=ui::LeagueFamilyRequestKind::control;
  invalid_control.from_child=1;invalid_control.control_index=10;
  std::string table_error;
  check(!family.session->dispatch_league_table_control(invalid_control,family.rev,table_error)&&
        !table_error.empty()&&unchanged(),
        "invalid Table control rejects with the complete family checkpoint intact");
  family.flow.pointer_down(5,176);const auto control=family.flow.click(5,176);
  const auto control_error=family.serve(control);
  const auto table_dispatch=family.session->current_league_table_dispatch(table_error);
  check(control.request==ui::FlowRequest::league_family_action&&
        control.league_family_action.kind==ui::LeagueFamilyRequestKind::control&&
        control.league_family_action.control_index==0&&control_error.empty()&&
        family.flow.screen_name()=="league_table"&&!family.flow.pending_league_family_action()&&
        table_dispatch&&table_error.empty()&&table_dispatch->page.source_aggregation_flags==7&&family.rev==2&&
        family.session->state().memory.u16(::bmh::layout::LeagueTableDisplayModes)==
            uint16_t(((checkpoint.state.memory.u16(::bmh::layout::LeagueTableDisplayModes)&0x0ff0)|1)&0x0f7f)&&
        family.session->game().seed()==checkpoint.game.seed()&&
        family.session->game().calls()==checkpoint.game.calls()&&
        family.session->rand().seed()==checkpoint.rand.seed()&&
        family.session->rand().calls()==checkpoint.rand.calls(),
        "Source Table Gesamt applies flags7 and the literal draw word without W/G draws");
  family.flow.pointer_down(131,202);const auto back=family.flow.click(131,202);
  check(back.request==ui::FlowRequest::league_family_action&&
        back.league_family_action.kind==ui::LeagueFamilyRequestKind::navigate&&
        back.league_family_action.target_child==2&&family.serve(back).empty()&&
        family.flow.screen_name()=="league_pairings",
        "Source Table SPIELE footer returns through the actual family request identity");
 }
 c.flow.pointer_down(190,200);auto next=c.flow.click(190,200);check(c.serve(next).empty()&&same()&&c.flow.league_pairings()->state().identity.sequence==1&&c.flow.league_pairings()->state().identity.round==0,"paired arrow wraps with only local sequence");
 std::string err;auto stale=next.league_pairings_action;check(!c.session->dispatch_league_pairings(stale,c.rev,err)&&same(),"stale sequence rejects");
 const auto initialdraft=*c.session->league_pairings_owner();auto row=c.flow.pointer_down(5,29);check(row.request==ui::FlowRequest::league_pairings_action,"actual row DOWN typed team request");check(!c.serve(row).empty()&&same()&&!c.flow.pending_league_pairings(),"missing team presenter rollback");
 row=c.flow.pointer_down(5,29);auto throwing=[&](app::ClubModalServices& svc,int team,const Image& parent)->bool{check(svc.transactional&&&svc.state()!=&c.session->state()&&team==0&&parent.width==320&&same(),"scoped presenter actual first fixture team");const auto range=squad_range(team);int row=0;while(row<int(range.count)&&(svc.state().squad_slots[range.first+size_t(row)]&0xfff)==0xfff)++row;check(row<int(range.count),"actual team has player");svc.redraw_player({team,int(svc.state().squad_slots[range.first+size_t(row)]&0xfff),row,row});svc.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField,2);svc.state().memory.set16(::bmh::layout::LeagueTableDisplayModes,0xffff);throw std::runtime_error("declared presenter failure");};
 check(!c.serve(row,throwing).empty()&&same(),"throw restores full staged state");auto after=*c.session->league_pairings_owner();check(save_stream(after.state)==save_stream(initialdraft.state)&&after.runtime==initialdraft.runtime&&after.rand.seed()==initialdraft.rand.seed()&&after.game.seed()==initialdraft.game.seed()&&after.identity==initialdraft.identity,"failed nested presenter retained exact owner");
 row=c.flow.pointer_down(5,29);auto accepted=[&](app::ClubModalServices& svc,int team,const Image&){check(team==0&&same(),"live untouched inside accepted nested modal");svc.enter_club(team);const auto range=squad_range(team);int row=0;while(row<int(range.count)&&(svc.state().squad_slots[range.first+size_t(row)]&0xfff)==0xfff)++row;check(row<int(range.count),"actual team has player");svc.redraw_player({team,int(svc.state().squad_slots[range.first+size_t(row)]&0xfff),row,row});svc.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField,2);return true;};
 check(c.serve(row,accepted).empty()&&same()&&c.flow.league_pairings()->state().identity.sequence==2,"nested team redraw publishes staging only");
 check(!c.session->save_game(c.dir/"BLOCK.FBM",0,false,err)&&err=="league_pairings_active","save API blocked during family");
 auto invalid=page;invalid.identity.club_identity^=1;rejects([&]{ui::LeaguePairingsScreen bad(data,invalid);},"forged projection stable identity rejected");
 app::CareerSession timer(data,1,c.dir/"AUTOSAVE.FBM");auto timer_services=timer.live_club_modal_services(c.rev);timer_services.set_runtime_byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField,5);timer_services.set_runtime_byte(::bmh::layout::Main_FormatError_RuntimeField4,0);timer_services.set_runtime_byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField2,1);for(uint32_t a=::bmh::layout::CareerSession_DispatchStrengths_ExecutableField3;a<=::bmh::layout::CareerSession_DispatchStrengths_ExecutableField5;++a)timer_services.set_runtime_byte(a,0);const auto timer_before=save_stream(timer.state());const auto timer_runtime=timer.club_modal_runtime_checkpoint();
 ui::DeskRequest timer_request;timer_request.kind=ui::DeskRequestKind::open_menu;timer_request.menu_kind=1;timer_request.manager_slot=0;timer_request.revision=c.rev;
 check(!timer.open_league_pairings(timer_request,c.rev,err)&&err.find("active process timer")!=std::string::npos&&timer_before==save_stream(timer.state())&&timer_runtime==timer.club_modal_runtime_checkpoint()&&!timer.league_pairings_owner(),"real active process timer rejected without Native/runtime effects");
 const auto coherent=c.session->league_pairings_checkpoint();auto broken=coherent;++broken.draft->state.date;c.session->restore_league_pairings_checkpoint(std::move(broken));
 const auto invalidstage=*c.session->league_pairings_owner();check(!c.serve(c.flow.pointer_down(200,100,true)).empty()&&same()&&c.flow.screen_name()=="league_pairings"&&!c.flow.pending_league_pairings(),"failed Flow close ACK rolls back complete session/revision");
 const auto restored=*c.session->league_pairings_owner();check(restored.identity==invalidstage.identity&&!restored.owner.closed&&restored.rand.seed()==invalidstage.rand.seed()&&restored.rand.calls()==invalidstage.rand.calls()&&restored.game.seed()==invalidstage.game.seed()&&restored.game.calls()==invalidstage.game.calls()&&restored.runtime==invalidstage.runtime&&save_stream(restored.state)==save_stream(invalidstage.state),"Flow ACK rollback retains original draft including runtime/W/G");
 c.session->restore_league_pairings_checkpoint(coherent);
 const auto stage=*c.session->league_pairings_owner();Oracle oracle{stage.rand.seed(),stage.game.seed(),stage.rand.calls(),stage.game.calls()};oracle.stir();
 check(c.serve(c.flow.pointer_down(200,100,true)).empty()&&c.flow.screen_name()=="desktop"&&c.rev==2&&!c.session->league_pairings_owner(),"Source right-DOWN final close commits exactly one revision");
 check(c.session->game().seed()==oracle.g&&c.session->game().calls()==oracle.gc&&c.session->rand().seed()==oracle.w&&c.session->rand().calls()==oracle.wc,"final Source stir follows staged nested RNG once");
 check(c.session->live_club_modal_services(c.rev).runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField)==2&&c.session->state().date==stage.state.date&&c.session->state().results==stage.state.results,"committed runtime and unchanged calendar/results");
 auto up=c.flow.click(200,100,true);check(up.request==ui::FlowRequest::none&&c.rev==2,"queued right UP has no family close action");
 const auto saved=save_stream(c.session->state());check(c.session->save_game(c.dir/"PAIR.FBM",0,false,err),"save after actual family exit");auto reload=load_savegame(c.dir/"PAIR.FBM");check(reload.checksum_ok&&reload.stream==saved,"Native committed fields survive checksum-valid save reload");
}
void club_filter_app_ui(const fs::path& data,const fs::path& input) {
 Context c(data,input);
 auto& test_state=c.session->state();
 const int test_league=team_league(test_state.manager_teams[0]);
 const int current_round=std::min<int>(test_state.league_rounds[size_t(test_league)],league_matchdays(test_league)-1);
 const auto current_fixtures=league_fixtures(test_league,current_round+1);
 const int selected_for_runtime=current_fixtures.front().home;
 for(int round=0;round<league_matchdays(test_league);++round) {
  const auto fixtures=league_fixtures(test_league,round+1);
  for(size_t slot=0;slot<fixtures.size();++slot)if(fixtures[slot].home==selected_for_runtime||fixtures[slot].away==selected_for_runtime)
   test_state.results[result_index(test_league,round+1,int(slot))]=0xcc;
 }
 const auto original=save_stream(c.session->state());
 const auto runtime=c.session->club_modal_runtime_checkpoint();const auto W=c.session->rand();const auto G=c.session->game();
 auto stable=[&]{return save_stream(c.session->state())==original&&runtime==c.session->club_modal_runtime_checkpoint()&&
  W.seed()==c.session->rand().seed()&&W.calls()==c.session->rand().calls()&&
  G.seed()==c.session->game().seed()&&G.calls()==c.session->game().calls()&&c.rev==1;};
 c.flow.pointer_down(10,207);check(!c.flow.pending_league_pairings(),"filter button DOWN awaits paired release");
 auto start=c.flow.click(10,207);
 check(start.league_pairings_action.event.kind==LeaguePairingsAction::filter_by_club&&c.serve(start).empty()&&stable(),
  "Original Spiele von button starts staged mode4 without committing live state");
 const auto& armed=c.flow.league_pairings()->state();
 check(armed.source.owner.filter_active&&armed.source.owner.filter_team==-2&&armed.source.owner.search_first==-1&&
  armed.source.preferences==4&&armed.identity.sequence==1,"mode4 UI owner and preference nibble");
 const int selected=armed.source.rows[0].home.runtime_team;
 auto row=c.flow.pointer_down(5,29);
 check(row.league_pairings_action.event.kind==LeaguePairingsAction::filter_club&&
  row.league_pairings_action.event.value==selected&&c.serve(row).empty()&&stable(),
  "filtered page team DOWN stages a typed club selection without TeamInfo presenter");
 const auto& filtered=c.flow.league_pairings()->state();
 check(filtered.source.owner.filter_team==selected&&filtered.source.owner.filter_identity==c.session->state().club_ids[size_t(selected)]&&
  filtered.source.total_rows==34&&filtered.rows.size()==10&&filtered.identity.sequence==2&&
  filtered.date.empty()&&filtered.source.rows.back().scheduled_date.has_value()&&
  filtered.title==message_text(c.session->state(),::bmh::layout::LeaguePairings_FormatError_TextPointer11)+league_name(c.session->state(),0)&&
  filtered.subtitle==c.session->state().club_name(selected),
  "filter ACK omits upper date and renders source title, selected club footer and all-round ten-row projection");
 const auto& scheduled_row=filtered.source.rows.back();const auto expected_date=ui::desk_date(*scheduled_row.scheduled_date,bmh::test::load_executable(data/"BMH.exe"));
 const auto draft=c.session->league_pairings_checkpoint().draft;
 check(draft.has_value(),"Mode4 runtime remains in its staged owner");
 const auto scratch=[&](uint32_t address){return draft->state.memory.has(address)?draft->state.memory.u8(address):draft->runtime.at(address);};
 check(scratch(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField4)==expected_date.day&&scratch(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField5)==expected_date.month&&
  scratch(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField6)==expected_date.weekday&&scratch(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField2)==uint8_t(expected_date.year)&&
  scratch(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField3)==uint8_t(expected_date.year>>8),
  "Mode4 runtime scratch follows the last real unplayed row date after suppressing header date");
 ui::LeaguePairingsScreen filtered_screen(data,filtered);const auto filter_json=filtered_screen.state_json();
 check(filter_json.find("\"source_round\":"+std::to_string(scheduled_row.source_round))!=std::string::npos&&
  filter_json.find("\"fixture_index\":"+std::to_string(scheduled_row.fixture_index))!=std::string::npos,
  "actual Mode4 UI diagnostics include each source round and fixture slot");
 c.flow.pointer_down(190,200);auto next=c.flow.click(190,200);
 check(next.league_pairings_action.event.kind==LeaguePairingsAction::next&&c.serve(next).empty()&&stable()&&
  c.flow.league_pairings()->state().source.owner.filter_offset==10&&c.flow.league_pairings()->state().identity.sequence==3,
  "mode4 red-arrow page ACK preserves live Native/W/G/revision");
 c.flow.pointer_down(10,180);auto reset=c.flow.click(10,180);
 check(reset.league_pairings_action.event.kind==LeaguePairingsAction::reset&&c.serve(reset).empty()&&stable()&&
  !c.flow.league_pairings()->state().source.owner.filter_active&&
  (c.flow.league_pairings()->state().source.preferences&0x0f)==1,
  "Paarungen button exits filter mode in the staged family without a public revision");
}
}

void postponed_app_ui(const fs::path& data,const fs::path& input) {
 Context c(data,input,true);const auto live=save_stream(c.session->state());const auto rt=c.session->club_modal_runtime_checkpoint();
 const auto W=c.session->rand();const auto G=c.session->game();
 check(c.flow.league_pairings()->state().source.owner.postponed_active&&c.flow.league_pairings()->state().rows.empty()&&
  c.flow.league_pairings()->state().subtitle.empty()&&c.flow.league_pairings()->state().date.empty(),
  "real staged Desk opens exact saved Mode8 with valid blank list/footer/header");
 const auto scratch=[&](uint32_t a){const auto d=c.session->league_pairings_checkpoint().draft.value();
  if(d.state.memory.has(a))return d.state.memory.u8(a);return d.runtime.at(a);};
 check(scratch(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField4)==0x77&&scratch(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField5)==0x77&&scratch(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField6)==0x66&&scratch(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField2)==0x55&&scratch(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField3)==0x44,
  "empty Mode8 skips header and row-date scratch conversion");
 const auto image=c.flow.render();
 if(c.flow.league_pairings()->state().decorated){const auto original_frame=load_vga(data/"KB"/"129.VGA");
  check(image.pixels[50*320+100]==original_frame.pixels[50*320+100],"empty Mode8 leaves original row panel/frame region untouched");}
 check(image.pixels==c.flow.render().pixels&&save_stream(c.session->state())==live&&
  c.session->club_modal_runtime_checkpoint()==rt&&W.seed()==c.session->rand().seed()&&G.seed()==c.session->game().seed(),
  "empty Mode8 render is stable and pure");
 auto before=c.flow.league_pairings()->state();ui::LeaguePairingsScreen screen(data,before);
 auto bad_offset=before;bad_offset.source.owner.postponed_offset=-10;bad_offset.source.page_offset=-10;
 rejects([&]{ui::LeaguePairingsScreen bad(data,bad_offset);},"forged negative Mode8 UI offset rejected");
 bad_offset=before;bad_offset.source.owner.postponed_offset=10;bad_offset.source.page_offset=10;
 rejects([&]{ui::LeaguePairingsScreen bad(data,bad_offset);},"forged empty Mode8 offset10 rejected");
 auto bad_mode=before;bad_mode.source.preferences=1;
 rejects([&]{ui::LeaguePairingsScreen bad(data,bad_mode);},"Mode8 projection with mismatched saved preference rejected");
 auto bad_search=before;bad_search.source.owner.search_first=-2;
 rejects([&]{ui::LeaguePairingsScreen bad(data,bad_search);},"Mode8 projection cannot carry Search sentinels");
 auto bad_filter=before;bad_filter.source.owner.filter_active=true;bad_filter.source.owner.filter_team=-2;
 rejects([&]{ui::LeaguePairingsScreen bad(data,bad_filter);},"Mode8 projection cannot carry Mode4 filter ownership");
 auto one_out_of_range=before;one_out_of_range.source.total_rows=1;one_out_of_range.source.owner.postponed_offset=10;
 one_out_of_range.source.page_offset=10;
 rejects([&]{ui::LeaguePairingsScreen bad(data,one_out_of_range);},"one-result Mode8 UI cannot claim an empty page at offset10");
 screen.pointer_down(10,220);check(!screen.pending(),"Nachholsp button DOWN waits for paired UP");screen.release(10,220);
 check(screen.pending()&&screen.pending()->event.kind==LeaguePairingsAction::postponed_list,
  "Original index3 Nachholsp button activates only on paired release");
 c.flow.pointer_down(10,220);auto click=c.flow.click(10,220);check(click.league_pairings_action.event.kind==LeaguePairingsAction::postponed_list&&
  c.serve(click).empty()&&save_stream(c.session->state())==live&&c.session->club_modal_runtime_checkpoint()==rt&&
  c.session->rand().seed()==W.seed()&&c.session->game().seed()==G.seed()&&
  c.flow.league_pairings()->state().source.owner.postponed_offset==0,"reselect Mode8 restarts at offset zero without live mutation");
 c.flow.pointer_down(10,180);auto reset=c.flow.click(10,180);check(reset.league_pairings_action.event.kind==LeaguePairingsAction::reset&&c.serve(reset).empty()&&
  !c.flow.league_pairings()->state().source.owner.postponed_active&&
  (c.flow.league_pairings()->state().source.preferences&15)==1,"Paarungen reset returns from Mode8 to normal list");
 Context single(data,input,true,true,true);const auto single_state=single.flow.league_pairings()->state();
 check(single_state.source.rows.size()==1&&single_state.source.rows[0].postponed_date==17&&
  single_state.rows[0].result=="18.07.2026"&&single_state.rows[0].result_x==255,
  "fixture66e5 day17 renders independent literal 18.07.2026 at x255");
 const auto single_draft=single.session->league_pairings_checkpoint().draft.value();const auto single_byte=[&](uint32_t a){
  return single_draft.state.memory.has(a)?single_draft.state.memory.u8(a):single_draft.runtime.at(a);};
 check(single_byte(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField4)==18&&single_byte(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField5)==7&&single_byte(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField6)==5&&
  single_byte(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField2)==234&&single_byte(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField3)==7&&single_byte(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField)==28,
  "fixture66e5 day17 sets literal Source scratch 18/7/5/234/7 and seasonal cycle28");
 Context row(data,input,true,true);const auto rowstate=row.flow.league_pairings()->state();
 check(rowstate.source.rows.size()==3&&rowstate.source.rows[0].postponed_queue_slot==38&&
  rowstate.source.rows[0].postponed_day==17&&rowstate.source.rows[1].postponed_queue_slot==39&&
  rowstate.source.rows[1].postponed_day==18&&!rowstate.source.rows[2].postponed_date,
  "App rows contain ordered real fixture/queue slots and an orphan without a queue date");
 check(rowstate.rows[0].result=="18.07.2026"&&rowstate.rows[0].result_x==255&&
  rowstate.rows[1].result=="19.07.2026"&&rowstate.rows[1].result_x==255&&rowstate.rows[2].result.empty()&&
  rowstate.rows[2].result_x==265&&rowstate.rows[0].scheduled_ordinal.empty()&&
  rowstate.rows[0].scheduled_date.empty()&&rowstate.rows[0].first_leg.empty(),
  "Mode8 literal display dates use x255 and omit ordinal/parentheses/first-leg while orphan stays blank");
 const auto rowdraft=row.session->league_pairings_checkpoint().draft.value();const auto rowbyte=[&](uint32_t a){return rowdraft.state.memory.has(a)?rowdraft.state.memory.u8(a):rowdraft.runtime.at(a);};
 check(rowbyte(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField4)==19&&rowbyte(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField5)==7&&rowbyte(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField6)==6&&rowbyte(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField2)==234&&
  rowbyte(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField3)==7&&rowbyte(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField)==28,"literal counter18 scratch is day19/month7/weekday6/year2026 and seasonal cycle28");
 const auto last_strength=team_average(rowdraft.state,rowstate.source.rows.back().away.runtime_team,0x1ff);
 check(rowbyte(::bmh::layout::CareerSession_FormatError_ExecutableField3)==uint8_t(last_strength.best_half_sum)&&rowbyte(::bmh::layout::CareerSession_FormatError_ExecutableField4)==uint8_t(last_strength.best_half_sum>>8),
  "last visible away team owns final Source strength scratch after orphan row");
 ui::LeaguePairingsScreen row_screen(data,rowstate);const auto json=row_screen.state_json();
 check(json.find("\"postponed_queue_slot\":38")!=std::string::npos&&
  json.find("\"source_round\":0")!=std::string::npos&&json.find("\"fixture_index\":2")!=std::string::npos,
  "Mode8 state JSON retains queue slots and actual Native fixture coordinates");
 auto initial_cp=row.session->league_pairings_checkpoint();
 auto rowreq=row.flow.pointer_down(5,29);check(rowreq.league_pairings_action.event.kind==LeaguePairingsAction::team_info&&
  rowreq.league_pairings_action.event.value==rowstate.source.rows[0].home.runtime_team,
  "Mode8 fixture LEFT-DOWN is immediate typed TeamInfo for visible home identity");
 check(!row.serve(rowreq).empty()&&same_pairings_checkpoint(row.session->league_pairings_checkpoint(),initial_cp),
  "missing TeamInfo presenter rolls back the entire Mode8 checkpoint");
 auto invalid_cp=initial_cp;invalid_cp.draft->owner.postponed_offset=-10;
 row.session->restore_league_pairings_checkpoint(invalid_cp);const auto before_bad_dispatch=row.session->league_pairings_checkpoint();
 ui::LeaguePairingsRequest bad_request{invalid_cp.draft->identity,{LeaguePairingsAction::reset}};
 const uint64_t oldrev=row.rev;std::string error;
 check(!row.session->dispatch_league_pairings(bad_request,row.rev,error)&&!error.empty()&&row.rev==oldrev&&
  same_pairings_checkpoint(row.session->league_pairings_checkpoint(),before_bad_dispatch),
  "stale Mode8 owner dispatch rejects without ACK/revision/checkpoint mutation");
 row.session->restore_league_pairings_checkpoint(initial_cp);
 auto rowreq_throw=row.flow.pointer_down(5,29);auto throwing=[&](app::ClubModalServices& svc,int team,const Image&){
  check(team==rowstate.source.rows[0].home.runtime_team,"Mode8 scoped TeamInfo receives actual clicked team");
  const auto range=squad_range(team);int n=0;while(n<int(range.count)&&(svc.state().squad_slots[range.first+size_t(n)]&0xfff)==0xfff)++n;
  check(n<int(range.count),"Mode8 clicked club has an actual player");
  svc.redraw_player({team,int(svc.state().squad_slots[range.first+size_t(n)]&0xfff),n,n});
  svc.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField,2);throw std::runtime_error("Mode8 declared presenter throw");return true;};
 check(!row.serve(rowreq_throw,throwing).empty()&&same_pairings_checkpoint(row.session->league_pairings_checkpoint(),initial_cp),
  "throw after nested Mode8 portrait G effects restores full transaction");
 const auto live_before=row.session->state();const auto runtime_before=row.session->club_modal_runtime_checkpoint();
 auto accepted_req=row.flow.pointer_down(5,29);auto accepted=[&](app::ClubModalServices& svc,int team,const Image&){
  check(team==rowstate.source.rows[0].home.runtime_team,"successful Mode8 nested presenter sees selected fixture side");
  const auto range=squad_range(team);int n=0;while(n<int(range.count)&&(svc.state().squad_slots[range.first+size_t(n)]&0xfff)==0xfff)++n;
  check(n<int(range.count),"Mode8 nested portrait player exists");
  svc.redraw_player({team,int(svc.state().squad_slots[range.first+size_t(n)]&0xfff),n,n});return true;};
 check(row.serve(accepted_req,accepted).empty()&&save_stream(row.session->state())==save_stream(live_before)&&
  row.session->club_modal_runtime_checkpoint()==runtime_before,"successful Mode8 nested presenter remains staged");
 auto after_nested=row.session->league_pairings_checkpoint();
 check(after_nested.draft->owner.postponed_active&&after_nested.draft->game.calls()>initial_cp.draft->game.calls()&&
  after_nested.draft->identity.sequence==initial_cp.draft->identity.sequence+1,
  "successful Mode8 portrait preserves owner and its actual staged G effects");
 const auto after_stale=row.session->league_pairings_checkpoint();
 check(!row.session->dispatch_league_pairings(accepted_req.league_pairings_action,row.rev,error)&&row.rev==oldrev&&
  same_pairings_checkpoint(row.session->league_pairings_checkpoint(),after_stale),
  "replayed Mode8 typed sequence is rejected with the accepted staged state unchanged");
 auto coherent=row.session->league_pairings_checkpoint();auto broken=coherent;++broken.draft->state.date;
 row.session->restore_league_pairings_checkpoint(broken);const auto failed_ack_state=row.session->league_pairings_checkpoint();
 auto failed_close=row.flow.pointer_down(200,100,true);
 check(!row.serve(failed_close).empty()&&row.rev==oldrev&&row.flow.screen_name()=="league_pairings"&&
  same_pairings_checkpoint(row.session->league_pairings_checkpoint(),failed_ack_state),
  "failed Mode8 close preparation/ACK restores full staged checkpoint and does not advance revision");
 row.session->restore_league_pairings_checkpoint(coherent);
 const auto& stage=*coherent.draft;Oracle close_oracle{stage.rand.seed(),stage.game.seed(),stage.rand.calls(),stage.game.calls()};close_oracle.stir();
 auto close_req=row.flow.pointer_down(200,100,true);
 check(row.serve(close_req).empty()&&row.rev==oldrev+1&&row.flow.screen_name()=="desktop"&&
  !row.session->league_pairings_owner(),"Mode8 right-DOWN closes once after valid staged return");
 check(row.session->game().seed()==close_oracle.g&&row.session->game().calls()==close_oracle.gc&&
  row.session->rand().seed()==close_oracle.w&&row.session->rand().calls()==close_oracle.wc,
  "final Mode8 exit stirs exactly once from accepted nested staged seeds");
 check(row.flow.click(200,100,true).request==ui::FlowRequest::none&&row.rev==oldrev+1,
  "Mode8 final right-UP is ignored after DOWN ownership");
}
int main(int argc,char** argv){try{check(argc==3,"data and Native fixture required");active_career_trigger(argv[1]);active_packet_core(argv[1],argv[2]);active_app(argv[1],argv[2],false);active_app(argv[1],argv[2],true);core(argv[1],argv[2]);national_source(argv[1]);national_app(argv[1]);computer_tip_core(argv[1],argv[2]);postponed_core(argv[1],argv[2]);app_ui(argv[1],argv[2]);computer_tip_app_ui(argv[1],argv[2]);search_core(argv[1],argv[2]);search_app_ui(argv[1],argv[2]);club_filter_core(argv[1],argv[2]);club_filter_app_ui(argv[1],argv[2]);postponed_app_ui(argv[1],argv[2]);std::cout<<"Source pairings/national/search/club-filter/postponed/Computer Tip Core/UI/App contracts passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
