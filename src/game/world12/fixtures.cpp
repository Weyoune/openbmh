// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/fixtures.hpp"
#include "game/schedule.hpp"
#include <numeric>
#include <stdexcept>
namespace bmh::world12 {
std::vector<Fixture> league_fixtures(const World& world,LeagueId id,int round){
    const auto& l=world.catalogue.league(id);const auto& clubs=world.membership.at(id.value).clubs;
    if(clubs.size()!=l.clubs||round<1||round>l.rounds())throw std::out_of_range("world12: fixture round or membership invalid");std::vector<Fixture> out;
    if(l.clubs!=10){for(const auto& fixture:bmh::generate_fixtures(l.clubs,round,l.clubs-1,0))out.push_back({clubs.at(size_t(fixture.home)),clubs.at(size_t(fixture.away))});return out;}
    std::vector<size_t> ring(10);std::iota(ring.begin(),ring.end(),size_t(0));const int first=(round-1)%9;
    for(int r=0;r<first;++r){auto last=ring.back();for(size_t j=9;j>1;--j)ring[j]=ring[j-1];ring[1]=last;}
    for(size_t i=0;i<5;++i){auto home=clubs[ring[i]],away=clubs[ring[9-i]];if((first%2==1&&i==0)^(round>9))std::swap(home,away);out.push_back({home,away});}return out;
}
SeasonResults SeasonResults::empty(const Catalogue& c){SeasonResults results;for(size_t i=0;i<14;++i)results.leagues[i].resize(size_t(c.leagues[i].rounds())*(c.leagues[i].clubs/2));return results;}
namespace {size_t offset(const Catalogue& c,LeagueId id,int round,int match){const auto&l=c.league(id);if(round<1||round>l.rounds()||match<0||match>=l.clubs/2)throw std::out_of_range("world12: result coordinates outside league");return size_t(round-1)*(l.clubs/2)+size_t(match);}}
LeagueResult& SeasonResults::result(const Catalogue& c,LeagueId id,int round,int match){return leagues.at(id.value).at(offset(c,id,round,match));}
const LeagueResult& SeasonResults::result(const Catalogue& c,LeagueId id,int round,int match)const{return leagues.at(id.value).at(offset(c,id,round,match));}
void SeasonResults::validate(const Catalogue& c)const{for(size_t i=0;i<14;++i){const auto&l=c.leagues[i];if(leagues[i].size()!=size_t(l.rounds())*(l.clubs/2))throw std::invalid_argument("world12: invalid result table dimensions");for(const auto&r:leagues[i])if(!r.played&&(r.home||r.away))throw std::invalid_argument("world12: unplayed score contains goals");}}
}
