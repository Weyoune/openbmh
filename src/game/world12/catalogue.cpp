// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/catalogue.hpp"
#include <stdexcept>
#include <set>
namespace bmh::world12 {
Catalogue Catalogue::dual_classic(){
    Catalogue c;
    const char* names[]={"Bundesliga","2. Bundesliga","Regionalliga A","Regionalliga B","Oberliga","Premier League","Championship","League One","League Two","National League","Spanien","Frankreich","Italien","Portugal"};
    for(uint32_t i=0;i<14;++i){auto& l=c.leagues[i];l.id={i};l.name=names[i];l.country=i<5?Country::germany:i<10?Country::england:i==10?Country::spain:i==11?Country::france:i==12?Country::italy:Country::portugal;l.local_index=uint8_t(i<5?i:i<10?i-5:0);l.clubs=uint8_t(i<5||i>=10?18:i==9?10:20);l.playable=i<10;l.relegation=uint8_t(i==0||i==4?3:i==1?4:i==2||i==3?2:i<10?4:0);}
    using Cup=CountryRules::Cup;
    c.playable_countries[0]={Country::germany,{{{{0}},3},{{{1}},4},{{{2},{3}},4},{{{4}},3}},{Cup::german_cup,Cup::german_supercup}};
    c.playable_countries[1]={Country::england,{{{{5}},4},{{{6}},4},{{{7}},4},{{{8}},4},{{{9}},4}},{Cup::fa_cup,Cup::league_cup,Cup::english_supercup}};
    c.validate();return c;
}
const League& Catalogue::league(LeagueId id)const {if(!id.valid()||id.value>=leagues.size())throw std::out_of_range("world12: unknown league");return leagues[id.value];}
const CountryRules& Catalogue::rules(Country country)const {for(const auto& r:playable_countries)if(r.country==country)return r;throw std::invalid_argument("world12: country is AI-only");}
bool Catalogue::selectable(Country country)const{return country==Country::germany||country==Country::england;}
LeagueId Catalogue::starting_league(Country country,uint8_t local)const {if(!selectable(country))throw std::invalid_argument("world12: unsupported manager country");for(const auto& l:leagues)if(l.country==country&&l.local_index==local)return l.id;throw std::out_of_range("world12: invalid starting league");}
std::vector<LeagueId> Catalogue::table_leagues(TableGroup group)const {std::vector<LeagueId> ids;for(const auto& l:leagues)if(group==TableGroup::germany?l.country==Country::germany:group==TableGroup::england?l.country==Country::england:!l.playable)ids.push_back(l.id);return ids;}
uint32_t Catalogue::league_clubs()const{uint32_t n=0;for(const auto&l:leagues)n+=l.clubs;return n;}
void Catalogue::validate()const{
    if(league_clubs()!=252)throw std::invalid_argument("world12: inconsistent club total");
    const uint8_t sizes[]={18,18,18,18,18,20,20,20,20,10,18,18,18,18};
    for(size_t i=0;i<leagues.size();++i){const auto& l=leagues[i];if(l.id.value!=i||l.clubs!=sizes[i]||l.name.empty()||l.relegation>=l.clubs||l.playable!=selectable(l.country))throw std::invalid_argument("world12: invalid league descriptor");}
    std::set<Country> countries;
    for(const auto& r:playable_countries){if(!selectable(r.country)||!countries.insert(r.country).second)throw std::invalid_argument("world12: duplicate or unsupported country rules");std::set<LeagueId> seen;for(const auto& group:r.ladder){unsigned outgoing=0;for(auto id:group.leagues){const auto& l=league(id);if(l.country!=r.country||!seen.insert(id).second)throw std::invalid_argument("world12: promotion crosses a country or duplicates a league");outgoing+=l.relegation;}if(outgoing!=group.relegation)throw std::invalid_argument("world12: unbalanced relegation group");}if(seen.size()!=5||r.cups.empty())throw std::invalid_argument("world12: incomplete country rules");}
}
}
