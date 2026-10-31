// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/calendar.hpp"
#include <algorithm>
#include <set>
#include <tuple>
#include <stdexcept>
namespace bmh::world12 {
AlignedSourceCalendar align_source_calendar(const std::vector<uint16_t>& source,
    int source_weekday,int target_weekday,uint16_t target_days) {
    if ((source.size()!=365 && source.size()!=366) ||
        (target_days!=365 && target_days!=366) ||
        source_weekday<0 || source_weekday>6 || target_weekday<0 || target_weekday>6)
        throw std::invalid_argument("world12: invalid source calendar epoch");
    int shift=(source_weekday-target_weekday+7)%7;
    if (shift>3) shift-=7;
    AlignedSourceCalendar aligned{std::vector<uint16_t>(target_days),shift};
    for (size_t day=0;day<source.size();++day) {
        if (!source[day]) continue;
        const int destination=int(day)+shift;
        if (destination<0 || destination>=target_days)
            throw std::invalid_argument("world12: weekday alignment moves an appointment outside the season");
        aligned.entries[size_t(destination)]=source[day];
    }
    return aligned;
}
SeasonCalendar SeasonCalendar::from_sources(const Catalogue& catalogue,const std::vector<uint16_t>& de,const std::vector<uint16_t>& en){
    if(de.size()!=en.size()||(de.size()!=365&&de.size()!=366))throw std::invalid_argument("world12: source calendars must use the same full season epoch");
    SeasonCalendar result;result.days=uint16_t(de.size());std::array<std::vector<uint16_t>,14> dates;
    for(size_t day=0;day<de.size();++day){
        for(int country=0;country<2;++country){const auto raw=country==0?de[day]:en[day];
            const bool enabled=country==0?catalogue.scope.germany:catalogue.scope.england;
            if(!(raw&0x4000)){
                if(country==0){for(uint32_t l=0;l<5;++l){const auto bit=l<2?l:l<4?2u:3u;if(raw&(1u<<bit))dates[l].push_back(uint16_t(day));}}
                else for(uint32_t l=0;l<4;++l)if(raw&(1u<<l))dates[5+l].push_back(uint16_t(day));
                // All European and national competition flags are owned by one
                // shared source, never replayed independently for each country.
                const auto domestic=uint16_t(raw&0x2020u); // national cup and national supercup
                const auto shared=uint16_t(raw&((catalogue.scope.club_play()?0x01c0u:0u)|(catalogue.scope.international?0x1e00u:0u)));
                if(enabled&&domestic)result.others.push_back({uint16_t(day),country==0?CalendarDomain::germany:CalendarDomain::england,domestic});
                if(country==0 && shared)result.others.push_back({uint16_t(day),CalendarDomain::shared,shared});
                if(raw&0x8000u)throw std::invalid_argument("world12: source calendar contains runtime postponements");
            }else if(raw){
                if(raw==0x4006){if(country==0&&catalogue.scope.international)result.others.push_back({uint16_t(day),CalendarDomain::shared,raw});}
                else if(enabled)result.others.push_back({uint16_t(day),country==0?CalendarDomain::germany:CalendarDomain::england,raw});
            }
        }
    }
    if(dates[0].size()!=34||dates[5].size()!=38)throw std::invalid_argument("world12: primary source calendars lack required rounds");
    for(size_t r=0;r<18;++r)dates[9].push_back(dates[5].at(((2*r+1)*dates[5].size())/36));
    // The European AI leagues use the German league cadence. This avoids
    // copying the former 22/38-round foreign calendar onto 18-team leagues.
    for(size_t l=10;l<14;++l)dates[l]=dates[0];
    for(uint32_t l=0;l<14;++l){if(!catalogue.leagues[l].active)continue;if(dates[l].size()!=size_t(catalogue.leagues[l].rounds()))throw std::invalid_argument("world12: incomplete country league calendar");for(size_t r=0;r<dates[l].size();++r)result.leagues.push_back({dates[l][r],{l},uint8_t(r+1)});}
    std::sort(result.leagues.begin(),result.leagues.end(),[](const auto&a,const auto&b){return a.day!=b.day?a.day<b.day:a.league<b.league;});
    std::sort(result.others.begin(),result.others.end(),[](const auto&a,const auto&b){return a.day!=b.day?a.day<b.day:a.domain!=b.domain?a.domain<b.domain:a.source_entry<b.source_entry;});result.validate(catalogue);return result;
}
void SeasonCalendar::validate(const Catalogue& catalogue)const {
    if(days!=365&&days!=366)throw std::invalid_argument("world12: invalid season length");
    std::array<uint8_t,14> rounds{};std::array<int,14> last;last.fill(-1);std::set<std::pair<uint16_t,LeagueId>> occupied;
    for(const auto& a:leagues){const auto& l=catalogue.league(a.league);if(a.day>=days||a.round!=rounds[a.league.value]+1||a.round>l.rounds()||int(a.day)<=last[a.league.value]||!occupied.emplace(a.day,a.league).second)throw std::invalid_argument("world12: duplicate/out-of-order league appointment");rounds[a.league.value]=a.round;last[a.league.value]=a.day;}
    for(size_t l=0;l<14;++l)if(rounds[l]!=catalogue.leagues[l].rounds())throw std::invalid_argument("world12: missing season rounds");
    std::set<std::tuple<uint16_t,CalendarDomain,uint16_t>> extra;
    for(const auto&a:others)if(a.day>=days||uint8_t(a.domain)>2||!a.source_entry||!extra.emplace(a.day,a.domain,a.source_entry).second)throw std::invalid_argument("world12: invalid duplicate non-league appointment");
    for(const auto& a:others) {
        if((a.domain==CalendarDomain::germany&&!catalogue.scope.germany)||(a.domain==CalendarDomain::england&&!catalogue.scope.england))
            throw std::invalid_argument("world12: calendar contains a disabled country's competition");
        if(a.domain==CalendarDomain::shared &&
            ((a.source_entry==0x4006&&!catalogue.scope.international)||
             (!(a.source_entry&0x4000)&&((!catalogue.scope.international&&(a.source_entry&0x1e00))||(!catalogue.scope.club_play()&&(a.source_entry&0x1c0))))))
            throw std::invalid_argument("world12: shared calendar event belongs to a disabled game scope");
    }
}
std::vector<LeagueAppointment> SeasonCalendar::leagues_on(uint16_t day)const{if(day>=days)throw std::out_of_range("world12: calendar day outside season");std::vector<LeagueAppointment> out;for(const auto&a:leagues)if(a.day==day)out.push_back(a);return out;}
std::vector<OtherAppointment> SeasonCalendar::others_on(uint16_t day)const{if(day>=days)throw std::out_of_range("world12: calendar day outside season");std::vector<OtherAppointment> out;for(const auto&a:others)if(a.day==day)out.push_back(a);return out;}
}
