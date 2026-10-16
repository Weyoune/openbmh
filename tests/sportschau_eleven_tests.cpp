// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <iostream>
#include "app/career_session.hpp"
#include "game/save.hpp"
#include "ui/sportschau.hpp"

using namespace bmh;
static void check(bool b,const char* message) {if(!b) throw std::runtime_error(message);}
static void word(Bytes& b,size_t at,uint16_t v) {b.at(at)=uint8_t(v);b.at(at+1)=uint8_t(v>>8);}

static void controlled(const std::filesystem::path& data) {
    GameState state;state.memory.add_block(::bmh::layout::FixtureSportschauOptionsBegin,200);
    state.manager_teams={0};state.manager_records.assign(0xb0c,0);
    state.manager_records[0x21e]=250;state.squad_slots.assign(168,0xfff);
    state.player_name_offsets.resize(11);state.club_ids.resize(7);
    for(size_t i=0;i<11;++i) {
        state.squad_slots[144+i]=uint16_t(i);
        state.player_name_offsets[i]=uint16_t(state.player_names.size());
        state.player_names.insert(state.player_names.end(),{'P',uint8_t('A'+i),0});
    }
    for(size_t i=0;i<7;++i) {state.club_ids[i]=uint16_t(state.club_names.size());state.club_names.insert(state.club_names.end(),{'C',uint8_t('A'+i),0});}
    MatchGroup group;group.desc.assign(0xd0,0);
    for(size_t i=0;i<11;++i) {group.desc[0x70+i]=uint8_t(5+i);word(group.desc,0x9c+i*2,uint16_t(144+i));}
    auto missing=group;missing.desc[0x7a]=0;
    check(!finish_sportschau_awards(state,missing,0) && state.manager_records[0x21e]==250,
          "one unfilled slot suppresses statistics and entire eleven viewer");
    const auto view=finish_sportschau_awards(state,group,0);
    check(view && state.manager_records[0x21e]==5,
          "Source statistic uses grade byte as squad slot, wraps eleven increments, independent of displayed club6");
    for(size_t i=0;i<11;++i) check(view->players[i].squad_slot==144+i && view->players[i].player_id==i &&
        view->players[i].team_id==6 && view->players[i].player_name==state.player_name(int(i)) &&
        view->players[i].club_name=="CG","display uses separate global slot words and Original player/club bytes");
    const auto before=state.manager_records;
    ui::SportschauEleven renderer(data);const auto frame=renderer.render(*view);
    const auto room=decode_vga(read_file(data/"KB/57.VGA"));
    const auto figures=decode_vga(read_file(data/"KB/16.VGA"));
    const auto exe=load_le(data/"BMH.exe");
    const int roles[11]={0,1,1,1,2,2,2,2,3,3,3};
    for(size_t i=0;i<11;++i) {
        const int role=roles[i];const int center=exe.u16(::bmh::layout::Sportschau_Render_RuntimeField5+uint32_t(i*2));
        const int baseline=exe.u8(::bmh::layout::Sportschau_Render_RuntimeField2+uint32_t(role))+(i==5||i==6?15:0);
        for(int y=0;y<31;++y) for(int x=0;x<33;++x)
            check(frame.pixels[size_t(baseline-32+y)*320+size_t(center-14+x)]==
                figures.pixels[size_t(y)*size_t(figures.width)+size_t(exe.u8(::bmh::layout::Sportschau_Render_RuntimeField4+uint32_t(role))+x)],
                "all eleven Source figure rectangles, including staggered middle pair, are pixel-exact");
    }
    check(frame.pixels[10*320+310]==room.pixels[10*320+310] && state.manager_records==before,
          "renderer retains Source backdrop and never writes engine statistics");
    auto empty=*view;for(auto& p:empty.players) {p.player_name.clear();p.club_name.clear();}
    auto long_name=empty;long_name.players[0].player_name=std::string(400,'A');
    check(renderer.render(empty).pixels==renderer.render(long_name).pixels,"out-of-viewport name rejects whole string, preserving Source figures");
    state.memory.set8(::bmh::layout::SportschauAwards_FinishSportschauAwards_RuntimeField,1);
    check(!finish_sportschau_awards(state,group,0) && state.manager_records[0x21e]==16,
          "G7 hides viewer after Source statistic, rather than suppressing counter");
    state.memory.set8(::bmh::layout::SportschauAwards_FinishSportschauAwards_RuntimeField,0);state.memory.set8(::bmh::layout::Controller_FormatError_ExecutableField2,1);
    check(!finish_sportschau_awards(state,group,0) && state.manager_records[0x21e]==27,
          "competition C4 also gates presentation after statistics");
}

static void natural(const std::filesystem::path& data,const std::filesystem::path& input) {
    app::CareerSession plain(data,8128,input),observed(data,8128,input);
    ui::SportschauEleven screen(data);int callbacks=0;
    SportschauPresentationHooks hooks;
    hooks.awards=[&](const GameState& state,const SportschauAwardsView& view) {
        const auto bytes=save_stream(state);const auto w=observed.rand();const auto g=observed.game();
        check(view.group==2 && !view.competition_title.empty(),"normal completed league owns eleven and Source footer title");
        check(screen.render(view).pixels.size()==320*240,"normal eleven renders complete Original-sized frame");
        check(save_stream(state)==bytes && observed.rand().seed()==w.seed() && observed.rand().calls()==w.calls() &&
              observed.game().seed()==g.seed() && observed.game().calls()==g.calls(),"award observer is Native/RNG neutral");++callbacks;
    };
    observed.set_sportschau_presentation_hooks(hooks);
    uint64_t a=1,b=1;std::string error;ui::DeskRequest req;req.kind=ui::DeskRequestKind::advance_turn;req.manager_slot=0;req.revision=1;
    check(plain.dispatch_kickoff(req,a,error).kind==TurnDeskKind::NextDesk && error.empty(),"normal no-UI turn completes");
    check(observed.dispatch_kickoff(req,b,error).kind==TurnDeskKind::NextDesk && error.empty() && callbacks==1,"one real completed eleven is presented");
    check(save_stream(plain.state())==save_stream(observed.state()) && plain.rand().seed()==observed.rand().seed() &&
          plain.rand().calls()==observed.rand().calls() && plain.game().seed()==observed.game().seed() &&
          plain.game().calls()==observed.game().calls(),"complete normal Native and RNG state independent of awards viewer");
}
int main(int argc,char** argv) {
    try {check(argc==3,"German data and normal save required");controlled(argv[1]);natural(argv[1],argv[2]);
        std::cout<<"sportschau_eleven_tests: Source completion/counter/gates, identities, eleven pixel rectangles and real career passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
