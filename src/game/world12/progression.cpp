// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/progression.hpp"
#include <stdexcept>
#include <limits>
#include <algorithm>
#include <set>
#include <type_traits>
namespace bmh::world12 {
static_assert(std::is_nothrow_move_assignable_v<CareerSnapshot>);
std::optional<DayTicket> prepare_day(const CareerSnapshot& state){
    if(!state.calendar)throw std::invalid_argument("world12: country calendar is not installed");const auto& cursor=*state.calendar;
    if(cursor.next_day==cursor.schedule.days)return std::nullopt;
    if(cursor.next_day>cursor.schedule.days)throw std::invalid_argument("world12: invalid calendar cursor");
    DayTicket ticket;ticket.day=cursor.next_day;ticket.revision=cursor.revision;ticket.others=cursor.schedule.others_on(ticket.day);
    for(const auto&a:cursor.schedule.leagues_on(ticket.day)){if(state.league_rounds.at(a.league.value)+1!=a.round)throw std::invalid_argument("world12: league cursor disagrees with calendar");ticket.leagues.push_back({a,league_fixtures(state.world,a.league,a.round)});}
    std::set<DomesticCup> scheduled;
    for(const auto& appointment:ticket.others) if(const auto required=domestic_cups_on(appointment))
        for(auto cup:*required) {
            if(!scheduled.insert(cup).second) throw std::invalid_argument("world12: domestic cup scheduled twice on one day");
            const auto stage=std::find_if(state.cups.rbegin(),state.cups.rend(),[&](const auto& s){return s.cup==cup;});
            if(stage!=state.cups.rend() && !stage->complete()) {
                stage->validate(state.world);
                ticket.cups.push_back({cup,stage->source_entry,stage->next_leg(),stage->revision,stage->next_fixtures()});
            }
        }
    return ticket;
}
void commit_league_day(CareerSnapshot& state,const DayTicket& ticket,const std::vector<CompletedLeagueRound>& completed){
    commit_day(state,ticket,completed,{});
}
void commit_day(CareerSnapshot& state,const DayTicket& ticket,const std::vector<CompletedLeagueRound>& completed,
    const std::vector<CupLegResult>& cup_results){
    const auto expected=prepare_day(state);if(!expected||*expected!=ticket)throw std::invalid_argument("world12: stale or changed day ticket");
    size_t required_count=0;
    for(const auto& appointment:ticket.others) {
        const auto required=domestic_cups_on(appointment);
        if(!required) throw std::invalid_argument("world12: indoor/shared/national transaction is required before advancing this day");
        required_count+=required->size();
    }
    if(required_count!=ticket.cups.size() || cup_results.size()!=ticket.cups.size())
        throw std::invalid_argument("world12: complete installed domestic cup stages and results are required");
    if(state.date==std::numeric_limits<uint32_t>::max()||state.calendar->revision==std::numeric_limits<uint64_t>::max())throw std::overflow_error("world12: date/revision exhausted");
    if(completed.size()!=ticket.leagues.size())throw std::invalid_argument("world12: incomplete day results");
    // Validate the entire result batch before writing any table or cursor.
    for(size_t i=0;i<completed.size();++i){const auto& got=completed[i];const auto& plan=ticket.leagues[i];if(got.league!=plan.appointment.league||got.round!=plan.appointment.round||got.results.size()!=plan.matches.size())throw std::invalid_argument("world12: score batch is for another competition/round");
        for(size_t m=0;m<got.results.size();++m){const auto&score=got.results[m];if(!score.played||score.home>15||score.away>15||state.results.result(state.world.catalogue,got.league,got.round,int(m)).played)throw std::invalid_argument("world12: missing, invalid or already committed result");}}
    auto staged=state;
    for(size_t i=0;i<cup_results.size();++i) {
        const auto& result=cup_results[i]; const auto& plan=ticket.cups[i];
        if(result.cup!=plan.cup || result.source_entry!=plan.source_entry || result.revision!=plan.revision)
            throw std::invalid_argument("world12: score batch is for another cup stage/leg");
        auto stage=std::find_if(staged.cups.rbegin(),staged.cups.rend(),[&](const auto& s){return s.cup==result.cup;});
        commit_cup_leg(*stage,result,staged.world);
        if(stage->complete() && stage->next_entry) {
            if(!result.next_stage || result.next_stage->cup!=stage->cup ||
                result.next_stage->source_entry!=stage->next_entry || result.next_stage->revision!=0)
                throw std::invalid_argument("world12: linked cup stage and its original draw are required");
            staged.cups.push_back(*result.next_stage);
        } else if(result.next_stage)throw std::invalid_argument("world12: unexpected linked cup stage");
    }
    for(const auto& round:completed){for(size_t m=0;m<round.results.size();++m)staged.results.result(staged.world.catalogue,round.league,round.round,int(m))=round.results[m];staged.league_rounds[round.league.value]=round.round;}
    ++staged.calendar->next_day;++staged.calendar->revision;++staged.date;
    validate_cup_programmes(staged.world,staged.cups);
    state=std::move(staged);
}
}
