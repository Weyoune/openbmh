#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <cstdio>
#include <stdexcept>
#include "formats/fbm.hpp"
#include "formats/textd.hpp"
#include "game/match_engine.hpp"
#include "game/save.hpp"
using namespace bmh;
namespace {
void check(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
MatchGroup fixture() {
    MatchGroup g;g.desc.resize(0xd0);g.desc[1]=2;g.desc[2]=1;
    g.fixtures={0,1};g.results={0};g.end_minute={0};g.spectators={12000};
    g.records={70,70,90,90,90,90,80,60,3,70,70,90,90,90,90,80,60,3};return g;
}
void configure(MatchEngine& engine,const LeImage& exe) {
    engine.reality_rules=&exe;engine.live_scene_visible=true;engine.leave_screens=true;
    engine.buttons.assign(0xb4,0);
}
}
int main(int argc,char** argv) { try {
    check(argc==2,"usage: reality_matchday_tests <Original data>");
    const auto data=std::filesystem::path(argv[1]);const auto exe=bmh::test::load_executable(data/"BMH.exe");
    const auto texts=load_textd(data/"TEXTD.BLK");
    const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
    auto base=GameState::from_save(SaveState(load_savegame(data/"AUTOSAVE.FBM").stream,counts),counts);
    base.texts=&texts;base.manager_teams.clear();base.live_matches={0};
    base.team_morale[0]=base.team_morale[1]=128;
    for (int side=0;side<2;++side) for (int row=0;row<24;++row) {
        const int pid=side*24+row;
        base.squad_slots[size_t(pid)]=uint16_t(pid|(row<11?kSlotInLineup:row<17?0x4000:0));
        base.players[size_t(pid)].w35c=48u|(100u<<7)|(100u<<14);
        base.players[size_t(pid)].w364=uint32_t(row==0?0:1+row%3);
        base.players[size_t(pid)].w368=3u|(3u<<4)|(3u<<6);
    }
    int scenes=0,rounds=0,changed_sides=0,attempts=0,restarts=0,forced=0;
    for (uint32_t seed=1;seed<=256;++seed) {
        auto shown=base,hidden=base;std::vector<MatchGroup> a{fixture()},b{fixture()};
        WatcomRandom aw(seed),bw(seed);GameRandom ag(seed*17),bg(seed*17);
        MatchEngine visible(shown,a,ag,aw,{27}),silent(hidden,b,bg,bw,{27});
        configure(visible,exe);configure(silent,exe);
        bool active=false;int scene_rounds=0;
        visible.live_reality=[&](const LiveRealityEvent& e) {
            check(e.minute==42&&e.group==0&&e.match==0,"actual minute/fixture identity");
            if (e.stage==LiveRealityStage::begin) {check(!active,"no nested Scene");active=true;scene_rounds=0;++scenes;}
            else if(e.stage==LiveRealityStage::round) {
                check(active&&int(e.tick.round)==scene_rounds++,"rounds run in Source order");++rounds;
                if(e.tick.motion)check(e.tick.motion->frames.size()==11,"eleven owned Source frames");
            } else if(e.stage==LiveRealityStage::end) {
                check(active&&e.tick.finished,"Scene completion follows real rounds");active=false;
                changed_sides+=(e.tick.result.return_flags&0x80)!=0;
                attempts+=(e.tick.result.return_flags&1)!=0;
                restarts+=e.tick.result.restart!=0;
                forced+=e.tick.result.stop==RealitySceneStop::forced_event;
            }
            return false;
        };
        const auto ar=visible.minute(42,90,5),br=silent.minute(42,90,5);
        check(!ar.unsupported&&!br.unsupported,ar.unsupported?ar.reason.c_str():br.reason.c_str());
        check(!active,"every real Scene and deferred tail completes");
        check(save_stream(shown)==save_stream(hidden)&&shown.reality==hidden.reality&&
            a[0].results==b[0].results&&a[0].records==b[0].records&&a[0].log==b[0].log&&a[0].desc==b[0].desc,
            "observation cannot affect Native, runtime residue, ratings, logs or score");
        check(aw.seed()==bw.seed()&&aw.calls()==bw.calls()&&ag.seed()==bg.seed()&&ag.calls()==bg.calls(),
            "Source RNG is identical with and without Reality presentation");
    }
    std::printf("COVERAGE scenes=%d rounds=%d handoffs=%d shots=%d restarts=%d cards=%d\n",scenes,rounds,changed_sides,attempts,restarts,forced);
    check(scenes&&rounds&&changed_sides&&attempts&&restarts&&forced,"natural engine seeds reach Source shots, possession changes, restarts and real fixed cards");
    int exits=0;
    for (uint32_t seed=1;seed<=32;++seed) {
        auto state=base;std::vector<MatchGroup> groups{fixture()};WatcomRandom w(seed);GameRandom g(seed*17);
        MatchEngine engine(state,groups,g,w,{27});configure(engine,exe);
        engine.live_reality=[&](const LiveRealityEvent& e) {
            if (e.stage==LiveRealityStage::round) {check(e.tick.round==0,"physical exit completes current animation, never a second round");++exits;return true;}
            return false;
        };
        auto report=engine.minute(42,90,5);check(!report.unsupported,report.reason.c_str());
    }
    check(exits,"exit qualification reaches genuine engine Scene");
    // Source eligibility must not change the legacy indoor or ticker paths.
    for (int mode=0;mode<2;++mode) {
        auto astate=base,bstate=base;
        if (mode==1) astate.team_morale[0]=astate.team_morale[1]=bstate.team_morale[0]=bstate.team_morale[1]=64;
        std::vector<MatchGroup> a{fixture()},b{fixture()};WatcomRandom aw(42),bw(42);GameRandom ag(17),bg(17);
        MatchEngine candidate(astate,a,ag,aw,{27}),prior(bstate,b,bg,bw,{27});
        configure(candidate,exe);configure(prior,exe);prior.live_scene_visible=false;
        candidate.indoor=prior.indoor=mode==0;int callbacks=0;
        candidate.live_reality=[&](const LiveRealityEvent&){++callbacks;return false;};
        auto ar=candidate.minute(42,90,5),br=prior.minute(42,90,5);
        check(!ar.unsupported&&!br.unsupported&&!callbacks&&save_stream(astate)==save_stream(bstate)&&
            a[0].log==b[0].log&&aw.seed()==bw.seed()&&ag.seed()==bg.seed(),"indoor and ticker remain draw-for-draw legacy path");
    }
    std::printf("PASS: real MatchEngine minute() Reality scenes=%d rounds=%d handoffs=%d shots=%d restarts=%d cards=%d exits=%d; identical observer-free Native/runtime/RNG, current-round exit and legacy indoor/ticker\n",scenes,rounds,changed_sides,attempts,restarts,forced,exits);
    return 0;
} catch (const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;} }
