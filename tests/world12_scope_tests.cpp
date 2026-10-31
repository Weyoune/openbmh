// SPDX-License-Identifier: GPL-3.0-only
#include "world12_fixture.hpp"
#include "fixture_tables.hpp"
#include "app/world12_cups.hpp"
#include "game/world12/codec.hpp"
#include "cli/json.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace bmh::world12;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class Function> void rejects(Function function,const char* message) {
    bool rejected=false;try{function();}catch(const std::exception&){rejected=true;}check(rejected,message);
}
World source_world() {
    auto world=bmh::test::world12::complete_world();
    for(uint32_t i=0;i<2;++i) {
        ClubRecord national;national.id={900000+i};national.name="National "+std::to_string(i);
        national.kind=ClubKind::national_team;national.country=i?Country::england:Country::germany;
        national.crest={900000+i};world.crests[national.crest]={"W/TEST.VGA",0};world.clubs[national.id]=national;
        ClubRecord extra;extra.id={800000+i};extra.name="Amateur "+std::to_string(i);extra.country=national.country;
        extra.crest={800000+i};world.crests[extra.crest]={"W/TEST.VGA",0};world.clubs[extra.id]=extra;
    }
    auto free=world.players.begin()->second;free.id={999999};free.attributes.name="Independent pool player";world.players[free.id]=free;
    world.validate();return world;
}
}
int main()try {
    using namespace bmh::world12;
    bmh::set_fixture_tables(bmh::test::authored_fixture_tables());
    const auto master=source_world();
    const auto german=bmh::test::world12::country_rules(false),english=bmh::test::world12::country_rules(true);
    std::vector<uint16_t> de(365),en(365);
    for(int r=0;r<34;++r)de[size_t(20+r*8)]=15;
    for(int r=0;r<38;++r)en[size_t(20+r*8)]=15;
    for(int mask=1;mask<8;++mask) {
        const GameScope scope{bool(mask&1),bool(mask&2),bool(mask&4)};
        const auto world=select_world_scope(master,scope);
        const size_t league_clubs=scope.club_play()?54+90*scope.germany+(scope.england?90:20):0;
        check(world.catalogue.league_clubs()==league_clubs,"scoped league club count wrong");
        check(world.catalogue.table_leagues(TableGroup::germany).size()==size_t(scope.germany?5:0) &&
            world.catalogue.table_leagues(TableGroup::england).size()==size_t(scope.england?5:0),"disabled country still has table pages");
        const auto europe=world.catalogue.table_leagues(TableGroup::europe);
        const std::vector<LeagueId> expected_europe=scope.club_play()?
            (scope.england?std::vector<LeagueId>{{10},{11},{12}}:std::vector<LeagueId>{{10},{5},{11},{12}}):std::vector<LeagueId>{};
        check(europe==expected_europe,"international table order or Premier League fallback wrong");
        const auto& premier=world.catalogue.league({5});
        check(premier.active==scope.club_play() && premier.playable==scope.england,"Premier League activity and manager selection confused");
        if(scope.club_play()&&!scope.england) {
            check(premier.relegation==0 && premier.rounds()==38,"AI-only Premier League must have no domestic relegation");
            check(world.membership[5]==master.membership[5],"Premier League fallback changed club identities");
        }
        for(const auto& [id,club]:world.clubs) {
            if(club.kind==ClubKind::club)check(scope.club_enabled(club.country,club.league),"excluded country survived as a foreign/AI club");
            else check(scope.international,"national team survived disabled International scope");
            check(master.clubs.at(id)==club,"surviving club ID or data changed");
        }
        for(const auto& [id,player]:world.players)check(master.players.at(id)==player,"scope filtering renumbered players");
        check(world.players.size()==(scope.club_play()?league_clubs*11+1:master.players.size()),"removed squads became free agents or national-only pool was discarded");
        check(world.clubs.contains({800000})==scope.germany && world.clubs.contains({800001})==scope.england,"excluded domestic amateur fallback survived");
        check(world.clubs.contains({900000})==scope.international,"International toggle removed/retained wrong national entities");
        check(decode_world(encode_world(world))==world,"scoped world codec roundtrip lost selection");
        if(!scope.england)rejects([&]{world.selectable_clubs(Country::england,{5});},"disabled England remains selectable");
        if(!scope.germany)rejects([&]{world.selectable_clubs(Country::germany,{0});},"disabled Germany remains selectable");
        CareerSnapshot state;state.world=world;state.results=SeasonResults::empty(world.catalogue);
        state.calendar=CalendarCursor{SeasonCalendar::from_sources(world.catalogue,de,en),0,0};
        size_t matches=0;
        while(const auto ticket=prepare_day(state)) {
            std::vector<CompletedLeagueRound> completed;
            for(const auto& round:ticket->leagues) {
                check(world.catalogue.league(round.appointment.league).active,"disabled country plays a calendar round");
                matches+=round.matches.size();
                completed.push_back({round.appointment.league,round.appointment.round,std::vector<LeagueResult>(round.matches.size(),{true,1,0})});
            }
            commit_league_day(state,*ticket,completed);
        }
        const size_t expected=scope.club_play()?918+1530*scope.germany+(scope.england?1610:380):0;
        check(matches==expected,"disabled-country league results were simulated or retained");
        check(read_save(make_save(state))==state,"scoped calendar/results/save ownership lost");
        auto cups=CareerSnapshot{};cups.world=world;cups.results=SeasonResults::empty(world.catalogue);
        CountryCupRanking de_ranks,en_ranks;
        if(scope.germany)de_ranks=bmh::test::world12::membership_ranking(world,Country::germany);
        if(scope.england)en_ranks=bmh::test::world12::membership_ranking(world,Country::england);
        bmh::app::world12::install_domestic_cup_fields(cups,de_ranks,en_ranks,{german,english});
        check(cups.cups.size()==size_t(scope.germany)+2*size_t(scope.england),"disabled country initialized cups");
        auto de_extra=de,en_extra=en;de_extra[2]=en_extra[2]=0x2020;
        de_extra[3]=en_extra[3]=0x4002;de_extra[4]=en_extra[4]=0x4006;de_extra[5]=en_extra[5]=0x1fc0;
        const auto calendar=SeasonCalendar::from_sources(world.catalogue,de_extra,en_extra);
        for(const auto& appointment:calendar.others) {
            check(appointment.domain!=CalendarDomain::germany||scope.germany,"disabled German calendar event survived");
            check(appointment.domain!=CalendarDomain::england||scope.england,"disabled English calendar event survived");
            if(appointment.domain==CalendarDomain::shared) {
                if(appointment.source_entry==0x4006)check(scope.international,"national friendly survived disabled International");
                else {check(scope.international||!(appointment.source_entry&0x1e00),"national tournament survived disabled International");
                    check(scope.club_play()||!(appointment.source_entry&0x1c0),"European club event survived national-only play");}
            }
        }
    }
    rejects([&]{select_world_scope(master,{false,false,false});},"empty game scope accepted");
    auto active=master;active.managers={{0,"Manager",active.membership[0].clubs[0],{0},0}};
    rejects([&]{select_world_scope(active,{true,false,false});},"running manager world was destructively re-scoped");
    auto germany=select_world_scope(master,{true,false,false});
    auto leak=master.clubs.at({800001});leak.crest=germany.clubs.begin()->second.crest;germany.clubs[leak.id]=leak;
    rejects([&]{germany.validate();},"excluded English club accepted without a league as fallback");
    auto filtered=select_world_scope(master,{true,false,false});const auto filtered_raw=encode_world(filtered);
    auto forged=bmh::cli::parse_json(std::string(filtered_raw.begin(),filtered_raw.end()));
    forged.object()->at("scope").object()->at("england")=true;const auto forged_raw=bmh::cli::dump_json(forged);
    rejects([&]{decode_world(bmh::Bytes(forged_raw.begin(),forged_raw.end()));},"scope flags re-enabled missing country entities in a save");
    filtered.managers={{0,"Manager",filtered.membership[0].clubs[0],{0},0}};filtered.validate();
    rejects([&]{filtered.national_coach_country(0);},"disabled International still offers a national-coach appointment");
    auto raw=encode_world(master);auto json=bmh::cli::parse_json(std::string(raw.begin(),raw.end()));
    json.object()->at("version")=1;json.object()->erase("scope");const auto legacy=bmh::cli::dump_json(json);
    check(decode_world(bmh::Bytes(legacy.begin(),legacy.end()))==master,"full-world development v1 database compatibility lost");
    std::cout<<"Seven scope combinations, absent domestic/fallback clubs, dynamic calendars/cups, stable IDs and scoped saves passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
