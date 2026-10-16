// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <iostream>
#include "app/career_session.hpp"
#include "game/matchday.hpp"
#include "game/save.hpp"
#include "ui/live_matchday.hpp"
#include "ui/sportschau.hpp"

using namespace bmh;
static void check(bool b,const char* message) { if(!b) throw std::runtime_error(message); }
static void same_random(const WatcomRandom& a,const WatcomRandom& b) {
    check(a.seed()==b.seed()&&a.calls()==b.calls(),"Source scene draw sequence matches existing animation functions");
}

static void controlled(const std::filesystem::path& data,const std::filesystem::path& input) {
    app::CareerSession session(data,8128,input);
    auto base=session.state();
    base.memory.set8(::bmh::layout::ReplayProgressionMode,0);base.memory.set8(::bmh::layout::ReplayChanceMode,0);base.memory.set8(::bmh::layout::Controller_FormatError_RuntimeField3,0);
    base.memory.set8(::bmh::layout::Controller_FormatError_ExecutableField2+2*9,0);base.memory.set8(::bmh::layout::CareerSession_FormatError_RuntimeField,1);
    base.sportschau_selection_available=true;base.sportschau_homes[0]=0;
    MatchGroup group;group.desc.assign(0xd0,0);group.desc[0xcc]=2;
    group.fixtures={0,1};group.results={0x21};group.end_minute={17};
    group.log={0x0004,12,0x0fff,0,0x1005,uint16_t((1<<7)|4),0x0fff,uint16_t(1<<7),
               0x0006,uint16_t((1<<7)|16),0x7007,uint16_t(1<<7)};
    const auto view=sportschau_fixture_view(base,group,0,0,{});
    check(view.source_selected,"controlled Source owner is selected");
    auto plain=base,observed=base;
    WatcomRandom expected(8128),actual(8128),no_ui(8128);GameRandom eg(123),ag(123),ng(123);
    std::vector<MatchGroup> expected_groups{group};std::vector<uint8_t> refs;
    MatchEngine engine{base,expected_groups,eg,expected,refs};
    engine.shot_animation(false,false,0,0,0,12);
    engine.shot_animation(true,false,1,0,0,4);
    engine.set_piece_animation(1,0,0,0,0,16);
    engine.shot_animation(false,false,1,0,0,16);
    present_sportschau_replay(plain,group,view,ng,no_ui,{});
    ui::LiveMatchdayScreen renderer(data);int scenes=0,frames=0,set_pieces=0;
    SportschauReplayHooks hooks;
    hooks.scene=[&](const GameState& state,const SportschauFixtureView&,const SportschauSceneView& scene) {
        const auto before=save_stream(state);const auto before_w=actual;const auto before_g=ag;
        check(scene.home_goals_before==(scenes?1:0)&&scene.away_goals_before==0,
              "scene header keeps previous score, excludes current goal and transient miss");
        check(scene.shot.miss==(scenes==1),"Source log controls goal versus miss");
        LiveChanceEvent e;e.group=0;e.match=scene.shot.match;e.side=scene.shot.acting_side;
        const auto header=ui::live_chance_projection(e,scene.groups,state,load_le(data/"BMH.exe"));
        const auto backdrop=renderer.render_sportschau_header(header,scene.home_goals_before,scene.away_goals_before,false);
        const auto movie=renderer.render_shot({},scene.shot,&backdrop);frames+=int(movie.size());
        for(const auto& frame:movie) for(int y=0;y<240;++y) for(int x=0;x<320;++x)
            if(x<64||x>=256||y<2||y>=97) check(frame.pixels[size_t(y)*320+size_t(x)]==
                backdrop.pixels[size_t(y)*320+size_t(x)],"shot retains Source scene backdrop outside viewport");
        if(scene.set_piece) {++set_pieces;check(scene.set_piece->kind==1&&scene.set_piece->source_side==0,"away penalty mirrors Source side");
            check(!renderer.render_set_piece({},*scene.set_piece,&backdrop).empty(),"owned penalty draws reconstruct figures");}
        same_random(before_w,actual);check(before_g.seed()==ag.seed()&&before_g.calls()==ag.calls()&&before==save_stream(state),
            "scene renderer does not mutate Native or either RNG");++scenes;
    };
    present_sportschau_replay(observed,group,view,ag,actual,hooks);
    same_random(expected,actual);same_random(no_ui,actual);
    check(scenes==3&&frames==54&&set_pieces==1&&save_stream(plain)==save_stream(observed)&&
          save_stream(base)==save_stream(observed)&&ng.calls()==0&&ag.calls()==0,"three scoped scenes with and without observers are equivalent");
    WatcomRandom skipped(8128);GameRandom sg(123);SportschauReplayHooks skip;
    skip.step=[](const GameState&,const SportschauFixtureView&,const SportschauReportStep& s){return s.minute_wait&&!s.terminal_wait;};
    present_sportschau_replay(observed,group,view,sg,skipped,skip);
    check(skipped.calls()==0,"left=2 before first chance suppresses future scene draws");
    auto disabled=view;disabled.replay_chance_mode=1;WatcomRandom off(8128);
    present_sportschau_replay(observed,group,disabled,sg,off,{});check(off.calls()==0,"E7 disabled keeps old no-scene RNG path");
    disabled=view;disabled.source_selected=false;
    present_sportschau_replay(observed,group,disabled,sg,off,{});check(off.calls()==0,"unselected fixture cannot generate replay scenes");
}

static void natural(const std::filesystem::path& data,const std::filesystem::path& input) {
    app::CareerSession plain(data,8128,input),observed(data,8128,input);
    plain.state().memory.set8(::bmh::layout::ReplayChanceMode,0);observed.state().memory.set8(::bmh::layout::ReplayChanceMode,0);
    int scenes=0;SportschauPresentationHooks hooks;
    hooks.replay.scene=[&](const GameState&,const SportschauFixtureView& view,const SportschauSceneView& scene) {
        check(view.source_selected&&scene.groups.size()==1&&scene.shot.group==0,"real Core scene remains in its selected fixture scope");++scenes;
    };
    observed.set_sportschau_presentation_hooks(hooks);
    uint64_t pr=1,orr=1;ui::DeskRequest request;request.kind=ui::DeskRequestKind::advance_turn;request.manager_slot=0;request.revision=1;
    std::string error;const auto a=plain.dispatch_kickoff(request,pr,error);check(error.empty()&&a.kind==TurnDeskKind::NextDesk,"real no-UI Source turn completes");
    const auto b=observed.dispatch_kickoff(request,orr,error);check(error.empty()&&b.kind==a.kind&&scenes>0,"real observed Source turn generates scenes");
    check(save_stream(plain.state())==save_stream(observed.state())&&plain.game().seed()==observed.game().seed()&&
          plain.game().calls()==observed.game().calls(),"real scene observers preserve complete Native and game RNG");
    same_random(plain.rand(),observed.rand());
}

int main(int argc,char** argv) {
    try {check(argc==3,"data and normal Sportschau save required");controlled(argv[1],argv[2]);natural(argv[1],argv[2]);
        std::cout<<"sportschau_scene_tests: Source owner, exact draws, goal/miss/penalty, viewport, skip and real career neutrality passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
