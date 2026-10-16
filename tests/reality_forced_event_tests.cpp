#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include "formats/fbm.hpp"
#include "formats/le.hpp"
#include "game/match.hpp"
#include "game/match_engine.hpp"
#include "game/reality_duel.hpp"
#include "game/save.hpp"

using namespace bmh;
namespace {
void check(bool ok,const char* text) { if (!ok) throw std::runtime_error(text); }
MatchGroup fixture() {
    MatchGroup g;g.desc.resize(0xd0);g.fixtures={0,1};g.results={0};g.spectators={12000};
    g.records={70,70,90,90,90,90,80,60,3,70,70,90,90,90,90,80,60,3};return g;
}
// Source prefix oracle: strict referee still draws W(0,1); mode1 starts with
// W(0,2), then red/duration. Fixed slots skip W(0,11), but keeper rejection
// repeats the balance/keeper rolls on the same player (legacy routine).
struct Prefix { int kind=-1,ban=0;bool change=false,second=false; };
Prefix prefix(WatcomRandom& w,bool keeper,bool booked,bool active) {
    Prefix out;if(w.range(0,1))return out;
    if(!w.range(0,2))return out;
    const bool red=w.range(0,27)==0;
    int ban=red?w.range(1,4):0;
    if(!active)return out;
    for(int tries=0;;++tries) {
        if(w.range(0,20)>75-int8_t(tries))continue;
        if(!keeper || w.range(0,4)==0)break;
    }
    out.kind=red?1:0;out.ban=ban;
    if(!red&&booked){out.kind=1;out.ban=1;out.second=true;}
    if(out.kind==0)out.change=w.range(0,12)==0;
    return out;
}
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"usage: reality_forced_event_tests <Original data>");
        const auto data=std::filesystem::path(argv[1]);const auto exe=bmh::test::load_executable(data/"BMH.exe");
        const auto save=load_savegame(data/"AUTOSAVE.FBM");
        const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
        auto base=GameState::from_save(SaveState(save.stream,counts),counts);
        base.manager_teams.clear();base.live_matches={0};base.team_morale[0]=base.team_morale[1]=128;
        for(int side=0;side<2;++side)for(int row=0;row<24;++row) {
            const int pid=side*24+row;base.squad_slots[size_t(pid)]=uint16_t(pid|(row<11?kSlotInLineup:0));
            base.players[size_t(pid)].w35c=48u|(40u<<7)|(100u<<14);
            base.players[size_t(pid)].w364=uint32_t(row==0?0:1+row%3);
            base.players[size_t(pid)].w368=3u|(3u<<4)|(3u<<6)|(7u<<14);
        }
        int seen=0,auto_changes=0,human_changes=0;
        for(int scenario=0;scenario<8;++scenario)for(uint32_t seed=1;seed<=256;++seed) {
            const int side=scenario&1,slot=side*24+(scenario==2?0:1),pid=slot;
            const bool booked=scenario==3||scenario==4,active=scenario!=5&&scenario!=6;
            const bool human=scenario==4||scenario==7;
            auto state=base;
            if(booked)state.squad_slots[size_t(slot)]|=0x1000;
            if(scenario==5)state.squad_slots[size_t(slot)]=kEmptySlot;
            if(scenario==6)state.squad_slots[size_t(slot)]&=0x0fff;
            if(human){state.manager_teams={side};state.manager_options.assign(5,0);}
            auto expected=state;
            auto group=fixture();std::vector<MatchGroup> groups{group};
            GameRandom game(777),eg(777);WatcomRandom w(seed),ew(seed);
            const auto want=prefix(ew,scenario==2,booked,active);
            MatchEngine engine(state,groups,game,w,{24});int boxes=0,screens=0;
            engine.live_booking=[&](const auto&){++boxes;};engine.live_event=[&](const auto&){++screens;};
            std::vector<ShowEvent> shows;engine.show=&shows;MinuteReport report;
            const auto out=engine.forced_event(groups[0],0,side,42,slot,report);
            check(!report.unsupported,report.reason.c_str());check(!boxes&&!screens,"mode1 suppresses immediate booking and manager screens");
            if(want.kind<0) {
                check(out.code==0&&!out.change&&groups[0].log.empty(),"unpunished/empty/off-field event has no effects or deferred request");
                check(save_stream(state)==save_stream(expected),"rejected offender leaves all Native bytes untouched");
            } else {
                const int code=want.kind==0?1:want.second?2:3;
                seen|=1<<code;
                check((out.code&0x7f)==code,"return byte matches actual yellow/second-yellow/straight-red log");
                check(groups[0].log==std::vector<uint16_t>{uint16_t(pid+(code+2)*0x1000),uint16_t(side*0x80+42)},"log retains fixed offender, team and minute");
                for(size_t i=0;i<state.squad_slots.size();++i)if(i!=size_t(slot))
                    check(state.squad_slots[i]==expected.squad_slots[i],"no random offender or immediate replacement changes other slots");
                auto& ep=expected.players[size_t(pid)];
                if(want.kind==0) {
                    ep.w368=(ep.w368&0xfffc3fffu)|(8u<<14);
                    expected.squad_slots[size_t(slot)]|=0x1000;
                } else {
                    ep.w364=(ep.w364&0xffe0ffffu)|uint32_t(want.ban|16)<<16;
                    ep.w368=(ep.w368&0xf8ffffffu)|0x01000000u;
                    expected.squad_slots[size_t(slot)]&=0x0fff;
                    expected.team_flags[size_t(side)]|=1;
                }
                const uint32_t at=::bmh::layout::DeskRecords_Teams_RuntimeField+uint32_t(side)*2;
                const int delta=want.kind==0||want.second?1:256;
                expected.memory.set16(at,uint16_t(expected.memory.u16(at)+delta));
                if(human)expected.manager_records[size_t(0x1ae + 42/5+((want.kind==1)+2)*0x12)]++;
                if(want.kind==0) {
                    check((state.players[size_t(pid)].w368>>14&15)==8&&(state.squad_slots[size_t(slot)]&0x1000),"selected player receives season yellow and match flag");
                    check(le32(groups[0].desc,0xbc)==1&&le32(groups[0].desc,0xc0)==0,"yellow increments descriptor booking counter");
                    check(bool(out.change)==(want.change&&!human),"yellow auto-change is deferred only when Source assistant permits it");
                    if(out.change){++auto_changes;check(!out.change->human&&out.change->team==side&&out.change->slot==slot&&out.change->mode==expected.players[size_t(pid)].position()+20,"automatic request retains format field/16/18 values");}
                    check(!game.calls(),"yellow card draws no Game RNG and no replacement RNG");
                } else {
                    check((state.players[size_t(pid)].w364>>16&31)==(want.ban|16)&&state.squad_slots[size_t(slot)]==pid,"selected sent-off player leaves lineup with actual ban");
                    check(le32(groups[0].desc,0xc0)==1&&state.team_flags[size_t(side)]&1,"red increments descriptor and sent-off team flag");
                    check(bool(out.code&0x80)==human&&bool(out.change)==human,"watched manager return bit and deferred request agree");
                    if(human){++human_changes;check(out.change->human&&out.change->team==1-side&&out.change->slot==255&&out.change->mode==0,"human request preserves Source opponent, ff sentinel and manager");}
                    // Re-rating is immediate even though the replacement/screen is deferred.
                    // Start with the actual card state and pre-rating board: rate_side is
                    // already qualified separately against Original recordings.
                    auto& rated=expected;
                    MatchSetup setup;setup.home=0;setup.away=1;setup.spectators=12000;
                    rate_side(rated,setup,side,80,ew,group.records.data()+side*9,&eg);
                    check(groups[0].records==group.records,"red re-rates exact side before any deferred manager action");
                }
                check(save_stream(state)==save_stream(expected),"actual Native changes equal selected card, counters and qualified re-rating only");
                for(const auto& event:shows)if(event.what==Show::Booking)check(!event.flag,"Reality emits observational card data without a message box");
            }
            check(w.seed()==ew.seed()&&w.calls()==ew.calls()&&game.seed()==eg.seed()&&game.calls()==eg.calls(),"mode1 has exact Source prefix and re-rating RNG; no random injury or slot draw");
        }
        check(seen==14&&auto_changes&&human_changes,"bounded seeds cover real yellow, both reds and automatic/human deferral");
        auto state=base;std::vector<MatchGroup> groups{fixture()};GameRandom game(777);WatcomRandom w(51);
        MatchEngine engine(state,groups,game,w,{24});MinuteReport report;engine.match_events_disabled=true;
        check(!engine.forced_event(groups[0],0,0,42,1,report).code&&!w.calls()&&!game.calls(),"disabled event draws nothing");
        bool rejected=false;try{engine.forced_event(groups[0],0,0,42,25,report);}catch(const FormatError&){rejected=true;}
        check(rejected&&!w.calls(),"wrong team's fixed slot is rejected before RNG or Native writes");
        // Integrate the actual legacy routine callback boundary, sharing its Watcom stream.
        state=base;state.players[25].w35c=100u<<7;RealitySetup scene;scene.teams={0,1};
        scene.players[12].player=25;scene.players[12].squad_slot=25;scene.players[1].player=1;scene.players[1].squad_slot=1;
        scene.grid.place(1,20,12,0);scene.grid.place(12,20,12,1);
        engine.match_events_disabled=false;int punished=0,bookings=0;
        for(uint32_t seed=1;seed<=256;++seed) {
            state=base;groups[0]=fixture();w=WatcomRandom(seed);game=GameRandom(777);
            const auto foul=reality_foul(state,scene,12,true,1,0,true,w,[&](int slot,int side){++punished;return engine.forced_event(groups[0],0,side,42,slot,report).code;});
            if(!groups[0].log.empty()){++bookings;check((groups[0].log[0]&0xfff)==25&&foul.event_kind>=1&&foul.event_kind<=3,"real foul calls Native card engine on its actual defender");}
        }
        check(punished&&bookings,"Source foul seeds reach actual Native cards without a mock callback");
        std::printf("PASS: 2048 Source fixed-offender cases, automatic=%d human=%d; real foul/card bookings=%d; no Main Reality loop claim\n",auto_changes,human_changes,bookings);
        return 0;
    } catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
