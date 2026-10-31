// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/standings.hpp"
#include "game/table.hpp"
#include <algorithm>
#include <stdexcept>

namespace bmh::world12 {
std::vector<StandingRow> compute_standings(const World& world,const SeasonResults& results,
    LeagueId id,int rounds_played,const TableQuery& query) {
    const auto& league=world.catalogue.league(id);
    const auto& membership=world.membership.at(id.value);
    const auto& members=membership.clubs;
    const int last=query.last_round==0 ? league.rounds() : query.last_round;
    if (membership.league!=id || members.size()!=league.clubs ||
        rounds_played<0 || rounds_played>league.rounds() ||
        query.first_round<1 || last<query.first_round || last>league.rounds() ||
        uint8_t(query.venue)>2 || query.suppressed_parts>7)
        throw std::invalid_argument("world12: invalid league table query");
    results.validate(world.catalogue);
    std::map<ClubId,int> positions;
    bmh::LeagueTable table; table.size=league.clubs;
    for (size_t i=0;i<members.size();++i) {
        const auto club=world.clubs.find(members[i]);
        if (club==world.clubs.end() || club->second.league!=id ||
            club->second.country!=league.country || !positions.emplace(members[i],int(i)).second)
            throw std::invalid_argument("world12: inconsistent table membership");
    }
    for (const auto& [club,deduction]:query.point_deductions) {
        const auto position=positions.find(club);
        if (position==positions.end() || deduction>31)
            throw std::invalid_argument("world12: invalid table point deduction");
        table.points[size_t(position->second)]=-int8_t(deduction);
    }
    // Match the original's rank-history checkpoints, including tables opened
    // before the first round. Do not replace ties with alphabetical sorting.
    const int last_round=rounds_played==0 ? 0 : rounds_played-1;
    const int before_last=last_round>1 ? rounds_played-2 : 0x80;
    const int flags=query.venue==TableVenue::home ? 1 : query.venue==TableVenue::away ? 2 : 3;
    for (int round=query.first_round;round<=last;++round) {
        const auto fixtures=league_fixtures(world,id,round);
        bool counted=false;
        for (size_t match=0;match<fixtures.size();++match) {
            const auto& score=results.result(world.catalogue,id,round,int(match));
            if (!score.played) continue;
            const int clubs[2]={positions.at(fixtures[match].home),positions.at(fixtures[match].away)};
            const uint8_t goals[2]={score.home,score.away};
            bmh::add_result(table,clubs,goals,flags,query.three_points ? 1 : 0);
            counted=true;
        }
        const int index=round-1;
        if (last<=index+2 || index==before_last || index==last_round || (last_round<index && counted))
            bmh::sort_table(table,query.suppressed_parts);
    }
    std::vector<StandingRow> rows; rows.reserve(members.size());
    for (size_t i=0;i<members.size();++i) {
        StandingRow row;
        row.club=members[i]; row.rank=uint8_t(table.rank(int(i))+1);
        row.previous_rank=uint8_t(table.previous_rank(int(i))+1);
        row.games=uint8_t(table.games(int(i))); row.wins=table.wins[i]; row.defeats=table.defeats[i];
        row.draws=uint8_t(row.games-row.wins-row.defeats);
        row.goals_for=table.goals_for[i]; row.goals_against=table.goals_against[i];
        row.points=uint8_t(std::max(0,int(table.points[i]))); row.form=table.form[i];
        rows.push_back(row);
    }
    std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.rank<b.rank;});
    return rows;
}
}
