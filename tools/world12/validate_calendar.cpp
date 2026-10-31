// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/calendar.hpp"
#include "game/calendar.hpp"
#include "formats/le.hpp"
#include "formats/executable_layout.hpp"
#include "launch/fltd.hpp"
#include <iostream>
int main(int argc,char**argv)try{
 if(argc!=3)throw std::runtime_error("usage: world12_calendar_validate <German rules directory> <supplied FLTD.EXE>");
 const auto de=bmh::load_le(std::filesystem::path(argv[1])/"BMH.exe"),en=bmh::launch::translate_fltd(argv[2]);
 std::vector<uint16_t> german(366),english(366);for(uint32_t day=0;day<366;++day){german[day]=de.u16(bmh::layout::CompetitionCalendar+day*2);english[day]=en.u16(bmh::layout::CompetitionCalendar+day*2);}
 const auto german_epoch=bmh::calendar_epoch_from_executable(de);
 const auto english_epoch=bmh::calendar_epoch_from_fltd(bmh::read_file(argv[2]));
 const auto aligned=bmh::world12::align_source_calendar(english,english_epoch.weekday,german_epoch.weekday,uint16_t(german.size()));
 const auto catalogue=bmh::world12::Catalogue::dual_classic();const auto calendar=bmh::world12::SeasonCalendar::from_sources(catalogue,german,aligned.entries);
 for(const auto&league:catalogue.leagues){int rounds=0;for(const auto&a:calendar.leagues)rounds+=a.league==league.id;std::cout<<league.name<<": "<<rounds<<" rounds\n";}
 std::cout<<"German source epoch: 1 July "<<german_epoch.year<<", weekday "<<german_epoch.weekday<<"\n"
          <<"English source epoch: 1 July "<<english_epoch.year<<", weekday "<<english_epoch.weekday<<"\n"
          <<"English weekday alignment: "<<aligned.day_shift<<" days\n"
          <<"Non-league source appointments: "<<calendar.others.size()<<"\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
