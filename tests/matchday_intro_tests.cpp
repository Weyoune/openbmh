// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <iostream>
#include "app/career_session.hpp"
#include "game/messages.hpp"
#include "game/save.hpp"
#include "game/season.hpp"
#include "ui/sportschau.hpp"
#include "ui/live_reality.hpp"

using namespace bmh;
static void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static MatchGroup group(uint8_t type,uint32_t id=0){MatchGroup g;g.desc.assign(0xd0,0);g.desc[0]=type;
    for(size_t i=0;i<4;++i)g.desc[0xcc+i]=uint8_t(id>>(8*i));return g;}
static void projection_and_pixels(const std::filesystem::path& data,const std::filesystem::path& input) {
    app::CareerSession session(data,8128,input);auto& state=session.state();
    state.memory.set8(::bmh::layout::SportschauAwards_MatchdayIntroView_RuntimeField,0);
    const std::vector<MatchGroup> groups{group(0,0),group(0,1),group(2),group(2),group(3),group(2),group(12)};
    const auto native=save_stream(state);auto view=matchday_intro_view(state,groups);
    check(view&&view->competition_names.size()==6,"each league and only adjacent repeated cup types suppressed");
    check(view->competition_names[0]==league_name(state,0)&&view->competition_names[1]==league_name(state,1)&&
          view->competition_names[2]==message_text(state,::bmh::layout::EuropeNameTable)&&
          view->competition_names[5]==message_text(state,::bmh::layout::MatchdayResults_CompetitionName_TextPointer),
          "Source league/cup/indoor caption selection");
    ui::MatchdayIntro screen(data);const auto frame=screen.render(*view);
    auto expected=load_vga(data/"KB/136.VGA");const auto font=load_font(data/"F/BMH2.FNT");
    for(size_t i=0;i<view->competition_names.size();++i){const auto& text=view->competition_names[i];
        font.draw(expected,135,53+int(i)*9-font.baseline,text,0x76);
        font.draw(expected,134,52+int(i)*9-font.baseline,text,0x44);}
    check(frame.pixels==expected.pixels&&save_stream(state)==native,"Original KB136, font2, shadow/main coordinates and read-only projection");
    check(matchday_intro_view(state,{group(0,5)})->competition_names[0]==
          message_text(state,::bmh::layout::SportschauAwards_MatchdayIntroView_TextPointer)+state.club_name(0x166),"format field secondary-league prefix");
    check(!matchday_intro_view(state,{}),"no group produces no intro");
    state.memory.set8(::bmh::layout::SportschauAwards_MatchdayIntroView_RuntimeField,1);check(!matchday_intro_view(state,groups),"N7 suppresses intro");

    // G8's consumer already implements format field's KB50 strip replacement.
    // Check every ball pixel independently against the untouched Original atlas.
    const auto atlas=load_vga(data/"KB/50.VGA");Image under(320,240,90);
    RealityMotionFrame motion;motion.count=1;motion.sprites[0]={23,20,20,25,0,false};
    std::array<Image,4> frames;
    for(int style=0;style<4;++style){ui::RealityFieldScreen field(data,uint8_t(style));frames[size_t(style)]=field.render(motion,under);
        for(int y=0;y<11;++y)for(int x=0;x<16;++x){const int sx=144+x,sy=16+y;
            const uint8_t pixel=sy<24?0:atlas.pixels[size_t(style?15+(style-1)*3+(sy-24):sy)*251+size_t(sx)];
            check(frames[size_t(style)].pixels[size_t(149+y)*320+size_t(20+x)]==(pixel?pixel:90),"ball pixels match selected Original strip with zero transparency");}}
    check(frames[0].pixels!=frames[1].pixels&&frames[1].pixels!=frames[2].pixels&&frames[2].pixels!=frames[3].pixels,
          "all four legal G8 values have distinct visible ball pixels");
}
static void natural(const std::filesystem::path& data,const std::filesystem::path& input) {
    app::CareerSession plain(data,8128,input),observed(data,8128,input);int calls=0;
    // One-event intro neutrality; F6-on also simulates intervening AI events.
    plain.state().memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField,1);observed.state().memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField,1);
    ui::MatchdayIntro screen(data);SportschauPresentationHooks hooks;
    hooks.kickoff=[&](const GameState& state,const MatchdayIntroView& view){
        const auto native=save_stream(state);const auto w=observed.rand();const auto g=observed.game();
        check(!view.competition_names.empty()&&screen.render(view).pixels.size()==320*240,"normal pre-play intro projection");
        check(save_stream(state)==native&&observed.rand().calls()==w.calls()&&observed.game().calls()==g.calls(),"intro observer preserves Native and RNG");++calls;};
    observed.set_sportschau_presentation_hooks(hooks);ui::DeskRequest r;r.kind=ui::DeskRequestKind::advance_turn;r.manager_slot=0;r.revision=1;
    uint64_t a=1,b=1;std::string error;
    check(plain.dispatch_kickoff(r,a,error).kind==TurnDeskKind::NextDesk&&error.empty(),"normal no-UI turn");
    check(observed.dispatch_kickoff(r,b,error).kind==TurnDeskKind::NextDesk&&error.empty()&&calls==1,"one normal intro before matchday");
    check(save_stream(plain.state())==save_stream(observed.state())&&plain.rand().seed()==observed.rand().seed()&&
          plain.rand().calls()==observed.rand().calls()&&plain.game().seed()==observed.game().seed()&&plain.game().calls()==observed.game().calls(),
          "complete natural turn is intro-observer neutral");
}
int main(int argc,char** argv){try{check(argc==3,"data and normal save required");projection_and_pixels(argv[1],argv[2]);natural(argv[1],argv[2]);
    std::cout<<"matchday_intro_tests: Source list/KB136, N7, four ball styles and normal turn passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
