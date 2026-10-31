// SPDX-License-Identifier: GPL-3.0-only
#include "world12_fixture.hpp"
#include "fixture_tables.hpp"
#include "game/world12/ai_match.hpp"
#include "game/world12/codec.hpp"
#include "game/world12/save.hpp"
#include "game/match.hpp"
#include "game/lineup.hpp"
#include "game/save.hpp"
#include "game/matchday.hpp"
#include "game/matchday_setup.hpp"
#include <limits>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using namespace bmh::world12;
World* interrupted_world=nullptr;ClubId interrupted_club;
bmh::GameRandom* interrupted_game=nullptr;
void change_live_owner(int) {
    bmh::draw_hook=nullptr;
    ++interrupted_world->clubs.at(interrupted_club).morale;
}
void change_live_random(int) {
    bmh::draw_hook=nullptr;interrupted_game->range(0,1);
}
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class Function> void rejects(Function function,const char* message) {
    bool rejected=false;try{function();}catch(const std::exception&){rejected=true;}check(rejected,message);
}
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
World source_world() {
    auto world=bmh::test::world12::complete_world();
    for(auto& [id,club]:world.clubs)for(size_t row=0;row<11;++row) {
        club.kit=uint8_t(id.value%20);club.short_span=1;
        auto& player=world.players.at(club.roster[row].player);
        player.attributes.w35c=60u|(75u<<7)|(85u<<14);
        player.attributes.w364=uint32_t(row==0?0:row<5?1:row<9?2:3);
        player.attributes.w368=0x2aaa;
    }
    return world;
}
void projection_state(const World& world,const bmh::GameState& base) {
    const auto en=world.membership[9].clubs[0],pt=world.membership[12].clubs[0];
    auto projection=AiMatchProjection::create(world,{en,pt},base);
    check(projection.state.club_kit(0)==world.clubs.at(en).kit && projection.state.club_short_offset(1)==world.clubs.at(pt).short_span,"club presentation metadata retained another local club identity");
    bool legacy_blocked=false;
    try{bmh::save_stream(projection.state);}catch(const bmh::FormatError& error){legacy_blocked=std::string(error.what()).find("projected match workspace")!=std::string::npos;}
    check(legacy_blocked,"local projected IDs could be exported as a legacy career save");
    check(bmh::match_team_league(projection.state,0)==4 && bmh::match_team_region(projection.state,0)==2 && bmh::match_foreign_player_limit(projection.state,0)==3,"English league/country/domestic foreign limit lost in local mapping");
    check(bmh::match_team_league(projection.state,1)==8 && bmh::match_team_region(projection.state,1)==4,"Italian climate/league classification lost in local mapping");
    const auto fallback=select_world_scope(world,{true,false,true});
    const auto premier=world.membership[5].clubs;
    const auto ai_premier=AiMatchProjection::create(fallback,{premier[0],premier[1]},base);
    const auto domestic_premier=AiMatchProjection::create(world,{premier[0],premier[1]},base);
    check(bmh::match_team_league(ai_premier.state,0)==6 && bmh::match_team_region(ai_premier.state,0)==2 &&
        bmh::match_foreign_player_limit(ai_premier.state,0)==2,"fallback Premier League lost Original foreign context");
    check(bmh::match_team_league(domestic_premier.state,0)==0 && bmh::match_foreign_player_limit(domestic_premier.state,0)==3,
        "full English Premier League inherited fallback context");
    auto malformed=projection.state;malformed.projected_match_clubs[0].league_class=-1;
    rejects([&]{bmh::match_team_league(malformed,0);},"invalid projected class used as a table index");
    auto persisted=world;
    const auto first=persisted.clubs.at(en).roster[0].player,second=persisted.clubs.at(en).roster[1].player;
    std::swap(projection.state.squad_slots[0],projection.state.squad_slots[1]);
    projection.state.player_cards[0]=0x8765;projection.state.team_flags[0]=0x601;projection.state.team_morale[0]=33;
    projection.commit_match_state(persisted);
    check(persisted.clubs.at(en).roster[0].player==second && persisted.clubs.at(en).roster[1].player==first,"local lineup changed global player identities");
    check(persisted.players.at(first).cards==0x8765 && persisted.clubs.at(en).match_flags==0x601 && persisted.clubs.at(en).morale==33,"cards, formation flags or morale were lost");
    check(decode_world(encode_world(persisted))==persisted,"new match-state fields failed world codec roundtrip");
    CareerSnapshot snapshot;snapshot.world=persisted;snapshot.results=SeasonResults::empty(world.catalogue);
    check(read_save(make_save(snapshot))==snapshot,"new match-state fields failed save roundtrip");
    auto stale=world;std::swap(stale.clubs.at(en).roster[0],stale.clubs.at(en).roster[1]);const auto before=stale;
    rejects([&]{projection.commit_match_state(stale);},"stale roster accepted after a match");check(stale==before,"stale roster rejection partially changed players");
    auto invalid=AiMatchProjection::create(world,{en,pt},base);invalid.state.squad_slots[0]=uint16_t(invalid.local_player(world.clubs.at(pt).roster[0].player)|0x8000);
    auto unchanged=world;rejects([&]{invalid.commit_match_state(unchanged);},"match projection transferred a player to another club");check(unchanged==world,"invalid cross-club roster partially committed");
    invalid=AiMatchProjection::create(world,{en,pt},base);invalid.state.players[0].w35c=(invalid.state.players[0].w35c&~127u)|110u;
    rejects([&]{invalid.commit_player_attributes(unchanged);},"invalid attributes accepted by partial commit");check(unchanged==world,"invalid attribute batch partially committed");
    auto human=world;human.managers={{0,"Human",human.membership[0].clubs[0],{0},0}};
    AiMatchProjection::create(human,{en,pt},base);
    rejects([&]{AiMatchProjection::create(human,{human.managers[0].club,en},base);},"unimplemented human match silently simulated as AI");
    auto ownership=world;ownership.managers=human.managers;const auto ownership_before=ownership;
    rejects([&]{projection.commit_match_state(ownership);},"manager ownership changed unnoticed during the match");check(ownership==ownership_before,"ownership rejection partially committed match data");
}
void rating_matches_legacy_positions(const World& world,const bmh::GameState& base) {
    // Compare the explicit context against the old positional lookup without
    // duplicating the rating formula. All global player IDs stay wide outside
    // the local workspace; the two legacy placements carry identical players.
    for(const auto& leagues:{std::pair{9u,9u},std::pair{10u,11u}}) {
        const auto home=world.membership[leagues.first].clubs[0],away=world.membership[leagues.second].clubs[1];
        auto projection=AiMatchProjection::create(world,{home,away},base);
        auto legacy=projection.state;legacy.projected_match_clubs.clear();
        const int native_home=leagues.first==9?72:90,native_away=leagues.second==9?73:114;
        for(int side=0;side<2;++side) {
            const int destination=side?native_away:native_home;
            const auto target=bmh::squad_range(destination),source=bmh::squad_range(side);
            for(size_t row=0;row<target.count;++row)legacy.squad_slots[target.first+row]=projection.state.squad_slots[source.first+row];
            legacy.team_flags[size_t(destination)]=projection.state.team_flags[size_t(side)];
            legacy.team_morale[size_t(destination)]=projection.state.team_morale[size_t(side)];
        }
        bmh::MatchSetup local;local.home=0;local.away=1;local.climate_effect=true;local.month=8;local.temperature=25;local.spectators=51000;
        auto original=local;original.home=native_home;original.away=native_away;
        bmh::WatcomRandom a(914),b(914);bmh::GameRandom ag(41),bg(41);
        const auto actual=bmh::build_side_records(projection.state,local,a,&ag);
        const auto expected=bmh::build_side_records(legacy,original,b,&bg);
        check(actual[0].b==expected[0].b && actual[1].b==expected[1].b && a.seed()==b.seed() && a.calls()==b.calls() && ag.seed()==bg.seed(),
            "explicit projected league/climate ratings differ from the Original positional calculation");
    }
}
void english_foreign_limit(const World& source,const bmh::GameState& base) {
    auto world=source;const auto first=world.membership[9].clubs[0],second=world.membership[9].clubs[1];
    auto& club=world.clubs.at(first);
    for(size_t i=0;i<5;++i) {
        PlayerRecord player;player.id={900000+uint32_t(i)};player.attributes.name="English reserve";
        player.attributes.w35c=40u|(50u<<7)|(60u<<14);player.attributes.w364=uint32_t(1+i%3);
        player.attributes.w368=2u<<27;world.players[player.id]=player;club.roster[11+i]={player.id,2};
    }
    auto projection=AiMatchProjection::create(world,{first,second},base);
    auto enabled=bmh::LeImage::from_bytes(bmh::layout::ForeignPlayerLimitEnabled,bmh::Bytes{1});
    bmh::install_lineup_rules(enabled);bmh::auto_lineup(projection.state,0,100);
    int locals=0;
    for(size_t row=0;row<11;++row)if(projection.state.players.at(projection.state.squad_slots[row]&0xfff).nationality()==2)++locals;
    auto disabled=bmh::LeImage::from_bytes(bmh::layout::ForeignPlayerLimitEnabled,bmh::Bytes{0});bmh::install_lineup_rules(disabled);
    check(locals==5,"English reserves were treated as foreigners under the German nationality origin");
}
void settlement_matches_core(const World& source,const bmh::GameState& base,const AiLeagueConditions& conditions) {
    for(uint32_t league:{0u,1u,9u,10u,12u}) {
        auto world=source;const Fixture fixture{world.membership[league].clubs[0],world.membership[league].clubs[1]};
        const auto historical=world.membership[12].clubs.back();
        auto& record=world.clubs.at(fixture.home).season_stats;
        record.opponents[0]=historical;record.counters[0x15]=0x90;record.dates[0]=599;
        world.development_balance=-61;world.trend_rotation=2;
        auto expected=world,actual=world;
        bmh::GameRandom eg(123456),ag(123456);bmh::WatcomRandom ew(91),aw(91);
        const auto scored=simulate_ai_league_match(expected,base,fixture,conditions,eg,ew);
        auto projection=AiMatchProjection::create(expected,{fixture.home,fixture.away},base);
        projection.state.date=conditions.date;
        bmh::MatchGroup group;group.desc.assign(0xd0,0);group.fixtures={0,1};
        group.results={uint8_t(scored.score.home<<4|scored.score.away)};
        bmh::MinuteReport report;bmh::process_group(projection.state,group,eg,ew,report);
        check(!report.unsupported,"direct Original post-match processing failed");projection.commit_match_state(expected);
        const auto settled=simulate_and_settle_ai_league_match(actual,base,fixture,conditions,ag,aw);
        check(settled.settled && !scored.settled && settled.score==scored.score,"settlement changed the already scored result");
        check(actual==expected && ag.seed()==eg.seed() && aw.seed()==ew.seed() && ag.calls()==eg.calls() && aw.calls()==ew.calls(),
            "typed post-match mapping differs from direct Original records/morale/development");
        check(actual.clubs.at(fixture.home).season_stats.opponents[0]==historical && actual.clubs.at(fixture.home).season_stats.dates[0]==599,
            "unprojected historical opponent was truncated or replaced by a current local ID");
        CareerSnapshot snapshot;snapshot.world=actual;snapshot.results=SeasonResults::empty(actual.catalogue);snapshot.game_seed=ag.seed();snapshot.watcom_seed=aw.seed();
        check(read_save(make_save(snapshot))==snapshot,"development or club record references lost after save/load");
        rejects([&]{select_world_scope(actual,{true,false,false});},"played development/statistics world was re-scoped");
    }
    // Check the original lower-division totals quirk against the actual old
    // positional implementation, not a duplicated points formula.
    for(const auto indices:{std::pair{1u,18},std::pair{4u,72},std::pair{10u,90},std::pair{12u,126}}) {
        const auto a=source.membership[indices.first].clubs[0],b=source.membership[indices.first].clubs[1];
        auto projection=AiMatchProjection::create(source,{a,b},base);auto native=projection.state;
        native.projected_match_clubs.clear();const int first=indices.second;
        native.club_ids[size_t(first)]=projection.state.club_ids[0];native.club_ids[size_t(first+1)]=projection.state.club_ids[1];
        native.date=projection.state.date=conditions.date;
        bmh::update_club_table(projection.state,0,1,2,1);bmh::update_club_table(native,first,first+1,2,1);
        for(size_t side=0;side<2;++side)for(size_t byte=0;byte<0x4a;++byte)
            check(projection.state.club_table[side*0x4a+byte]==native.club_table[(size_t(first)+side)*0x4a+byte],"local club number changed Original league/streak/record bookkeeping");
    }
    const auto a=source.membership[0].clubs[0],b=source.membership[0].clubs[1];
    auto stale_projection=AiMatchProjection::create(source,{a,b},base);auto stale=source;stale.development_balance=1;const auto before=stale;
    rejects([&]{stale_projection.commit_match_state(stale);},"stale development stream was overwritten");check(stale==before,"stale development rejection mutated state");
    auto invalid=AiMatchProjection::create(source,{a,b},base);invalid.state.club_table[0x2a]=255;invalid.state.club_table[0x2b]=255;
    auto untouched=source;rejects([&]{invalid.commit_match_state(untouched);},"unknown legacy name offset persisted as a statistics club ID");check(untouched==source,"invalid record reference partially committed");
}
void settled_rounds(const World& world,const bmh::GameState& base,const AiLeagueConditions& conditions) {
    bmh::set_fixture_tables(bmh::test::authored_fixture_tables());
    for(uint32_t league:{0u,5u,9u,12u}) {
        const auto fixtures=league_fixtures(world,{league},1);
        std::vector<AiLeagueConditions> inputs(fixtures.size(),conditions);
        for(size_t i=0;i<inputs.size();++i)inputs[i].spectators+=uint32_t(i)*1000;
        auto a=world,b=world;bmh::GameRandom ag(123456),bg(123456);bmh::WatcomRandom aw(91),bw(91);
        const auto result=simulate_and_settle_ai_league_round(a,base,{league},1,inputs,ag,aw);
        const auto repeated=simulate_and_settle_ai_league_round(b,base,{league},1,inputs,bg,bw);
        check(a==b && ag.seed()==bg.seed() && aw.seed()==bw.seed() && result.game_draws==repeated.game_draws && result.watcom_draws==repeated.watcom_draws,
            "shared-clock round and post-match settlement are not deterministic");
        check(result.matches.size()==fixtures.size() && result.game_draws>0 && result.watcom_draws>0,"incomplete settled round");
        for(size_t i=0;i<fixtures.size();++i)check(result.matches[i].fixture==fixtures[i] && result.matches[i].settled && result.matches[i].score.played && result.matches[i].end_minute>=90 &&
            result.matches[i].game_draws==0 && result.matches[i].watcom_draws==0,"round identities, end times or RNG ownership lost");
        CareerSnapshot snapshot;snapshot.world=a;snapshot.results=SeasonResults::empty(a.catalogue);snapshot.date=conditions.date;snapshot.game_seed=ag.seed();snapshot.watcom_seed=aw.seed();
        check(read_save(make_save(snapshot))==snapshot,"settled round records and development lost in snapshot");
        auto untouched=world;auto bad=inputs;bad.back().date++;
        bmh::GameRandom game(17);bmh::WatcomRandom random(91);
        rejects([&]{simulate_and_settle_ai_league_round(untouched,base,{league},1,bad,game,random);},"mixed-day batch accepted");
        check(untouched==world && game.seed()==17 && game.calls()==0 && random.seed()==91 && random.calls()==0,"invalid late fixture partially committed the round");
    }
}
void card_and_development_edges(const World& world,const bmh::GameState& base) {
    const auto a=world.membership[0].clubs[0],b=world.membership[0].clubs[1];
    auto projection=AiMatchProjection::create(world,{a,b},base);auto& state=projection.state;
    const auto booked=state.squad_slots[0]&0xfff,injured=state.squad_slots[1]&0xfff;
    state.players[booked].w364|=0x1000;
    bmh::reset_match_played_flags(state);check(!(state.players[booked].w364&0x1000),"previous match participation leaked into kick-off");
    state.players[booked].w364|=0x1000;state.players[booked].w368=(state.players[booked].w368&~0x3c000u)|(5u<<14);
    state.squad_slots[0]|=0x1000;state.players[injured].w364=(state.players[injured].w364&~0x1f1000u)|(2u<<16);
    bmh::MatchGroup group;group.desc.assign(0xd0,0);group.fixtures={0,1};group.results={0x10};
    bmh::GameRandom game(123456);bmh::WatcomRandom random(91);bmh::MinuteReport report;
    bmh::process_group(state,group,game,random,report);check(!report.unsupported,"card/healing settlement failed");
    check(((state.players[booked].w364>>16)&15)==1 && !(state.squad_slots[0]&0x1000),"fifth yellow did not create the next-match suspension or clear match booking flag");
    check((state.players[booked].w364>>26)==1,"played league appearance was not counted");
    check(((state.players[injured].w364>>16)&15)==1,"injury did not heal by one match");
    bool wrapped=false;
    for(int seed=123456;seed<123520 && !wrapped;++seed) {
        auto extreme=AiMatchProjection::create(world,{a,b},base);extreme.state.development_balance=std::numeric_limits<int32_t>::max();
        extreme.state.team_flags[0]=extreme.state.team_flags[1]=4;bmh::GameRandom g(seed);bmh::WatcomRandom w(91);
        bmh::develop_players(extreme.state,group,0,g,w);
        if(extreme.state.development_balance<0) {
            auto persisted=world;extreme.commit_match_state(persisted);
            check(decode_world(encode_world(persisted))==persisted,"signed development wrap failed codec roundtrip");wrapped=true;
        }
    }
    check(wrapped,"Original development balance does not wrap deterministically at 32 bits");
}
}
int main()try {
    using namespace bmh::world12;
    const auto world=source_world();const auto base=rules();projection_state(world,base);rating_matches_legacy_positions(world,base);english_foreign_limit(world,base);
    AiLeagueConditions conditions;conditions.month=8;conditions.temperature=19;conditions.rain=0;
    conditions.spectators=15000;conditions.date=40;conditions.referee_properties={24};conditions.presentation_buttons.assign(0xb4,1);
    settlement_matches_core(world,base,conditions);
    settled_rounds(world,base,conditions);
    card_and_development_edges(world,base);
    const Fixture fixture{world.membership[0].clubs[0],world.membership[0].clubs[1]};
    int goals=0,changed=0;
    for(uint32_t seed=1;seed<=16;++seed) {
        const uint32_t league=seed<=4?0:seed<=8?9:seed<=12?10:12;
        const Fixture selected_fixture{world.membership[league].clubs[0],world.membership[league].clubs[1]};
        auto first=world,second=world;bmh::GameRandom ag(int32_t(seed*17)),bg(int32_t(seed*17));bmh::WatcomRandom aw(seed),bw(seed);
        const auto a=simulate_ai_league_match(first,base,selected_fixture,conditions,ag,aw);
        const auto b=simulate_ai_league_match(second,base,selected_fixture,conditions,bg,bw);
        check(a.score==b.score && a.end_minute==b.end_minute && first==second && ag.seed()==bg.seed() && aw.seed()==bw.seed(),"actual Original scoring phases are not reproducible");
        check(a.score.played && a.end_minute>=90 && a.end_minute<=94 && a.game_draws>0 && a.watcom_draws>0,"match used a supplied score or failed to reach full time");
        goals+=a.score.home+a.score.away;changed+=first!=world;
    }
    check(goals>0 && changed>0,"genuine scoring/events did not change any match state");
    for(const auto country:{Country::germany,Country::england}) {
        const uint32_t league=country==Country::germany?0:5;
        const Fixture same{world.membership[league].clubs[0],world.membership[league].clubs[1]};
        auto combined=world,selected=select_world_scope(world,{country==Country::germany,country==Country::england,false});
        bmh::GameRandom ag(37),bg(37);bmh::WatcomRandom aw(91),bw(91);
        const auto a=simulate_and_settle_ai_league_match(combined,base,same,conditions,ag,aw),b=simulate_and_settle_ai_league_match(selected,base,same,conditions,bg,bw);
        check(a.score==b.score && a.end_minute==b.end_minute && ag.seed()==bg.seed() && aw.seed()==bw.seed() && combined.development_balance==selected.development_balance && combined.trend_rotation==selected.trend_rotation,"country exclusion changed a retained fixture's scoring, settlement or RNG");
        for(auto club:{same.home,same.away}) {
            check(combined.clubs.at(club)==selected.clubs.at(club),"country exclusion changed retained club match state");
            for(const auto& slot:selected.clubs.at(club).roster)if(!slot.empty())check(combined.players.at(slot.player)==selected.players.at(slot.player),"country exclusion changed retained player match state");
        }
    }
    auto rejected=world;auto missing=conditions;missing.referee_properties.clear();bmh::GameRandom game(17);bmh::WatcomRandom random(91);
    rejects([&]{simulate_ai_league_match(rejected,base,fixture,missing,game,random);},"missing referee accepted");
    check(rejected==world && game.seed()==17 && random.seed()==91 && game.calls()==0 && random.calls()==0,"failed simulation mutated the world or random streams");
    auto bad_rules=base;bad_rules.formations[0]=255;
    rejects([&]{simulate_ai_league_match(rejected,bad_rules,fixture,conditions,game,random);},"malformed formation used for unchecked indexing");
    check(rejected==world && game.seed()==17 && random.seed()==91 && game.calls()==0 && random.calls()==0,"bad formation changed the world or random streams");
    const auto previous_hook=bmh::draw_hook;interrupted_world=&rejected;interrupted_club=fixture.home;bmh::draw_hook=change_live_owner;
    bool stale_rejected=false;
    try{simulate_and_settle_ai_league_match(rejected,base,fixture,conditions,game,random);}catch(const std::exception&){stale_rejected=true;}
    bmh::draw_hook=previous_hook;auto external_change=world;++external_change.clubs.at(fixture.home).morale;
    check(stale_rejected && rejected==external_change && game.seed()==17 && random.seed()==91 && game.calls()==0 && random.calls()==0,
        "late stale-state rejection retained simulated mutations/RNG or rolled back the external change");
    rejected=world;auto expected_external=game;expected_external.range(0,1);interrupted_game=&game;bmh::draw_hook=change_live_random;
    bool rng_rejected=false;
    try{simulate_ai_league_match(rejected,base,fixture,conditions,game,random);}catch(const std::exception&){rng_rejected=true;}
    bmh::draw_hook=previous_hook;
    check(rng_rejected && rejected==world && game.seed()==expected_external.seed() && game.calls()==expected_external.calls() && random.seed()==91 && random.calls()==0,
        "external random-stream change was overwritten or partially published simulated state");
    std::cout<<"AI scoring, Original post-match equivalence, four shared-clock rounds, records/card/healing and rollback passed; 16 scoring scenarios goals="<<goals<<"\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
