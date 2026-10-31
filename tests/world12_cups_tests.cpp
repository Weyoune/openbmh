// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/progression.hpp"
#include "game/cup.hpp"
#include "formats/le.hpp"
#include "fixture_tables.hpp"
#include "world12_fixture.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace bmh::world12;
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
template<class Function> void rejects(Function function,const char* message) {
    bool rejected=false;try{function();}catch(const std::exception&){rejected=true;}check(rejected,message);
}
World fixture_world() {return bmh::test::world12::complete_world();}

std::vector<Fixture> draw(const World& world,LeagueId league,int matches) {
    const auto& clubs=world.membership[league.value].clubs;std::vector<Fixture> fixtures;
    for(int m=0;m<matches;++m)fixtures.push_back({clubs[size_t(2*m)],clubs[size_t(2*m+1)]});
    return fixtures;
}
bmh::LeImage image(DomesticCup cup,uint8_t entry,uint8_t teams,uint8_t rounds,uint8_t flags) {
    auto source=bmh::LeImage::from_bytes(bmh::layout::LeagueSizes,bmh::Bytes(0x1000));
    const uint8_t league_size=cup_country(cup)==Country::germany?18:20;
    source.patch(bmh::layout::LeagueSizes,&league_size,1);
    const auto at=bmh::cup_entry_address(entry);
    const uint8_t cup_id=cup==DomesticCup::league_cup?11:cup==DomesticCup::german_supercup||cup==DomesticCup::english_supercup?8:0;
    source.patch(at+0x14,&flags,1);source.patch(at+0x18,&cup_id,1);
    source.patch(at+0x1a,&teams,1);source.patch(at+0x1b,&rounds,1);return source;
}
KnockoutStage stage(const World& world,DomesticCup cup,LeagueId league,uint8_t entry,int matches,uint8_t rounds,uint8_t flags=0x2c) {
    return KnockoutStage::from_source(cup,image(cup,entry,uint8_t(matches*2),rounds,flags),entry,draw(world,league,matches),world);
}
CupLegResult home_wins(const ScheduledCupLeg& plan,const KnockoutStage& stage) {
    CupLegResult result{plan.cup,plan.source_entry,plan.revision,std::vector<uint16_t>(plan.matches.size(),2<<5),{}};
    if(stage.rounds.size()<stage.planned_rounds) {
        std::vector<ClubId> winners;for(const auto& match:plan.matches)winners.push_back(match.home);
        for(size_t i=0;i<winners.size()/2;++i)result.next_draw.push_back({winners[i],winners[winners.size()-1-i]});
    }
    return result;
}
KnockoutStage seeded_stage(const World& world,DomesticCup cup,uint8_t entry,int teams,int rounds,
    uint8_t next,int seed_count,uint8_t flags=46) {
    auto source=image(cup,entry,uint8_t(teams),uint8_t(rounds),flags);
    source.patch(bmh::cup_entry_address(entry)+0x16,&next,1);
    const auto count=uint8_t(seed_count);source.patch(bmh::cup_entry_address(entry)+0x20,&count,1);
    // Larger fields span the country ladder, not just its top division.
    std::vector<ClubId> pool;
    for(auto id:world.catalogue.table_leagues(cup_country(cup)==Country::germany?TableGroup::germany:TableGroup::england))
        pool.insert(pool.end(),world.membership[id.value].clubs.begin(),world.membership[id.value].clubs.end());
    std::vector<Fixture> fixtures;
    for(int i=0;i<teams;i+=2)fixtures.push_back({pool[size_t(i)],pool[size_t(i+1)]});
    const std::vector<ClubId> seeds(pool.begin()+teams,pool.begin()+teams+seed_count);
    return KnockoutStage::from_source(cup,source,entry,fixtures,world,seeds);
}
void full_domestic_programmes(const World& world) {
    std::vector<uint16_t> de(365),en(365);
    for(int r=0;r<34;++r)de[size_t(30+r*8)]=15;
    for(int r=0;r<38;++r)en[size_t(30+r*8)]=15;
    for(int day=0;day<=20;day+=4)de[size_t(day)]=0x20;
    for(int day=0;day<=12;day+=2)en[size_t(day)]=0x20;
    for(int day=1;day<=19;day+=2)en[size_t(day)]=0x4002;
    CareerSnapshot state;state.world=world;state.results=SeasonResults::empty(world.catalogue);
    state.calendar=CalendarCursor{SeasonCalendar::from_sources(world.catalogue,de,en),0,0};
    state.cups={seeded_stage(world,DomesticCup::german_cup,0,64,6,0,0),
        seeded_stage(world,DomesticCup::fa_cup,0,48,2,28,20),
        seeded_stage(world,DomesticCup::league_cup,25,24,1,26,20)};
    int matches=0;std::map<DomesticCup,int> totals;
    for(int day=0;day<=20;++day) {
        const auto ticket=*prepare_day(state);std::vector<CupLegResult> completed;
        for(const auto& plan:ticket.cups) {
            const auto found=std::find_if(state.cups.rbegin(),state.cups.rend(),[&](const auto& s){return s.cup==plan.cup;});
            const auto& current=*found;
            CupLegResult result{plan.cup,plan.source_entry,plan.revision,
                std::vector<uint16_t>(plan.matches.size(),plan.leg==1?uint16_t(2<<5):uint16_t(1)),{}};
            if((!current.two_legs() || plan.leg==2) && current.rounds.size()<current.planned_rounds) {
                const auto& fixtures=current.rounds.back().fixtures;
                for(size_t i=0;i<fixtures.size()/2;++i)
                    result.next_draw.push_back({fixtures[i].home,fixtures[fixtures.size()-1-i].home});
            }
            auto preview=current;commit_cup_leg(preview,result,world);
            if(preview.complete() && preview.next_entry) {
                const auto qualified=preview.qualifiers();std::vector<Fixture> next_draw;
                for(size_t i=0;i<qualified.size()/2;++i)next_draw.push_back({qualified[i],qualified[qualified.size()-1-i]});
                const bool fa=plan.cup==DomesticCup::fa_cup;
                const uint8_t next=fa?0:preview.next_entry==26?27:0;
                const uint8_t rounds=fa?5:preview.next_entry==26?4:1;
                const uint8_t flags=fa?46:preview.next_entry==26?63:44;
                auto source=image(plan.cup,preview.next_entry,uint8_t(qualified.size()),rounds,flags);
                source.patch(bmh::cup_entry_address(preview.next_entry)+0x16,&next,1);
                result.next_stage=KnockoutStage::from_source(plan.cup,source,preview.next_entry,next_draw,world);
            }
            totals[plan.cup]+=int(plan.matches.size());matches+=int(plan.matches.size());
            completed.push_back(std::move(result));
        }
        commit_day(state,ticket,{},completed);
        const auto before_save=state;state=read_save(make_save(state));
        check(state==before_save,"full cup programme failed daily save/reload");
    }
    check(matches==203 && totals[DomesticCup::german_cup]==63 && totals[DomesticCup::fa_cup]==67 &&
        totals[DomesticCup::league_cup]==73,"full domestic cup programme has wrong fixture totals");
    check(state.cups.size()==6,"completed qualifying/intermediate stage history lost");
    for(auto cup:{DomesticCup::german_cup,DomesticCup::fa_cup,DomesticCup::league_cup}) {
        const auto last=std::find_if(state.cups.rbegin(),state.cups.rend(),[&](const auto& s){return s.cup==cup;});
        check(last->complete() && last->next_entry==0 && last->qualifiers().size()==1,"domestic cup did not reach one champion");
    }
}
}
int main()try {
    using namespace bmh::world12;
    const auto world=fixture_world();
    check(!domestic_cups_on({0,CalendarDomain::germany,0x4002}),"indoor tournament interpreted as League Cup");
    check(domestic_cups_on({0,CalendarDomain::england,0x4002})->at(0)==DomesticCup::league_cup,"English League Cup not routed");
    check(!domestic_cups_on({0,CalendarDomain::shared,0x4006}),"national friendly interpreted as domestic cup");
    check(domestic_cups_on({0,CalendarDomain::germany,0x2020})->size()==2,"German cup/supercup mask lost");
    auto german=stage(world,DomesticCup::german_cup,{0},0,4,3);
    auto english=stage(world,DomesticCup::fa_cup,{5},28,4,3);
    auto audience=world;
    check(!cup_draw_notice(audience,german) && !cup_draw_notice(audience,english),"AI-only world opens cup draws");
    audience.managers={{0,"German manager",world.membership[4].clubs[0],{4},0}};
    audience.validate();
    check(cup_draw_notice(audience,german).has_value() && !cup_draw_notice(audience,english),
        "draw visibility ignores the manager's country or lower domestic division");
    check(!show_domestic_draw(audience,DomesticCup::league_cup) && !show_domestic_draw(audience,DomesticCup::english_supercup),
        "English subsidiary cup draws shown to German-only managers");
    audience.managers.push_back({1,"English manager",world.membership[9].clubs[0],{9},1});audience.validate();
    check(cup_draw_notice(audience,german).has_value() && cup_draw_notice(audience,english).has_value(),"mixed-country managers lose their draws");
    audience.managers.erase(audience.managers.begin());
    check(!show_domestic_draw(audience,DomesticCup::german_cup) && show_domestic_draw(audience,DomesticCup::fa_cup),"draw audience persists after last country manager leaves");
    auto foreign=draw(world,{5},4);
    rejects([&]{KnockoutStage::from_source(DomesticCup::german_cup,image(DomesticCup::german_cup,0,8,3,0x2c),0,foreign,world);},"foreign entrants accepted");
    rejects([&]{KnockoutStage::from_source(DomesticCup::fa_cup,image(DomesticCup::german_cup,0,8,3,0x2c),0,foreign,world);},"German source used as English rules");
    auto unchanged=german;
    CupLegResult missing_draw{german.cup,german.source_entry,0,std::vector<uint16_t>(4,2<<5),{}};
    rejects([&]{commit_cup_leg(german,missing_draw,world);},"undrawn next round fabricated");check(german==unchanged,"failed draw changed the cup");
    KnockoutStage empty;
    rejects([&]{empty.next_fixtures();},"empty stage exposed fixtures");

    bmh::set_fixture_tables(bmh::test::authored_fixture_tables());
    std::vector<uint16_t> de(365),en(365);
    for(int r=0;r<34;++r)de[size_t(r*8)]=15;
    for(int r=0;r<38;++r)en[size_t(r*8)]=15;
    for(int day=0;day<3;++day){de[size_t(day)]|=0x20;en[size_t(day)]|=0x20;}
    CareerSnapshot state;state.world=world;state.results=SeasonResults::empty(world.catalogue);
    state.calendar=CalendarCursor{SeasonCalendar::from_sources(world.catalogue,de,en),0,0};
    state.cups={german,english};state.watcom_seed=91;state.game_seed=27;
    state.world.managers={{0,"German manager",world.membership[4].clubs[0],{4},0}};
    for(int day=0;day<3;++day) {
        auto ticket=*prepare_day(state);check(ticket.cups.size()==2,"same-day country cups overwritten");
        std::vector<CompletedLeagueRound> leagues;
        for(const auto& plan:ticket.leagues)leagues.push_back({plan.appointment.league,plan.appointment.round,std::vector<LeagueResult>(plan.matches.size(),{true,2,1})});
        std::vector<CupLegResult> cups;
        for(size_t i=0;i<ticket.cups.size();++i)cups.push_back(home_wins(ticket.cups[i],state.cups[i]));
        const auto before=state;
        auto invalid=cups;invalid.back().scores.pop_back();
        rejects([&]{commit_day(state,ticket,leagues,invalid);},"partial English cup batch committed");
        check(state==before,"rejected English result changed German cup, league scores or date");
        auto ai_only=state;ai_only.world.managers.clear();
        const auto hidden=commit_day(ai_only,ticket,leagues,cups);
        check(hidden.cup_draws.empty(),"AI-only daily progression emitted draw UI");
        const auto shown=commit_day(state,ticket,leagues,cups);
        check(shown.cup_draws.size()==size_t(day<2?1:0),"draw not filtered to the human manager's country");
        if(!shown.cup_draws.empty())check(shown.cup_draws[0].cup==DomesticCup::german_cup,"English draw emitted for a German-only career");
        ai_only.world.managers=state.world.managers;
        check(ai_only==state,"hiding draws changed cup results, league scores, date or RNG");
        const auto committed=state;
        rejects([&]{commit_day(state,ticket,leagues,cups);},"cup day replay accepted");check(state==committed,"replayed cup day mutated career");
        state=read_save(make_save(state));check(state==committed,"cup history or ownership lost on reload");
    }
    check(state.date==3 && state.cups[0].complete() && state.cups[1].complete(),"mixed country cups did not end together");
    check(state.cups[0].qualifiers().size()==1 && state.cups[1].qualifiers().size()==1,"final winners missing");
    check(world.clubs.at(state.cups[0].qualifiers()[0]).country==Country::germany &&
        world.clubs.at(state.cups[1].qualifiers()[0]).country==Country::england,"winner belongs to another country");
    check(state.watcom_seed==91 && state.game_seed==27,"cup adapter invented RNG draws");

    auto league_cup=stage(world,DomesticCup::league_cup,{5},25,1,1,0x1d);
    const auto original=league_cup.next_fixtures()[0];
    commit_cup_leg(league_cup,{DomesticCup::league_cup,25,0,{uint16_t((2<<5)|1)},{}},world);
    check(league_cup.next_leg()==2 && league_cup.next_fixtures()[0]==Fixture{original.away,original.home},"return leg not reversed");
    state.cups.push_back(league_cup);state=read_save(make_save(state));league_cup=state.cups.back();
    commit_cup_leg(league_cup,{DomesticCup::league_cup,25,1,{uint16_t(1<<5)},{}},world);
    check(league_cup.complete() && league_cup.qualifiers()[0]==original.away,"Classic aggregate/away-goal winner changed");
    auto unresolved=stage(world,DomesticCup::english_supercup,{5},23,1,1,0x0c);const auto pending=unresolved;
    rejects([&]{commit_cup_leg(unresolved,{DomesticCup::english_supercup,23,0,{0},{}},world);},"unresolved final assigned an arbitrary winner");
    check(unresolved==pending,"unresolved tie mutated final");
    auto qualifier=stage(world,DomesticCup::fa_cup,{5},0,4,1);
    commit_cup_leg(qualifier,{DomesticCup::fa_cup,0,0,std::vector<uint16_t>(4,2<<5),{}},world);
    check(qualifier.qualifiers().size()==4,"qualifying-stage outputs forced into one champion");
    // FA Cup-style chain: qualifiers and clubs seeded past the preliminary
    // stage join the proper cup. Keep both stages in save history.
    auto qualifying_image=image(DomesticCup::fa_cup,0,4,1,0x2c);
    const uint8_t successor=28,seed_count=2;
    qualifying_image.patch(bmh::cup_entry_address(0)+0x16,&successor,1);
    qualifying_image.patch(bmh::cup_entry_address(0)+0x20,&seed_count,1);
    const auto& english_clubs=world.membership[5].clubs;
    const std::vector<ClubId> seeds={english_clubs[4],english_clubs[5]};
    auto qualifying_stage=KnockoutStage::from_source(DomesticCup::fa_cup,qualifying_image,0,draw(world,{5},2),world,seeds);
    rejects([&]{KnockoutStage::from_source(DomesticCup::fa_cup,qualifying_image,0,draw(world,{5},2),world,{english_clubs[0],english_clubs[5]});},"seeded club also plays qualifying");
    auto clean_de=de,clean_en=en;
    for(int day=0;day<3;++day){clean_de[size_t(day)]&=uint16_t(~0x20u);clean_en[size_t(day)]|=0x20;}
    CareerSnapshot seeded_state;seeded_state.world=world;seeded_state.results=SeasonResults::empty(world.catalogue);
    seeded_state.calendar=CalendarCursor{SeasonCalendar::from_sources(world.catalogue,clean_de,clean_en),0,0};
    seeded_state.cups={qualifying_stage};
    for(int day=0;day<3;++day) {
        const auto ticket=*prepare_day(seeded_state);check(ticket.cups.size()==1,"linked cup active stage not selected");
        const auto& current=seeded_state.cups.back();
        auto result=home_wins(ticket.cups[0],current);
        std::vector<CompletedLeagueRound> leagues;
        for(const auto& plan:ticket.leagues)leagues.push_back({plan.appointment.league,plan.appointment.round,std::vector<LeagueResult>(plan.matches.size(),{true,1,0})});
        if(day==0) {
            const auto before=seeded_state;
            rejects([&]{commit_day(seeded_state,ticket,leagues,{result});},"linked FA Cup stage skipped");
            check(seeded_state==before,"missing successor partially committed the cup day");
            const auto initial=draw(world,{5},2);
            const std::vector<Fixture> proper_draw={{initial[0].home,seeds[0]},{initial[1].home,seeds[1]}};
            result.next_stage=KnockoutStage::from_source(DomesticCup::fa_cup,image(DomesticCup::fa_cup,28,4,2,0x2c),28,proper_draw,world);
            auto wrong=result;wrong.next_stage->rounds[0].fixtures[0].away=english_clubs[6];
            rejects([&]{commit_day(seeded_state,ticket,leagues,{wrong});},"successor omitted a seeded club");
            check(seeded_state==before,"invalid successor draw changed the previous stage");
        } else check(ticket.cups[0].source_entry==28,"calendar returned to FA Cup qualifying");
        commit_day(seeded_state,ticket,leagues,{result});
        const auto saved=seeded_state;seeded_state=read_save(make_save(seeded_state));
        check(saved==seeded_state,"seeded entries, stage links or completed cup history lost");
    }
    check(seeded_state.cups.size()==2 && seeded_state.cups.back().qualifiers().size()==1,"linked cup failed to reach its champion");
    auto repeated=seeded_state;repeated.cups.push_back(repeated.cups.front());
    rejects([&]{make_save(repeated);},"cyclic or repeated cup stage saved");
    const auto complete_save=make_save(seeded_state);
    for(size_t cut=1;cut<=32;++cut) {
        auto truncated=complete_save;truncated.stream.resize(truncated.stream.size()-cut);
        rejects([&]{read_save(truncated);},"truncated cup history loaded");
    }
    full_domestic_programmes(world);
    std::cout<<"Country-owned cups, original tie rules, atomic mixed days, seeded stages and 203 test-score cup matches with daily save/reload passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
