// SPDX-License-Identifier: GPL-3.0-only
#include "app/world12_cups.hpp"
#include "game/cup_draw_rules.hpp"
#include "world12_fixture.hpp"
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace bmh::world12;
namespace application=bmh::app::world12;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class Function> void rejects(Function function,const char* message) {
    bool rejected=false;try{function();}catch(const std::exception&){rejected=true;}check(rejected,message);
}
std::set<ClubId> participants(const KnockoutStage& stage) {
    std::set<ClubId> clubs;
    for(const auto& pair:stage.rounds.front().fixtures){clubs.insert(pair.home);clubs.insert(pair.away);}
    clubs.insert(stage.seeded.begin(),stage.seeded.end());return clubs;
}
void check_home_order(const World& world,const KnockoutStage& stage) {
    int previous=255;
    for(const auto& pair:stage.rounds.front().fixtures) {
        const int a=world.catalogue.league(world.clubs.at(pair.home).league).local_index;
        const int b=world.catalogue.league(world.clubs.at(pair.away).league).local_index;
        check(!(a<2 && b>=2),"underclass away club did not receive Original home priority");
        const int weight=bmh::cup_league_weight(a)+bmh::cup_league_weight(b);
        check(weight<=previous,"initial ties not ordered by Original league weight");previous=weight;
    }
}
CareerSnapshot fresh(const World& world) {
    CareerSnapshot state;state.world=world;state.results=SeasonResults::empty(world.catalogue);
    state.game_seed=37;state.watcom_seed=91;return state;
}
void initial_fields(const World& world,const application::CupSources& sources) {
    auto de=bmh::test::world12::membership_ranking(world,Country::germany);
    auto en=bmh::test::world12::membership_ranking(world,Country::england);
    for(auto& league:de)std::reverse(league.begin(),league.end());
    for(auto& league:en)std::reverse(league.begin(),league.end());
    auto state=fresh(world);
    state.world.managers={{0,"Manager",de[4][0],{4},0}};
    const auto initial=state;
    const auto notices=application::install_domestic_cup_fields(state,de,en,sources);
    check(state.cups.size()==3 && notices.size()==1 && notices[0].cup==DomesticCup::german_cup,"country draw audience changes initial field installation");
    check(participants(state.cups[0]).size()==64 && participants(state.cups[1]).size()==68 && participants(state.cups[2]).size()==44,"Original initial entrant totals changed");
    check(participants(state.cups[0]).contains(de[2][13]) && !participants(state.cups[0]).contains(de[2][14]),"German cup qualification inferred from IDs instead of supplied ranks");
    check(participants(state.cups[1]).contains(en[3][7]) && !participants(state.cups[1]).contains(en[3][8]),"FA Cup lower division quota changed");
    check(participants(state.cups[2]).contains(en[2][3]) && !participants(state.cups[2]).contains(en[2][4]),"League Cup lower division quota changed");
    for(const auto& cup:state.cups)check_home_order(world,cup);
    const std::set<ClubId> premier(en[0].begin(),en[0].end());
    for(size_t i:{size_t(1),size_t(2)})check(std::set<ClubId>(state.cups[i].seeded.begin(),state.cups[i].seeded.end())==premier,"Premier League seeds entered the preliminary round");
    bmh::GameRandom expected(initial.game_seed);
    for(int size:{64,20,48,20,24})for(int i=0;i<size;++i)expected.range(0,size-1);
    check(expected.calls()==176 && state.game_seed==expected.seed() && state.watcom_seed==initial.watcom_seed,"initial GameRandom stream/count changed");
    auto hidden=initial;hidden.world.managers.clear();
    check(application::install_domestic_cup_fields(hidden,de,en,sources).empty(),"AI-only initial draw was displayed");
    hidden.world.managers=state.world.managers;check(hidden==state,"hiding the initial draw changed the world or RNG");
    const auto installed=state;
    rejects([&]{application::install_domestic_cup_fields(state,de,en,sources);},"initial fields installed twice");check(state==installed,"second initial installation changed the state");
    auto invalid=initial;auto broken=en;broken[0][0]=de[0][0];
    rejects([&]{application::install_domestic_cup_fields(invalid,de,broken,sources);},"foreign ranking accepted");check(invalid==initial,"English failure retained German initial draws/RNG");
    auto damaged=sources.english;const uint8_t wrong=8;damaged.patch(bmh::cup_entry_address(25)+0x18,&wrong,1);
    rejects([&]{application::install_domestic_cup_fields(invalid,de,en,{sources.german,damaged});},"invalid League Cup source accepted");check(invalid==initial,"late source failure advanced the live game RNG");
    auto negative=initial;negative.game_seed=-19;const auto negative_before=negative;
    rejects([&]{application::install_domestic_cup_fields(negative,de,en,sources);},"invalid random index used for a country field");check(negative==negative_before,"invalid game RNG state partially initialized cups");
}
void full_programme(const World& world,const application::CupSources& sources) {
    auto state=fresh(world);
    state.world.managers={{0,"German",world.membership[4].clubs[0],{4},0},{1,"English",world.membership[9].clubs[0],{9},1}};
    std::vector<uint16_t> de(365),en(365);
    for(int r=0;r<34;++r)de[size_t(30+r*8)]=15;
    for(int r=0;r<38;++r)en[size_t(30+r*8)]=15;
    for(int day=0;day<=20;day+=4)de[size_t(day)]=0x20;
    for(int day=0;day<=12;day+=2)en[size_t(day)]=0x20;
    for(int day=1;day<=19;day+=2)en[size_t(day)]=0x4002;
    state.calendar=CalendarCursor{SeasonCalendar::from_sources(world.catalogue,de,en),0,0};
    application::install_domestic_cup_fields(state,bmh::test::world12::membership_ranking(world,Country::germany),
        bmh::test::world12::membership_ranking(world,Country::england),sources);
    const auto game_seed=state.game_seed;std::map<DomesticCup,int> matches;int notices=0;
    for(int day=0;day<=20;++day) {
        const auto ticket=*prepare_day(state);std::vector<application::CupScores> scores;
        for(const auto& plan:ticket.cups) {
            scores.push_back({plan.cup,plan.source_entry,plan.revision,
                std::vector<uint16_t>(plan.matches.size(),plan.leg==1?uint16_t(2<<5):uint16_t(1))});
            matches[plan.cup]+=int(plan.matches.size());
        }
        if(day==0) {
            const auto before=state;auto incomplete=scores;incomplete.back().scores.pop_back();
            rejects([&]{application::commit_day_with_domestic_draws(state,ticket,{},incomplete,sources);},"incomplete second-country results accepted");
            check(state==before,"failed day retained an earlier country's draw or Watcom seed");
        }
        const auto presented=application::commit_day_with_domestic_draws(state,ticket,{},scores,sources);notices+=int(presented.cup_draws.size());
        const auto saved=state;state=read_save(make_save(state));check(state==saved,"automatic draw/stage/RNG lost on save/reload");
    }
    check(matches[DomesticCup::german_cup]==63 && matches[DomesticCup::fa_cup]==67 && matches[DomesticCup::league_cup]==73,"automatic cup progression lost a source tie");
    bmh::WatcomRandom expected(91);for(int i=0;i<420;++i)expected.rand();
    check(state.watcom_seed==expected.seed() && state.game_seed==game_seed,"next-round draws used the wrong RNG/count");
    check(notices==16 && state.cups.size()==6,"return legs/finals emitted draws or stage history was lost");
    for(auto cup:{DomesticCup::german_cup,DomesticCup::fa_cup,DomesticCup::league_cup}) {
        const auto final=std::find_if(state.cups.rbegin(),state.cups.rend(),[&](const auto& stage){return stage.cup==cup;});
        check(final->complete() && final->qualifiers().size()==1,"automatic draw programme did not reach one winner");
    }
}
void original_exchange_order() {
    struct Pair {int home,away;};
    std::vector<Pair> pairs={{1,2},{1,3},{0,0}};
    bmh::order_national_cup_pairs(pairs,[](int tier){return bmh::CupDrawClass{tier>=2,bmh::cup_league_weight(tier)};});
    check(pairs[0].home==0 && pairs[1].home==3 && pairs[2].home==2,"Original exchange ordering replaced by stable sorting");
}
}
int main()try {
    const auto world=bmh::test::world12::complete_world();
    const auto german=bmh::test::world12::country_rules(false),english=bmh::test::world12::country_rules(true);
    const application::CupSources sources{german,english};
    original_exchange_order();initial_fields(world,sources);full_programme(world,sources);
    std::cout<<"Original entrant quotas, 176 initial draws, 420 subsequent draws, human visibility and transactional automatic cup progression passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
