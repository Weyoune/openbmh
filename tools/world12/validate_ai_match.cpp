// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/ai_match.hpp"
#include "game/world12/codec.hpp"
#include "game/world12/save.hpp"
#include "game/career.hpp"
#include "formats/data_profile.hpp"
#include "formats/le.hpp"
#include <iostream>

int main(int argc,char** argv)try {
    using namespace bmh;
    using namespace bmh::world12;
    if(argc!=3)throw std::runtime_error("usage: world12_ai_validate <WORLD12.JSON> <supplied reference rules directory>");
    const auto source=decode_world(read_file(argv[1]));
    const std::filesystem::path data=argv[2];
    const auto profile=load_data_profile(data);install_runtime_profile(profile);
    const auto exe=load_le(data/"BMH.exe");install_career_rules(exe);
    const auto database=load_database(data/"M3D.MAN",profile.counts);
    auto base=startup_state(exe,database,read_file(data/"M3.MEN"),read_file(data/"M3.DAT"));
    AiLeagueConditions conditions;conditions.month=8;conditions.temperature=19;conditions.rain=0;conditions.date=40;
    conditions.spectators=15000;conditions.referee_properties=database.raw_referee_properties;conditions.presentation_buttons.assign(0xb4,1);
    int completed=0,total_goals=0;
    for(uint32_t league:{0u,5u,9u,10u,11u,12u,13u}) {
        if(!source.catalogue.league({league}).active)continue;
        const auto& clubs=source.membership[league].clubs;
        const bmh::world12::Fixture fixture{clubs.at(0),clubs.at(1)};
        auto world=source;GameRandom game(37);WatcomRandom random(91);
        const auto outcome=simulate_ai_league_match(world,base,fixture,conditions,game,random);
        CareerSnapshot snapshot;snapshot.world=world;snapshot.results=SeasonResults::empty(world.catalogue);
        snapshot.date=conditions.date;snapshot.game_seed=game.seed();snapshot.watcom_seed=random.seed();
        if(read_save(make_save(snapshot))!=snapshot)throw std::runtime_error("source-backed post-scoring state failed snapshot roundtrip");
        std::cout<<source.catalogue.league({league}).name<<": clubs "<<fixture.home.value<<"-"<<fixture.away.value
            <<" score "<<int(outcome.score.home)<<":"<<int(outcome.score.away)<<" end "<<int(outcome.end_minute)
            <<" G="<<outcome.game_draws<<" W="<<outcome.watcom_draws<<"; match-state snapshot passed\n";
        total_goals+=outcome.score.home+outcome.score.away;++completed;
    }
    std::cout<<completed<<" actual Original AI scoring-phase matches, "<<total_goals<<" goals. Weather/attendance are explicit probe inputs.\n"
        <<"No after-match development/economy, human tactics or complete career progression is claimed.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
