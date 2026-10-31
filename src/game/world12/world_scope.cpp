// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/world.hpp"
#include <set>
#include <stdexcept>
namespace bmh::world12 {
World select_world_scope(const World& source,GameScope scope) {
    source.validate();scope.validate();
    if(source.catalogue.scope!=GameScope{} || !source.managers.empty() || !source.loans.empty())
        throw std::invalid_argument("world12: scope selection requires an untouched complete master world");
    if(source.development_balance!=0 || source.trend_rotation!=0)throw std::invalid_argument("world12: developed careers cannot be re-scoped");
    for(const auto& [id,club]:source.clubs)if(club.season_stats!=ClubSeasonStats{})throw std::invalid_argument("world12: played club records cannot be re-scoped");
    World selected=source;selected.catalogue=Catalogue::dual_classic(scope);
    std::set<PlayerId> removed_players,retained_players;
    for(auto it=selected.clubs.begin();it!=selected.clubs.end();) {
        const auto& club=it->second;
        const bool keep=club.kind==ClubKind::national_team?scope.international:scope.club_enabled(club.country,club.league);
        for(const auto& slot:club.roster)if(!slot.empty())(keep?retained_players:removed_players).insert(slot.player);
        if(!keep)it=selected.clubs.erase(it);else ++it;
    }
    for(auto& membership:selected.membership)if(!selected.catalogue.league(membership.league).active)membership.clubs.clear();
    // National-only play needs its independent nationality pool, but has no
    // club market. In club careers, removed squads do not become free agents.
    if(scope.club_play())for(auto id:removed_players)if(!retained_players.contains(id))selected.players.erase(id);
    std::set<CrestId> crests;for(const auto& [id,club]:selected.clubs)crests.insert(club.crest);
    for(auto it=selected.crests.begin();it!=selected.crests.end();)if(!crests.contains(it->first))it=selected.crests.erase(it);else ++it;
    selected.validate();return selected;
}
}
