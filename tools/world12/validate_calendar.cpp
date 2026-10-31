// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/calendar.hpp"
#include "formats/le.hpp"
#include "formats/executable_layout.hpp"
#include "launch/fltd.hpp"
#include <iostream>
int main(int argc,char**argv)try{
 if(argc!=3)throw std::runtime_error("usage: world12_calendar_validate <German rules directory> <supplied FLTD.EXE>");
 const auto de=bmh::load_le(std::filesystem::path(argv[1])/"BMH.exe"),en=bmh::launch::translate_fltd(argv[2]);
 std::vector<uint16_t> german(366),english(366);for(uint32_t day=0;day<366;++day){german[day]=de.u16(bmh::layout::CompetitionCalendar+day*2);english[day]=en.u16(bmh::layout::CompetitionCalendar+day*2);}
 const auto catalogue=bmh::world12::Catalogue::dual_classic();const auto calendar=bmh::world12::SeasonCalendar::from_sources(catalogue,german,english);
 for(const auto&league:catalogue.leagues){int rounds=0;for(const auto&a:calendar.leagues)rounds+=a.league==league.id;std::cout<<league.name<<": "<<rounds<<" rounds\n";}
 std::cout<<"Non-league source appointments: "<<calendar.others.size()<<"; source days are not yet realigned to a shared weekday epoch\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
