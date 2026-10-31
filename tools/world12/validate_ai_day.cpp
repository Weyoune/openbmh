// SPDX-License-Identifier: GPL-3.0-only
#include "app/world12_ai_day.hpp"
#include "game/world12/codec.hpp"
#include "game/calendar.hpp"
#include "game/career.hpp"
#include "game/schedule.hpp"
#include "formats/data_profile.hpp"
#include "formats/executable_layout.hpp"
#include "formats/le.hpp"
#include "launch/fltd.hpp"
#include <chrono>
#include <iostream>

int main(int argc,char** argv)try {
    using namespace bmh;using namespace bmh::world12;
    namespace app=bmh::app::world12;
    if(argc!=4)throw std::runtime_error("usage: world12_day_validate <WORLD12.JSON> <German rules directory> <supplied FLTD.EXE>");
    const auto master=decode_world(read_file(argv[1]));const std::filesystem::path data=argv[2];
    const auto profile=load_data_profile(data);install_runtime_profile(profile);
    const auto german=load_le(data/"BMH.exe"),english=bmh::launch::translate_fltd(argv[3]);
    install_career_rules(german);install_fixture_tables(german);
    const auto database=load_database(data/"M3D.MAN",profile.counts);
    const auto base=startup_state(german,database,read_file(data/"M3.MEN"),read_file(data/"M3.DAT"));
    std::vector<uint16_t> de(366),en(366);
    for(size_t i=0;i<de.size();++i){de[i]=german.u16(layout::CompetitionCalendar+uint32_t(i*2));en[i]=english.u16(layout::CompetitionCalendar+uint32_t(i*2));}
    const auto epoch=calendar_epoch_from_executable(german),en_epoch=calendar_epoch_from_fltd(read_file(argv[3]));
    en=align_source_calendar(en,en_epoch.weekday,epoch.weekday,366).entries;
    const auto first=std::chrono::sys_days{std::chrono::year{epoch.year}/7/1};
    for(const auto scope:{GameScope{},GameScope{true,false,true},GameScope{false,true,true}}) {
        CareerSnapshot state;state.world=select_world_scope(master,scope);
        auto calendar=SeasonCalendar::from_sources(state.world.catalogue,de,en);
        const auto excluded=calendar.others.size();calendar.others.clear();
        state.calendar=CalendarCursor{calendar,0,0};state.results=SeasonResults::empty(state.world.catalogue);
        state.game_seed=123456;state.watcom_seed=91;app::AiDayOwner owner{71,0};
        int played_dates=0;size_t matches=0;
        while(played_dates<3) {
            const auto ticket=prepare_day(state);if(!ticket)throw std::runtime_error("not enough match dates in source calendar");
            std::vector<app::AiDayLeagueConditions> conditions;
            AiLeagueConditions c;c.month=int(unsigned(std::chrono::year_month_day{first+std::chrono::days{state.date}}.month()));
            c.temperature=19;c.rain=0;c.spectators=15000;c.date=state.date;
            c.referee_properties=database.raw_referee_properties;c.presentation_buttons.assign(0xb4,1);
            for(const auto& round:ticket->leagues)conditions.push_back({round.appointment,std::vector<AiLeagueConditions>(round.matches.size(),c)});
            const auto before=state;auto repeat=before;auto repeated_owner=owner;
            const app::AiDayIdentity identity{owner.scope,owner.revision};
            const auto result=app::play_ai_league_day(state,owner,identity,*ticket,base,conditions);
            app::play_ai_league_day(repeat,repeated_owner,identity,*ticket,base,conditions);
            if(state!=repeat || owner!=repeated_owner || read_save(make_save(state))!=state)
                throw std::runtime_error("source-backed day replay/snapshot mismatch");
            if(!result.leagues.empty()) {
                ++played_dates;size_t day_matches=0;for(const auto& round:result.leagues)day_matches+=round.matches.size();matches+=day_matches;
                std::cout<<"DE="<<scope.germany<<" EN="<<scope.england<<" day "<<ticket->day<<": "<<result.leagues.size()<<" groups, "<<day_matches
                    <<" actual matches; G="<<result.game_draws<<" W="<<result.watcom_draws<<"; repeat/snapshot passed\n";
            }
        }
        std::cout<<"Scope total: "<<matches<<" matches across three source league dates; "<<excluded<<" non-league appointments EXCLUDED from this isolated probe.\n";
    }
    std::cout<<"No human games, daily economy, cup/national progression or complete playable career is certified. Weather/attendance are explicit probe inputs.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
