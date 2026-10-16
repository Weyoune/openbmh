// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "app/career_session.hpp"
#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/vga.hpp"
#include "game/messages.hpp"
#include "game/save.hpp"
#include "ui/sportschau.hpp"
#include <iostream>
#include <stdexcept>
using namespace bmh;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(int argc,char**argv){try{
 check(argc==3,"winner tests data real-last-league-save");const std::filesystem::path data=argv[1],input=argv[2];
 app::CareerSession plain(data,8128,input),observed(data,8128,input);auto& g=observed.state();
 // Isolate the final-event presenter from F6's following calendar search.
 plain.state().memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField,1);g.memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField,1);
 const auto exe=load_le(data/"BMH.exe");const auto font=load_font(data/"F/BMH2.FNT");
 ui::CompetitionWinnerScreen screen(data);const auto before=save_stream(g);
 for(int title=0;title<=8;++title){
  Image expected=load_vga(data/"KB"/(title==7?"131.VGA":"128.VGA"));
  auto bottom=load_vga(data/"KB"/(title==7?"47.VGA":"40.VGA"));
  for(int y=0;y<139;++y)for(int x=0;x<320;++x)expected.at(x,y+101)=bottom.at(x,y);
  if(title!=0&&title!=7){auto trophy=load_vga(data/"KB"/(std::to_string(title+40)+".VGA"));
   for(int y=0;y<98;++y)for(int x=0;x<79;++x)expected.at(x+64,y+60)=trophy.at(x,y);}
  auto caption=message_text(g,::bmh::layout::Sportschau_Render_TextPointer3)+exe.cstring(::bmh::layout::Sportschau_Render_TextPointer)+message_text(g,
   (exe.u8(::bmh::layout::SeasonEntrants_FormatError_TextPointer+title)?::bmh::layout::CupDraw_FormatError_ExecutableField6 : ::bmh::layout::SeasonEntrants_FormatError_TextPointer2)+uint32_t(exe.u8(::bmh::layout::SeasonEntrants_FormatError_RuntimeField+title))*4);
  auto club=g.club_name(14);const uint8_t color=title==7?0x76:0x92;
  // Source calls have four different centering bounds and two baselines.
  font.draw(expected,158-font.text_width(caption)/2,224-font.baseline,caption,0);
  font.draw(expected,159-font.text_width(caption)/2,225-font.baseline,caption,color);
  font.draw(expected,159-font.text_width(club)/2,235-font.baseline,club,0);
  font.draw(expected,160-font.text_width(club)/2,236-font.baseline,club,color);
  check(screen.render(g,{uint8_t(title),14}).pixels==expected.pixels,"Original title artwork, font2, color and four label positions");
 }
 check(save_stream(g)==before,"winner projection is read-only");
 int calls=0;SportschauPresentationHooks hooks;
 hooks.winner=[&](const GameState& state,const CompetitionWinnerView& event){
  ++calls;check(event.title==0&&event.team<90,"natural final league round produces its real German champion");
  auto native=save_stream(state);check(screen.render(state,event).pixels.size()==320*240,"real winner renders");
  check(save_stream(state)==native,"winner observer does not mutate Native");
 };
 observed.set_sportschau_presentation_hooks(hooks);
 auto run=[](app::CareerSession& session){uint64_t revision=1;std::string error;ui::DeskRequest r;
  r.kind=ui::DeskRequestKind::advance_turn;r.manager_slot=session.state().memory.u8(::bmh::layout::ActiveManager);r.revision=revision;
  auto result=session.dispatch_kickoff(r,revision,error);check(error.empty()&&result.kind==TurnDeskKind::NextDesk,"real final league turn finishes");};
 run(plain);run(observed);check(calls==1,"one genuine championship viewer");
 check(save_stream(plain.state())==save_stream(observed.state())&&plain.rand().seed()==observed.rand().seed()&&
  plain.rand().calls()==observed.rand().calls()&&plain.game().seed()==observed.game().seed()&&
  plain.game().calls()==observed.game().calls(),"winner callback preserves whole Native and both RNG trajectories");
 app::CareerSession hidden(data,8128,input);hidden.state().memory.set8(::bmh::layout::Cup_AdvanceCup_RuntimeField,1);calls=0;
 hidden.state().memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField,1);
 hidden.set_sportschau_presentation_hooks(hooks);run(hidden);check(calls==0,"I7 off suppresses winner view");
 std::cout<<"winner source/artwork/natural caller/Native+W+G checks passed\n";return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
