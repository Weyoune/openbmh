// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/world.hpp"
namespace bmh::world12 {
struct Fixture {ClubId home,away;bool operator==(const Fixture&)const=default;};
// Original 18/20-team fixture tables remain supplied data. The ten-team league
// uses an explicit complete round-robin because FLTD never scheduled that group.
std::vector<Fixture> league_fixtures(const World&,LeagueId,int round);
struct LeagueResult {bool played=false;uint8_t home=0,away=0;bool operator==(const LeagueResult&)const=default;};
struct SeasonResults {
    std::array<std::vector<LeagueResult>,kLeagueSlots> leagues;
    static SeasonResults empty(const Catalogue&);
    LeagueResult& result(const Catalogue&,LeagueId,int round,int match);
    const LeagueResult& result(const Catalogue&,LeagueId,int round,int match)const;
    void validate(const Catalogue&)const;
    bool operator==(const SeasonResults&)const=default;
};
}
