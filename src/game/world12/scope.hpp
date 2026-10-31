// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/identity.hpp"
#include <stdexcept>
namespace bmh::world12 {
struct GameScope {
    bool germany=true,england=true,international=true;
    bool club_play()const{return germany||england;}
    bool domestic_enabled(Country country)const {
        return country==Country::germany?germany:country==Country::england?england:false;
    }
    bool clubs_enabled(Country country)const {
        return club_play() && (country==Country::germany?germany:true);
    }
    bool club_enabled(Country country,LeagueId league)const {
        return clubs_enabled(country) && (country!=Country::england || england || league==LeagueId{5});
    }
    void validate()const{if(!germany&&!england&&!international)throw std::invalid_argument("world12: at least one game scope is required");}
    bool operator==(const GameScope&)const=default;
};
}
