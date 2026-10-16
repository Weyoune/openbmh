// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <iostream>
#include "app/career_session.hpp"
#include "game/save.hpp"
#include "ui/sportschau.hpp"

using namespace bmh;
static void check(bool b, const char* why) { if (!b) throw std::runtime_error(why); }
static void dword(Bytes& b, size_t at, uint32_t v) {
    for (size_t i = 0; i < 4; ++i) b.at(at+i) = uint8_t(v >> (8*i));
}
static void controlled(const std::filesystem::path& data) {
    GameState state; state.memory.add_block(::bmh::layout::FixtureSportschauOptionsBegin, 200);
    MatchGroup group; group.desc.assign(0xd0, 0);
    const std::array<int32_t,4> numbers{23,5,52600,22};
    for (size_t i = 0; i < 4; ++i) dword(group.desc,0xbc+i*4,uint32_t(numbers[i]));
    const auto view = sportschau_team_info_view(state,group,2);
    check(view && view->group==2 && view->totals==numbers,"exact descriptor counters, independent of award completeness");
    state.memory.set8(::bmh::layout::Controller_FormatError_RuntimeField2,1);
    check(sportschau_team_info_view(state,group,2)->source_backdrop_available,"C7 hides animation but still loads KB57");
    state.memory.set8(::bmh::layout::Controller_FormatError_RuntimeField,1);
    auto black = sportschau_team_info_view(state,group,2);
    check(black && !black->source_backdrop_available,"B7 suppresses background load without suppressing H7");
    ui::SportschauTeamInfo screen(data);
    const auto frame = screen.render(*view);
    const auto exe = load_le(data/"BMH.exe"); const auto atlas = load_vga(data/"KB/59.VGA");
    for (size_t i = 0; i < 4; ++i) {
        const int x=exe.u8(::bmh::layout::Sportschau_Render_TextPointer2+uint32_t(i)), y=exe.u8(::bmh::layout::Sportschau_Render_RuntimeField+uint32_t(i));
        for (int yy=0; yy<38; ++yy) for (int xx=0; xx<38; ++xx)
            check(frame.pixels[size_t(y+yy)*320+size_t(x+xx)]==atlas.pixels[size_t(yy)*152+i*38+size_t(xx)],
                  "four complete opaque Original icon rectangles");
        check(frame.pixels[size_t(y+38)*320+size_t(x+2)]==0x40 &&
              frame.pixels[size_t(y+2)*320+size_t(x+38)]==0x40,"Original two-pixel bottom/right shadows");
        // Independently walk the decimal string left-to-right, checking every
        // glyph pixel against the real KB59 atlas and the preceding backdrop.
        auto empty=*view; empty.totals[i]=-1;
        const auto under=screen.render(empty); const auto digits=std::to_string(numbers[i]);
        for (size_t d=0; d<digits.size(); ++d) for(int yy=0;yy<18;++yy) for(int xx=0;xx<11;++xx) {
            const size_t at=size_t(y+13+yy)*320+size_t(x+51+int(d)*12+xx);
            const auto pixel=atlas.pixels[size_t(38+yy)*152+size_t(digits[d]-'0')*11+size_t(xx)];
            check(frame.pixels[at]==(pixel ? pixel : under.pixels[at]),"real atlas digits, +1 bearing, twelve-pixel spacing and zero transparency");
        }
    }
    check(screen.render(*black).pixels[10*320+310]==0,"Source null-background fallback is black");
    ui::SportschauEleven eleven(data); SportschauAwardsView award; award.source_backdrop_available=false;
    check(eleven.render(award).pixels[10*320+310]==0,"same Source fallback applies to Elf des Tages");
    auto zero=*view;zero.totals.fill(0);check(screen.render(zero).pixels!=screen.render(*view).pixels,"zero is a visible digit");
    zero.totals.fill(INT32_MAX);check(screen.render(zero).pixels.size()==320*240,"maximum signed Source counter fits viewport");
    state.memory.set8(::bmh::layout::SportschauAwards_SportschauTeamInfoView_RuntimeField,1);check(!sportschau_team_info_view(state,group,2),"H7 off hides totals");
    state.memory.set8(::bmh::layout::SportschauAwards_SportschauTeamInfoView_RuntimeField,0);state.memory.set8(::bmh::layout::Controller_FormatError_ExecutableField2,1);
    check(!sportschau_team_info_view(state,group,2),"C4 off hides competition totals");
}
static void natural(const std::filesystem::path& data,const std::filesystem::path& input) {
    app::CareerSession plain(data,8128,input), observed(data,8128,input);
    ui::SportschauTeamInfo screen(data);int calls=0;std::vector<std::string> order;
    SportschauPresentationHooks hooks;
    hooks.awards=[&](const GameState&,const SportschauAwardsView&){order.push_back("eleven");};
    hooks.team_info=[&](const GameState& state,const SportschauTeamInfoView& view) {
        const auto before=save_stream(state);const auto w=observed.rand();const auto g=observed.game();
        check(view.group==2 && !view.competition_title.empty(),"real Source competition title/group");
        check(view.totals[0]>0 && view.totals[2]>0 && view.totals[3]>0,"natural matchday counters are accumulated");
        check(screen.render(view).pixels.size()==320*240 && save_stream(state)==before &&
              observed.rand().seed()==w.seed() && observed.rand().calls()==w.calls() &&
              observed.game().seed()==g.seed() && observed.game().calls()==g.calls(),"viewer is Native/RNG neutral");
        order.push_back("info");++calls;
    };
    observed.set_sportschau_presentation_hooks(hooks);
    uint64_t a=1,b=1;std::string error;ui::DeskRequest req;
    req.kind=ui::DeskRequestKind::advance_turn;req.manager_slot=0;req.revision=1;
    check(plain.dispatch_kickoff(req,a,error).kind==TurnDeskKind::NextDesk && error.empty(),"normal no-UI career turn");
    check(observed.dispatch_kickoff(req,b,error).kind==TurnDeskKind::NextDesk && error.empty() && calls==1 &&
          order==std::vector<std::string>{"eleven","info"},"totals follow real eleven before leaving competition");
    const auto& group=observed.last_turn_results().groups.at(2);
    const auto view=sportschau_team_info_view(observed.state(),group,2);
    check(view && view->totals[3]==int32_t(le32(group.desc,0xc8)),"final totals retain exact descriptor value");
    check(save_stream(plain.state())==save_stream(observed.state()) && plain.rand().seed()==observed.rand().seed() &&
          plain.rand().calls()==observed.rand().calls() && plain.game().seed()==observed.game().seed() &&
          plain.game().calls()==observed.game().calls(),"whole Native/W/G identical with and without totals observer");
}
int main(int argc,char** argv) {
    try {check(argc==3,"data and normal save required");controlled(argv[1]);natural(argv[1],argv[2]);
        std::cout<<"sportschau_teaminfo_tests: Source counters/gates, atlas geometry, fallback and normal career passed\n";return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
