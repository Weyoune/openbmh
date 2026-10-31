// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/ai_match.hpp"
#include "game/driver.hpp"
#include "game/lineup.hpp"
#include "game/match.hpp"
#include "game/matchday_setup.hpp"
#include "game/matchday.hpp"
#include <limits>
#include <stdexcept>
#include <type_traits>
namespace bmh::world12 {
static_assert(std::is_nothrow_move_assignable_v<World>);
namespace {
AiLeagueRoundOutcome run_ai_league_matches(World& world,const GameState& base,const std::vector<Fixture>& fixtures,
    const std::vector<AiLeagueConditions>& conditions,GameRandom& game,WatcomRandom& random,bool settle) {
    if(fixtures.empty() || fixtures.size()>10 || conditions.size()!=fixtures.size())throw std::invalid_argument("world12: incomplete AI league match batch");
    std::vector<ClubId> participants;LeagueId league;
    for(size_t m=0;m<fixtures.size();++m) {
        const auto fixture=fixtures[m];const auto& c=conditions[m];
        if(fixture.home==fixture.away || c.month<1 || c.month>12 || c.temperature<-128 || c.temperature>127 || c.rain<0 || c.rain>7 ||
            c.spectators>uint32_t(std::numeric_limits<int>::max()) || c.referee>=c.referee_properties.size() || c.presentation_buttons.size()<0xb4)
            throw std::invalid_argument("world12: incomplete AI league match conditions");
        if(c.month!=conditions[0].month || c.date!=conditions[0].date || c.referee_properties!=conditions[0].referee_properties || c.presentation_buttons!=conditions[0].presentation_buttons)
            throw std::invalid_argument("world12: inconsistent shared league-round conditions");
        const auto& home=world.clubs.at(fixture.home);const auto& away=world.clubs.at(fixture.away);
        if(!home.league.valid() || home.league!=away.league || !world.catalogue.league(home.league).active || (league.valid() && league!=home.league))
            throw std::invalid_argument("world12: AI league fixture crosses league ownership");
        league=home.league;participants.push_back(fixture.home);participants.push_back(fixture.away);
    }
    auto projection=AiMatchProjection::create(world,participants,base);auto& local=projection.state;
    local.indoor=false;local.lineups_frozen=false;local.pools_open=false;local.date=conditions[0].date;
    local.chance_rain=conditions[0].rain;local.chance_temperature=conditions[0].temperature;
    for(size_t team=0;team<participants.size();++team) {
        bmh::prepare_team(local,int(team),0,int(team));
        const auto formation=size_t(local.formation(int(team)))*22;
        if(formation+22>local.formations.size())throw std::invalid_argument("world12: missing Original formation table");
        for(size_t row=0;row<11;++row)if(local.formations[formation+row]>=24 || local.formations[formation+11+row]>8)
            throw std::invalid_argument("world12: invalid Original formation coordinates");
        bmh::auto_lineup(local,int(team),100);
        const auto fit=bmh::team_average(local,int(team),0x1f1,nullptr);
        if(fit.players<8)throw std::invalid_argument("world12: short-squad forfeit needs the full matchday settlement owner");
    }
    bmh::reset_match_played_flags(local);
    auto staged_game=game;auto staged_random=random;
    const auto initial_game_seed=game.seed();const auto initial_watcom_seed=random.seed();
    const auto initial_game_calls=game.calls(),initial_watcom_calls=random.calls();
    bmh::MatchGroup group;group.desc.assign(0xd0,0);group.desc[1]=2;group.desc[2]=uint8_t(fixtures.size());
    group.desc[0xcc]=uint8_t(bmh::match_team_league(local,0));
    for(size_t m=0;m<fixtures.size();++m) {
        const auto& c=conditions[m];group.desc[0x10+m]=c.referee;group.desc[0x30+m]=uint8_t(c.temperature);group.desc[0x50+m]=uint8_t(c.rain);
        group.fixtures.push_back(int(2*m));group.fixtures.push_back(int(2*m+1));group.results.push_back(0);group.end_minute.push_back(0);group.spectators.push_back(c.spectators);
        bmh::MatchSetup setup;setup.home=int(2*m);setup.away=int(2*m+1);setup.match_index=int(m);setup.climate_effect=true;
        setup.month=c.month;setup.temperature=c.temperature;setup.spectators=int(c.spectators);
        const auto sides=bmh::build_side_records(local,setup,staged_random,&staged_game);
        for(const auto& side:sides)group.records.insert(group.records.end(),side.b.begin(),side.b.end());
    }
    std::vector<bmh::MatchGroup> groups;groups.push_back(std::move(group));
    bmh::MatchEngine engine(local,groups,staged_game,staged_random,conditions[0].referee_properties);
    engine.month=conditions[0].month;engine.buttons=conditions[0].presentation_buttons;engine.leave_screens=true;
    bmh::MinuteReport report;
    bmh::play_matchday(engine,local,groups,conditions[0].presentation_buttons,staged_game,staged_random,report);
    if(report.unsupported)throw std::runtime_error("world12: Original AI match could not complete: "+report.reason);
    const auto& finished=groups[0];
    for(auto minute:finished.end_minute)if(minute<90 || minute>94)throw std::runtime_error("world12: Original AI league match did not reach full time");
    if(settle) {
        for(auto score:finished.results)if((score&0xcc)==0xcc)throw std::invalid_argument("world12: result collides with the legacy post-match sentinel");
        bmh::process_group(local,finished,staged_game,staged_random,report);
        if(report.unsupported)throw std::runtime_error("world12: AI post-match settlement could not complete: "+report.reason);
    }
    if(game.seed()!=initial_game_seed || game.calls()!=initial_game_calls || random.seed()!=initial_watcom_seed || random.calls()!=initial_watcom_calls)
        throw std::invalid_argument("world12: random stream ownership changed during the match");
    AiLeagueRoundOutcome outcome;outcome.game_draws=staged_game.calls()-initial_game_calls;outcome.watcom_draws=staged_random.calls()-initial_watcom_calls;
    for(size_t m=0;m<fixtures.size();++m)outcome.matches.push_back({fixtures[m],{true,uint8_t(finished.results[m]>>4),uint8_t(finished.results[m]&15)},finished.end_minute[m],0,0,settle});
    projection.commit_match_state(world);game=staged_game;random=staged_random;return outcome;
}
}
AiLeagueOutcome simulate_ai_league_match(World& world,const GameState& base,Fixture fixture,const AiLeagueConditions& conditions,GameRandom& game,WatcomRandom& random) {
    auto result=run_ai_league_matches(world,base,{fixture},{conditions},game,random,false);auto outcome=result.matches.front();outcome.game_draws=result.game_draws;outcome.watcom_draws=result.watcom_draws;return outcome;
}
AiLeagueOutcome simulate_and_settle_ai_league_match(World& world,const GameState& base,Fixture fixture,const AiLeagueConditions& conditions,GameRandom& game,WatcomRandom& random) {
    auto result=run_ai_league_matches(world,base,{fixture},{conditions},game,random,true);auto outcome=result.matches.front();outcome.game_draws=result.game_draws;outcome.watcom_draws=result.watcom_draws;return outcome;
}
AiLeagueRoundOutcome simulate_and_settle_ai_league_round(World& world,const GameState& base,LeagueId league,int round,const std::vector<AiLeagueConditions>& conditions,GameRandom& game,WatcomRandom& random) {
    return run_ai_league_matches(world,base,league_fixtures(world,league,round),conditions,game,random,true);
}
}
