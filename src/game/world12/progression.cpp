// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/progression.hpp"
#include <stdexcept>
#include <limits>
namespace bmh::world12 {
std::optional<DayTicket> prepare_day(const CareerSnapshot& state){
    if(!state.calendar)throw std::invalid_argument("world12: country calendar is not installed");const auto& cursor=*state.calendar;
    if(cursor.next_day==cursor.schedule.days)return std::nullopt;
    if(cursor.next_day>cursor.schedule.days)throw std::invalid_argument("world12: invalid calendar cursor");
    DayTicket ticket;ticket.day=cursor.next_day;ticket.revision=cursor.revision;ticket.others=cursor.schedule.others_on(ticket.day);
    for(const auto&a:cursor.schedule.leagues_on(ticket.day)){if(state.league_rounds.at(a.league.value)+1!=a.round)throw std::invalid_argument("world12: league cursor disagrees with calendar");ticket.leagues.push_back({a,league_fixtures(state.world,a.league,a.round)});}
    return ticket;
}
void commit_league_day(CareerSnapshot& state,const DayTicket& ticket,const std::vector<CompletedLeagueRound>& completed){
    const auto expected=prepare_day(state);if(!expected||*expected!=ticket)throw std::invalid_argument("world12: stale or changed day ticket");
    if(!ticket.others.empty())throw std::invalid_argument("world12: cup/national transaction is required before advancing this day");
    if(state.date==std::numeric_limits<uint32_t>::max()||state.calendar->revision==std::numeric_limits<uint64_t>::max())throw std::overflow_error("world12: date/revision exhausted");
    if(completed.size()!=ticket.leagues.size())throw std::invalid_argument("world12: incomplete day results");
    // Validate the entire result batch before writing any table or cursor.
    for(size_t i=0;i<completed.size();++i){const auto& got=completed[i];const auto& plan=ticket.leagues[i];if(got.league!=plan.appointment.league||got.round!=plan.appointment.round||got.results.size()!=plan.matches.size())throw std::invalid_argument("world12: score batch is for another competition/round");
        for(size_t m=0;m<got.results.size();++m){const auto&score=got.results[m];if(!score.played||score.home>15||score.away>15||state.results.result(state.world.catalogue,got.league,got.round,int(m)).played)throw std::invalid_argument("world12: missing, invalid or already committed result");}}
    for(const auto& round:completed){for(size_t m=0;m<round.results.size();++m)state.results.result(state.world.catalogue,round.league,round.round,int(m))=round.results[m];state.league_rounds[round.league.value]=round.round;}
    ++state.calendar->next_day;++state.calendar->revision;++state.date;
}
}
