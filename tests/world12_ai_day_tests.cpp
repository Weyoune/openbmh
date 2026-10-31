// SPDX-License-Identifier: GPL-3.0-only
#include "world12_fixture.hpp"
#include "fixture_tables.hpp"
#include "app/world12_ai_day.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
using namespace bmh::world12;
namespace app=bmh::app::world12;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Function> void rejects(Function function,const char* message){bool rejected=false;try{function();}catch(const std::exception&){rejected=true;}check(rejected,message);}
bmh::GameState rules() {
    bmh::GameState state;state.club_ids.resize(445);state.team_flags.resize(445);state.team_morale.resize(445);
    state.formations.resize(0x580);
    const uint8_t depth[]={0,5,5,5,5,10,10,10,10,17,17};
    const uint8_t lane[]={4,1,3,5,7,1,3,5,7,3,5};
    for(int set=0;set<4;++set)for(int indoor=0;indoor<2;++indoor)for(int formation=0;formation<8;++formation)
        for(int row=0;row<11;++row) {
            const auto at=size_t(set)*0x160+size_t(indoor)*0xb0+size_t(formation)*22+size_t(row);
            if(at+11<state.formations.size()){state.formations[at]=depth[row];state.formations[at+11]=lane[row];}
        }
    return state;
}

CareerSnapshot snapshot(GameScope scope={}) {
    CareerSnapshot state;state.world=select_world_scope(bmh::test::world12::complete_world(),scope);
    std::vector<uint16_t> de(365),en(365);
    for(int r=0;r<34;++r)de[size_t(20+r*8)]=15;
    for(int r=0;r<38;++r)en[size_t(20+r*8)]=15;
    state.results=SeasonResults::empty(state.world.catalogue);
    state.calendar=CalendarCursor{SeasonCalendar::from_sources(state.world.catalogue,de,en),20,0};
    state.date=1020;state.game_seed=123456;state.watcom_seed=91;state.validate();return state;
}
std::vector<app::AiDayLeagueConditions> inputs(const CareerSnapshot& state,const DayTicket& day) {
    std::vector<app::AiDayLeagueConditions> result;
    AiLeagueConditions c;c.month=8;c.temperature=19;c.rain=0;c.spectators=15000;c.date=state.date;
    c.referee_properties={24};c.presentation_buttons.assign(0xb4,1);
    for(const auto& round:day.leagues)result.push_back({round.appointment,std::vector<AiLeagueConditions>(round.matches.size(),c)});
    return result;
}
uint64_t draws=0;
void count_draw(int){++draws;}
CareerSnapshot* actual_state=nullptr;app::AiDayOwner* actual_owner=nullptr;
void change_state(int){bmh::draw_hook=nullptr;++actual_state->world.clubs.begin()->second.morale;}
void change_owner(int){bmh::draw_hook=nullptr;++actual_owner->revision;}
void actual_days(const bmh::GameState& base) {
    size_t match_count=0;
    for(const auto scope:{GameScope{},GameScope{true,false,true},GameScope{false,true,true}}) {
        auto state=snapshot(scope);const auto before=state;const auto ticket=*prepare_day(state);const auto conditions=inputs(state,ticket);
        auto expected=state;bmh::GameRandom game(state.game_seed);bmh::WatcomRandom random(state.watcom_seed);
        std::vector<CompletedLeagueRound> scores;
        for(size_t i=0;i<ticket.leagues.size();++i) {
            const auto& a=ticket.leagues[i].appointment;
            const auto round=simulate_and_settle_ai_league_round(expected.world,base,a.league,a.round,conditions[i].matches,game,random);
            CompletedLeagueRound completed{a.league,a.round,{}};
            for(const auto& match:round.matches){check(match.settled&&match.score.played,"unsettled day score");completed.results.push_back(match.score);++match_count;}
            scores.push_back(std::move(completed));
        }
        expected.game_seed=game.seed();expected.watcom_seed=random.seed();commit_league_day(expected,ticket,scores);
        app::AiDayOwner owner{71,8};const app::AiDayIdentity identity{71,8};
        const auto result=app::play_ai_league_day(state,owner,identity,ticket,base,conditions);
        check(state==expected && owner.revision==9 && result.game_draws==game.calls() && result.watcom_draws==random.calls(),
            "AI day differs from direct shared-clock groups, settlement, RNG or coordinator");
        check(state.date==1021 && state.calendar->next_day==21 && state.calendar->revision==1,"AI day did not publish one date transition");
        check(read_save(make_save(state))==state,"played day lost development/results/calendar/RNG on reload");
        auto repeated=read_save(make_save(before));app::AiDayOwner repeated_owner{71,8};
        app::play_ai_league_day(repeated,repeated_owner,identity,ticket,base,conditions);
        check(repeated==state,"save/reload changed the same-day simulation");
        const auto after=state;rejects([&]{app::play_ai_league_day(state,owner,identity,ticket,base,conditions);},"day replay accepted");
        check(state==after&&owner.revision==9,"replay mutated the completed day");
        const auto empty=*prepare_day(state);check(empty.leagues.empty(),"empty date fixture assumptions wrong");
        const auto quiet=app::play_ai_league_day(state,owner,{71,9},empty,base,{});
        check(quiet.leagues.empty()&&quiet.game_draws==0&&quiet.watcom_draws==0&&state.world==after.world&&
            state.game_seed==after.game_seed&&state.watcom_seed==after.watcom_seed&&state.date==1022&&owner.revision==10,
            "empty league date changed match state or consumed RNG");
    }
    std::cout<<match_count<<" actual fixtures across full/German-fallback/English days matched direct Core groups, with replay and reload checks\n";
}
void rejection_tests(const bmh::GameState& base) {
    auto state=snapshot();auto ticket=*prepare_day(state);auto conditions=inputs(state,ticket);
    app::AiDayOwner owner{71,8};const auto initial_owner=owner;const auto before=state;
    const auto run=[&](const auto& t,const auto& c){return app::play_ai_league_day(state,owner,{71,8},t,base,c);};
    auto stale=ticket;++stale.revision;rejects([&]{run(stale,conditions);},"stale ticket accepted");
    auto missing=conditions;missing.pop_back();rejects([&]{run(ticket,missing);},"incomplete day weather accepted");
    auto wrong_date=conditions;wrong_date.back().matches.back().date=ticket.day;
    rejects([&]{run(ticket,wrong_date);},"season-local date substituted for absolute career date");
    rejects([&]{app::play_ai_league_day(state,owner,{72,8},ticket,base,conditions);},"foreign scope accepted");
    check(state==before&&owner==initial_owner,"invalid request changed career or owner");
    // Fail only in the final league: earlier groups have genuinely consumed
    // match RNG in the candidate, but none of those effects may be published.
    auto late=conditions;late.back().matches.back().month=0;draws=0;bmh::draw_hook=count_draw;
    rejects([&]{run(ticket,late);},"bad final-league conditions accepted");bmh::draw_hook=nullptr;
    check(draws>0&&state==before&&owner==initial_owner,"late simulation failure retained earlier groups/RNG/date");
    state.calendar->revision=std::numeric_limits<uint64_t>::max();ticket=*prepare_day(state);const auto exhausted=state;
    draws=0;bmh::draw_hook=count_draw;rejects([&]{run(ticket,conditions);},"exhausted coordinator revision accepted");bmh::draw_hook=nullptr;
    check(draws>0&&state==exhausted&&owner==initial_owner,"late coordinator failure published scored matches");
    state=before;ticket=*prepare_day(state);
    const auto last=ticket.leagues.back().matches.back().away;
    state.world.managers={{3,"Human",last,state.world.clubs.at(last).league,3}};const auto human=state;
    draws=0;bmh::draw_hook=count_draw;rejects([&]{run(ticket,conditions);},"human participant simulated as AI");bmh::draw_hook=nullptr;
    check(draws==0&&state==human&&owner==initial_owner,"human preflight did not block entire day before scoring");
    state=before;state.calendar->schedule.others.push_back({20,CalendarDomain::shared,0x40});ticket=*prepare_day(state);const auto shared=state;
    rejects([&]{run(ticket,conditions);},"shared competition day silently advanced");check(state==shared,"blocked competition mutated state");
    state=before;ticket=*prepare_day(state);actual_state=&state;actual_owner=&owner;bmh::draw_hook=change_state;
    rejects([&]{run(ticket,conditions);},"externally changed career overwritten");bmh::draw_hook=nullptr;
    auto external=before;++external.world.clubs.begin()->second.morale;
    check(state==external&&owner==initial_owner,"external change was lost or candidate leaked");
    state=before;bmh::draw_hook=change_owner;
    rejects([&]{run(ticket,conditions);},"externally changed owner overwritten");bmh::draw_hook=nullptr;
    check(state==before&&owner.revision==9,"external owner change lost or simulated state published");
    auto unrelated=snapshot({true,false,true});unrelated.world.managers={{0,"Human",unrelated.world.membership[0].clubs[0],{0},0}};
    // A blank date is safe even when a human owns a club; this is explicitly
    // the league-only coordinator, not the GUI's daily-economy transaction.
    unrelated.calendar->next_day=0;unrelated.date=1000;app::AiDayOwner quiet_owner{72,0};
    app::play_ai_league_day(unrelated,quiet_owner,{72,0},*prepare_day(unrelated),base,{});
    check(unrelated.date==1001,"unrelated human ownership blocked a blank league date");
}
}
int main()try {
    bmh::set_fixture_tables(bmh::test::authored_fixture_tables());const auto base=rules();
    actual_days(base);rejection_tests(base);
    std::cout<<"Atomic AI league days: stale owners, late rollback, human/competition guards and external-change preservation passed\n";return 0;
}catch(const std::exception& error){bmh::draw_hook=nullptr;std::cerr<<error.what()<<'\n';return 1;}
