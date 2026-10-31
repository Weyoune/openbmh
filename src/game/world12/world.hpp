// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/catalogue.hpp"
#include "formats/m3d.hpp"
#include <map>
namespace bmh::world12 {
struct PlayerRecord {
    PlayerId id;Player attributes;uint8_t bonus=0;
    bool operator==(const PlayerRecord& p)const{return id==p.id&&attributes.name==p.attributes.name&&attributes.w35c==p.attributes.w35c&&attributes.w368==p.attributes.w368&&attributes.w364==p.attributes.w364&&bonus==p.bonus;}
};
enum class ClubKind:uint8_t { club,national_team };
struct ClubRecord {
    ClubId id;std::string name;Country country=Country::germany;ClubKind kind=ClubKind::club;
    LeagueId league;CrestId crest;uint8_t kit=0,rating=0,short_span=0;
    std::array<RosterSlot,24> roster;
    bool operator==(const ClubRecord&)const=default;
};
struct LeagueMembers {LeagueId league;std::vector<ClubId> clubs;bool operator==(const LeagueMembers&)const=default;};
struct Loan {
    PlayerId player;ClubId owner,borrower;uint8_t months=0;
    bool operator==(const Loan&)const=default;
};
struct Manager {
    uint8_t seat=0;std::string name;ClubId club;LeagueId start_league;uint8_t portrait=0;
    bool operator==(const Manager&)const=default;
};
struct CrestResource {std::string path;uint8_t cell=0;bool operator==(const CrestResource&)const=default;};
struct World {
    Catalogue catalogue=Catalogue::dual_classic();
    std::map<PlayerId,PlayerRecord> players;
    std::map<ClubId,ClubRecord> clubs;
    std::array<LeagueMembers,14> membership;
    std::vector<Loan> loans;
    std::vector<Manager> managers;
    // A crest is a distinct atlas identity, never combined with team flags.
    std::map<CrestId,CrestResource> crests;
    void validate(bool complete=true)const;
    std::vector<ClubId> selectable_clubs(Country country,LeagueId league)const;
    Country manager_country(uint8_t seat)const;
    Country national_coach_country(uint8_t seat)const;
    bool operator==(const World&)const=default;
};
// Filter an immutable imported master world before manager selection. Countries
// are removed as entities, never demoted into an AI/foreign fallback league.
World select_world_scope(const World& source,GameScope);
}
