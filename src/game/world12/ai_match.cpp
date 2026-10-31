// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/ai_match.hpp"
#include "game/driver.hpp"
#include "game/lineup.hpp"
#include "game/match.hpp"
#include "game/matchday_setup.hpp"
#include <limits>
#include <stdexcept>
#include <type_traits>
namespace bmh::world12 {
static_assert(std::is_nothrow_move_assignable_v<World>);
AiLeagueOutcome simulate_ai_league_match(World& world,const GameState& base,Fixture fixture,
    const AiLeagueConditions& conditions,GameRandom& game,WatcomRandom& random) {
    if(fixture.home==fixture.away || conditions.month<1 || conditions.month>12 ||
        conditions.temperature<-128 || conditions.temperature>127 || conditions.rain<0 || conditions.rain>7 ||
        conditions.spectators>uint32_t(std::numeric_limits<int>::max()) ||
        conditions.referee>=conditions.referee_properties.size() || conditions.presentation_buttons.size()<0xb4)
        throw std::invalid_argument("world12: incomplete AI league match conditions");
    const auto& home=world.clubs.at(fixture.home);const auto& away=world.clubs.at(fixture.away);
    if(!home.league.valid() || home.league!=away.league || !world.catalogue.league(home.league).active)
        throw std::invalid_argument("world12: AI league fixture crosses league ownership");
    auto projection=AiMatchProjection::create(world,{fixture.home,fixture.away},base);
    auto& local=projection.state;
    local.indoor=false;local.lineups_frozen=false;local.pools_open=false;local.date=conditions.date;
    local.chance_rain=conditions.rain;local.chance_temperature=conditions.temperature;
    for(int team=0;team<2;++team) {
        bmh::prepare_team(local,team,0,team);
        const auto formation=size_t(local.formation(team))*22;
        if(formation+22>local.formations.size())throw std::invalid_argument("world12: missing Original formation table");
        for(size_t row=0;row<11;++row)if(local.formations[formation+row]>=24 || local.formations[formation+11+row]>8)
            throw std::invalid_argument("world12: invalid Original formation coordinates");
        bmh::auto_lineup(local,team,100);
        const auto fit=bmh::team_average(local,team,0x1f1,nullptr);
        if(fit.players<8)throw std::invalid_argument("world12: short-squad forfeit needs the full matchday settlement owner");
    }
    auto staged_game=game;auto staged_random=random;
    const auto initial_game_seed=game.seed();const auto initial_watcom_seed=random.seed();
    const auto initial_game_calls=game.calls(),initial_watcom_calls=random.calls();
    bmh::MatchGroup group;group.desc.assign(0xd0,0);group.desc[1]=2;group.desc[2]=1;
    group.desc[0xcc]=uint8_t(bmh::match_team_league(local,0));
    group.desc[0x10]=conditions.referee;group.desc[0x30]=uint8_t(conditions.temperature);group.desc[0x50]=uint8_t(conditions.rain);
    group.fixtures={0,1};group.results={0};group.end_minute={0};group.spectators={conditions.spectators};
    bmh::MatchSetup setup;setup.home=0;setup.away=1;setup.climate_effect=true;
    setup.month=conditions.month;setup.temperature=conditions.temperature;setup.spectators=int(conditions.spectators);
    const auto sides=bmh::build_side_records(local,setup,staged_random,&staged_game);
    for(const auto& side:sides)group.records.insert(group.records.end(),side.b.begin(),side.b.end());
    std::vector<bmh::MatchGroup> groups;groups.push_back(std::move(group));
    bmh::MatchEngine engine(local,groups,staged_game,staged_random,conditions.referee_properties);
    engine.month=conditions.month;engine.buttons=conditions.presentation_buttons;engine.leave_screens=true;
    bmh::MinuteReport report;
    bmh::play_matchday(engine,local,groups,conditions.presentation_buttons,staged_game,staged_random,report);
    if(report.unsupported)throw std::runtime_error("world12: Original AI match could not complete: "+report.reason);
    const auto& finished=groups[0];
    if(finished.end_minute.at(0)<90 || finished.end_minute.at(0)>94)
        throw std::runtime_error("world12: Original AI league match did not reach full time");
    if(game.seed()!=initial_game_seed || game.calls()!=initial_game_calls || random.seed()!=initial_watcom_seed || random.calls()!=initial_watcom_calls)
        throw std::invalid_argument("world12: random stream ownership changed during the match");
    AiLeagueOutcome outcome{fixture,{true,uint8_t(finished.results.at(0)>>4),uint8_t(finished.results.at(0)&15)},
        finished.end_minute[0],staged_game.calls()-initial_game_calls,staged_random.calls()-initial_watcom_calls};
    projection.commit_match_state(world);game=staged_game;random=staged_random;return outcome;
}
}
