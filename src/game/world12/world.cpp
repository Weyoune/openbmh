// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/world.hpp"
#include <set>
#include <stdexcept>
namespace bmh::world12 {
void validate_manager_tactics(const ManagerTactics& tactics) {
    if(tactics.automatic_formation>8)throw std::invalid_argument("world12: invalid automatic formation selector");
    for(size_t i=0;i<tactics.rows.size();++i) {
        const auto& row=tactics.rows[i];
        if((row.partner!=255 && row.partner>=24) ||
            (i<11 && (row.depth>19 || row.lane>8)))
            throw std::invalid_argument("world12: invalid tactical position or partner row");
    }
}
void World::validate(bool complete)const {
    catalogue.validate();if(trend_rotation>2)throw std::invalid_argument("world12: invalid development trend rotation");std::set<ClubId> league_clubs;std::set<PlayerId> rostered;
    for(size_t i=0;i<membership.size();++i){const auto& m=membership[i];const auto& league=catalogue.leagues[i];if(m.league!=league.id)throw std::invalid_argument("world12: league membership identity mismatch");if(complete&&m.clubs.size()!=(league.active?league.clubs:0))throw std::invalid_argument("world12: incomplete league membership");if(m.clubs.size()>(league.active?league.clubs:0))throw std::invalid_argument("world12: league capacity exceeded");
        for(auto id:m.clubs){const auto found=clubs.find(id);if(found==clubs.end()||!league_clubs.insert(id).second)throw std::invalid_argument("world12: missing/duplicate league club");const auto& club=found->second;if(club.kind!=ClubKind::club||club.country!=league.country||club.league!=league.id)throw std::invalid_argument("world12: club assigned to the wrong country/league");}
    }
    for(const auto&[id,record]:players){const auto& p=record.attributes;if(!id.valid()||id!=record.id||p.name.empty()||p.name.size()>255||p.form()>99||p.technique()>99||p.condition()>99||p.nationality()+((record.bonus&128)?32:0)>=62)throw std::invalid_argument("world12: invalid player");}
    for(const auto&[id,club]:clubs){if((club.kind==ClubKind::club&&!catalogue.scope.club_enabled(club.country,club.league))||(club.kind==ClubKind::national_team&&!catalogue.scope.international))throw std::invalid_argument("world12: club/national team belongs to a disabled game scope");if(!id.valid()||id!=club.id||club.name.empty()||club.name.size()>255||uint8_t(club.country)>=62||!club.crest.valid()||!crests.contains(club.crest)||uint8_t(club.kind)>1)throw std::invalid_argument("world12: invalid club");for(auto opponent:club.season_stats.opponents)if(opponent.valid()&&(!clubs.contains(opponent)||clubs.at(opponent).kind!=ClubKind::club))throw std::invalid_argument("world12: missing/non-club statistics opponent");if(club.morale>63)throw std::invalid_argument("world12: invalid club morale");if(club.league.valid()&&!league_clubs.contains(id))throw std::invalid_argument("world12: club league absent from membership");unsigned occupied=0;bool empty=false;for(const auto& slot:club.roster){if(slot.flags&~uint16_t(15))throw std::invalid_argument("world12: unknown roster flags");if(slot.empty()){if(slot.flags)throw std::invalid_argument("world12: flags on an empty roster slot");empty=true;continue;}if(empty||!players.contains(slot.player)||!rostered.insert(slot.player).second)throw std::invalid_argument("world12: missing, duplicate or non-compact roster player");++occupied;}if(complete&&club.league.valid()&&occupied<11)throw std::invalid_argument("world12: league club below minimum squad size");if(!club.league.valid()&&occupied)throw std::invalid_argument("world12: non-league club has a league roster");}
    for(const auto&[id,crest]:crests){const auto& path=crest.path;if(!id.valid()||crest.cell>=8||path.empty()||path.find("..")!=std::string::npos||path.find(':')!=std::string::npos||path[0]=='/'||path.find('\\')!=std::string::npos)throw std::invalid_argument("world12: invalid crest resource");}
    std::set<PlayerId> loaned;for(const auto& loan:loans){if(!players.contains(loan.player)||!clubs.contains(loan.owner)||!clubs.contains(loan.borrower)||loan.owner==loan.borrower||!loan.months||!loaned.insert(loan.player).second)throw std::invalid_argument("world12: invalid loan");bool at_borrower=false;for(const auto& slot:clubs.at(loan.borrower).roster)at_borrower|=slot.player==loan.player;if(!at_borrower)throw std::invalid_argument("world12: loan player is not at borrower");}
    if(managers.size()>4)throw std::invalid_argument("world12: too many managers");std::set<uint8_t> seats,portraits;std::set<ClubId> owned;
    for(const auto& manager:managers){if(manager.tactics)validate_manager_tactics(*manager.tactics);const auto it=clubs.find(manager.club);if(manager.seat>=4||manager.name.empty()||manager.name.size()>20||manager.portrait>=12||!seats.insert(manager.seat).second||!portraits.insert(manager.portrait).second||!owned.insert(manager.club).second||it==clubs.end()||!it->second.league.valid()||!catalogue.selectable(it->second.country)||catalogue.league(manager.start_league).country!=it->second.country)throw std::invalid_argument("world12: invalid manager selection");}
}
std::vector<ClubId> World::selectable_clubs(Country country,LeagueId id)const {if(!catalogue.selectable(country)||catalogue.league(id).country!=country)throw std::invalid_argument("world12: unavailable manager country/league");return membership.at(id.value).clubs;}
Country World::manager_country(uint8_t seat)const {for(const auto& manager:managers)if(manager.seat==seat)return clubs.at(manager.club).country;throw std::out_of_range("world12: manager seat is not occupied");}
Country World::national_coach_country(uint8_t seat)const {if(!catalogue.scope.international||managers.size()!=1||managers[0].seat!=seat)throw std::invalid_argument("world12: original single-manager national coach restriction");return manager_country(seat);}
}
