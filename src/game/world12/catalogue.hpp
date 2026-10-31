// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/identity.hpp"
#include <array>
#include <string>
#include <vector>
namespace bmh::world12 {
struct League {
    LeagueId id;Country country=Country::germany;uint8_t local_index=0,clubs=0,relegation=0;
    std::string name;
    bool playable=false;
    int rounds()const{return 2*(int(clubs)-1);}
    bool operator==(const League&)const=default;
};
struct LadderGroup {std::vector<LeagueId> leagues;uint8_t relegation=0;bool operator==(const LadderGroup&)const=default;};
struct CountryRules {
    Country country;std::vector<LadderGroup> ladder;
    enum class Cup:uint8_t { german_cup,german_supercup,fa_cup,league_cup,english_supercup };
    std::vector<Cup> cups;
    bool operator==(const CountryRules&)const=default;
};
struct Catalogue {
    std::array<League,14> leagues;
    std::array<CountryRules,2> playable_countries;
    static Catalogue dual_classic();
    const League& league(LeagueId)const;
    const CountryRules& rules(Country)const;
    std::vector<LeagueId> table_leagues(TableGroup)const;
    LeagueId starting_league(Country,uint8_t local)const;
    bool selectable(Country)const;
    uint32_t league_clubs()const;
    void validate()const;
    bool operator==(const Catalogue&)const=default;
};
}
