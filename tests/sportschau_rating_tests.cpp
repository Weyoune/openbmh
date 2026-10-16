// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <iostream>
#include "app/career_session.hpp"
#include "game/matchday.hpp"
#include "game/save.hpp"
#include "game/turn.hpp"

using namespace bmh;
static void check(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
static void put16(Bytes& b,size_t at,uint16_t value) {b.at(at)=uint8_t(value);b.at(at+1)=uint8_t(value>>8);}

static void controlled(const LeImage& exe) {
    auto tables = sportschau_rating_tables(exe);
    check(tables.position_limit == std::array<int8_t,4>{1,7,12,22} &&
          tables.award_begin == std::array<uint8_t,4>{0,1,4,8} &&
          tables.award_end == std::array<uint8_t,4>{1,4,8,11} && tables.domestic_teams == 138,
          "German Original executable supplies signed coefficients and eleven award bins");
    GameState state;
    state.memory.add_block(::bmh::layout::FixtureSportschauOptionsBegin, 200);
    state.squad_slots.assign(48, 0xfff); state.players.resize(5);
    state.manager_teams = {0}; state.tactics.assign(0x840, 0);
    for (size_t i = 0; i < 5; ++i) { state.squad_slots[i] = uint16_t(i); state.players[i].w364 = 0x1000; }
    state.players[4].w364 |= 0x10000; // injured, no rating draw
    const int depths[4] = {0,1,7,12};
    for (size_t i = 0; i < 4; ++i) state.tactics[i*0x58+0x41] = uint8_t(depths[i]);
    MatchGroup group; group.desc.assign(0xd0,0);
    // Home: two goals by3 (both assisted by1), miss by2. Away: one goal,
    // two misses. Goal/miss companions are consumed, not counted as goals.
    group.log = {3,10,1,0, 3,20,1,0, 0x1002,30,0x0fff,0,
                 0x0fff,uint16_t(128|40),0x0fff,128,
                 0x1fff,uint16_t(128|50),0x0fff,128,
                 0x1fff,uint16_t(128|60),0x0fff,128};
    SportschauFixtureView view; view.home_team=0;view.away_team=1;view.source_setting_index=0;
    WatcomRandom actual(8128), expected(8128);
    const int s0=expected.range(0,7)+5+3;
    const int s1_base=expected.range(0,7)+5;
    const int s1=s1_base+1+expected.range(0,4)+4;
    const int s2=expected.range(0,7)+5+1-1+3;
    const int s3=expected.range(0,7)+5+2+4;
    rate_sportschau_players(state,group,view,actual,tables);
    check(actual.seed()==expected.seed() && actual.calls()==5,"exact eligible order, profile1 extra draw, injury skip");
    check(group.desc[0x70]==s0 && group.desc[0x71]==s1 && group.desc[0x74]==s2 && group.desc[0x78]==s3,
          "opponent misses, goals, assists, own chances and scorer coefficients match hand-computed Source values");
    check(le16(group.desc,0x9c)==0 && le16(group.desc,0x9e)==1 &&
          le16(group.desc,0xa4)==2 && le16(group.desc,0xac)==3,
          "award identity is global squad slot, not scorer id or fixture-local row");
    auto unchanged = group.desc; state.memory.set8(::bmh::layout::SportschauAwards_FinishSportschauAwards_RuntimeField,1);
    rate_sportschau_players(state,group,view,actual,tables);
    check(actual.calls()==5 && group.desc==unchanged,"G7 off suppresses all award writes and draws");
    state.memory.set8(::bmh::layout::SportschauAwards_FinishSportschauAwards_RuntimeField,0);state.memory.set8(::bmh::layout::Controller_FormatError_ExecutableField2,1);
    rate_sportschau_players(state,group,view,actual,tables);
    check(actual.calls()==5 && group.desc==unchanged,"C4 off suppresses all award writes and draws");
    state.memory.set8(::bmh::layout::Controller_FormatError_ExecutableField2,0);state.memory.set8(::bmh::layout::Controller_FormatError_RuntimeField3,1);
    view.source_selected=false;view.replay_progression_mode=1;view.replay_chance_mode=1;
    WatcomRandom hidden(8128);group.desc.assign(0xd0,0);
    rate_sportschau_players(state,group,view,hidden,tables);
    check(hidden.seed()==expected.seed() && group.desc[0x78]==s3,"hidden F7 and unselected report still rate players");

    // Twelve-entry actor limits are independent of total chance counters.
    state.squad_slots.assign(48,0xfff);state.squad_slots[23]=3;
    state.tactics[23*0x58+0x41]=12;group.desc.assign(0xd0,0);group.log.clear();
    for (int i=0;i<13;++i) { group.log.insert(group.log.end(),{3,10,0x7fff,0}); }
    for (int i=0;i<13;++i) { group.log.insert(group.log.end(),{0x1003,20,0x8fff,0}); }
    WatcomRandom capped(44), cap_expected(44);const int cap_score=cap_expected.range(0,7)+5+24;
    rate_sportschau_players(state,group,view,capped,tables);
    check(group.desc[0x78]==cap_score && le16(group.desc,0xac)==23 && capped.calls()==1,
          "independent capped actors, no penalty/free-kick assistants, substituted global slot identity");
    // Equal candidates replace the first minimum; old score bytes are unsigned.
    group.desc.assign(0xd0,0);group.log.clear();WatcomRandom tie(44), tie_expected(44);
    const int tie_score=tie_expected.range(0,7)+5;
    for(size_t i=8;i<11;++i) {group.desc[0x70+i]=uint8_t(tie_score);put16(group.desc,0x9c+i*2,999);}
    rate_sportschau_players(state,group,view,tie,tables);
    check(le16(group.desc,0xac)==23 && le16(group.desc,0xae)==999,"equal score replaces first minimum only");
    state.tactics[23*0x58+0x41]=0;group.desc.assign(0xd0,0);group.log.clear();
    for(int i=0;i<13;++i) group.log.insert(group.log.end(),{0x0fff,128,0x0fff,128});
    WatcomRandom negative(44);rate_sportschau_players(state,group,view,negative,tables);
    check(group.desc[0x70]==0 && negative.calls()==1,"negative signed rating does not wrap into a winning unsigned byte");
    view.home_team=138;view.away_team=139;WatcomRandom foreign(44);
    rate_sportschau_players(state,group,view,foreign,tables);
    check(foreign.calls()==0,"foreign teams have no domestic squad rating draws");
    // Real group-entry reset happens even with G7 disabled. It clears eleven
    // scores only, keeps stale slot ids and mirrors both Native award arrays.
    state.memory.add_block(::bmh::layout::MatchdayGroupDescriptors,0xd0);state.memory.set8(::bmh::layout::SportschauAwards_FinishSportschauAwards_RuntimeField,1);
    group.fixtures.clear();group.desc.assign(0xd0,0);
    for(size_t i=0;i<12;++i) group.desc[0x70+i]=77;
    for(size_t i=0;i<11;++i) put16(group.desc,0x9c+i*2,uint16_t(900+i));
    GameRandom gr(44);WatcomRandom wr(44);MinuteReport report;
    process_group(state,group,gr,wr,report,0,{},nullptr,nullptr,&tables);
    for(size_t i=0;i<11;++i) check(group.desc[0x70+i]==0 && state.memory.u8(::bmh::layout::Turn_FinishEvent_ExecutableField+uint32_t(i))==0 &&
        le16(group.desc,0x9c+i*2)==900+i && state.memory.u16(::bmh::layout::FixtureSportschauDescriptorWords+uint32_t(i*2))==900+i,
        "group reset and Native copy preserve exact eleven-array ownership");
    check(group.desc[0x7b]==77 && wr.calls()==0 && gr.calls()==0,"twelfth descriptor byte and both RNGs preserved at group reset");
    state.manager_records.assign(0xb0c,0);state.manager_options.assign(5,0);
    state.league_rounds.assign(20,0);state.results.assign(20*0x17c,0);
    for(size_t i=0;i<11;++i) {group.desc[0x70+i]=77;state.memory.set8(::bmh::layout::Turn_FinishEvent_ExecutableField+uint32_t(i),77);}
    std::vector<MatchGroup> groups{group};std::vector<uint8_t> buttons(180,1);
    finish_event(state,groups,0,buttons,{},{},gr,wr,report,{},nullptr,nullptr,false,&tables);
    for(size_t i=0;i<11;++i) check(groups[0].desc[0x70+i]==0 && state.memory.u8(::bmh::layout::Turn_FinishEvent_ExecutableField+uint32_t(i))==0,
        "Source caller resets eleven scores even when a5088 disables all report/rating calls");
    check(!report.unsupported && wr.calls()==0 && gr.calls()==0,"disabled Source caller resets scores without RNG draws");
}

static void natural(const std::filesystem::path& data,const std::filesystem::path& input) {
    app::CareerSession plain(data,8128,input),observed(data,8128,input);
    const auto options=plain.sportschau_source_options();check(options.ratings.has_value(),"loaded career owns executable tables");
    int fixtures=0;SportschauPresentationHooks hooks;
    hooks.fixture=[&](const GameState& state,const SportschauFixtureView& f) {
        const uint32_t base=::bmh::layout::MatchdayGroupDescriptors+uint32_t(f.group)*0xd0;
        check(state.memory.has(base+0xb1),"award Native descriptor exists at fixture callback");++fixtures;
    };
    observed.set_sportschau_presentation_hooks(hooks);
    uint64_t a=1,b=1;std::string error;ui::DeskRequest r;r.kind=ui::DeskRequestKind::advance_turn;r.manager_slot=0;r.revision=1;
    check(plain.dispatch_kickoff(r,a,error).kind==TurnDeskKind::NextDesk && error.empty(),"no-UI natural turn completes");
    check(observed.dispatch_kickoff(r,b,error).kind==TurnDeskKind::NextDesk && error.empty() && fixtures>0,"observed natural turn completes");
    check(save_stream(plain.state())==save_stream(observed.state()) && plain.rand().seed()==observed.rand().seed() &&
          plain.rand().calls()==observed.rand().calls() && plain.game().seed()==observed.game().seed() &&
          plain.game().calls()==observed.game().calls(),"all Native bytes and RNG state independent of UI observers");
    size_t scores=0;
    for(const auto& group:plain.last_turn_results().groups) for(size_t i=0;i<11;++i) scores+=group.desc[0x70+i]!=0;
    check(scores>0,"ordinary completed matchday has genuine computed award candidates");
}
int main(int argc,char**argv) {
    try {check(argc==3,"data and normal save required");controlled(load_le(std::filesystem::path(argv[1])/"BMH.exe"));natural(argv[1],argv[2]);
        std::cout<<"sportschau_rating_tests: Original coefficients, exact draws, gates, caps, ties and real career neutrality passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
