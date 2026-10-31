// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/cup_draw.hpp"
#include "game/cup_draw_rules.hpp"
#include "game/cup.hpp"
#include "formats/le.hpp"
#include <set>
#include <stdexcept>

namespace bmh::world12 {
namespace {
void validate_ranking(const World& world,Country country,const CountryCupRanking& ranking) {
    for(uint8_t local=0;local<5;++local) {
        const auto league=world.catalogue.starting_league(country,local);
        const auto& expected=world.membership.at(league.value).clubs;
        const std::set<ClubId> actual(ranking[local].begin(),ranking[local].end());
        if(ranking[local].size()!=world.catalogue.league(league).clubs || actual.size()!=ranking[local].size() ||
            actual!=std::set<ClubId>(expected.begin(),expected.end()))
            throw std::invalid_argument("world12: cup ranking is not the complete country league membership");
        for(auto club:actual) {
            const auto& record=world.clubs.at(club);
            if(record.country!=country || record.league!=league || record.kind!=ClubKind::club)
                throw std::invalid_argument("world12: cup ranking contains a foreign club");
        }
    }
}
void shuffle(std::vector<ClubId>& clubs,GameRandom& game) {
    initial_cup_shuffle(int(clubs.size()),game,[&](int a,int b){std::swap(clubs[size_t(a)],clubs[size_t(b)]);});
}
std::vector<Fixture> consecutive_pairs(const std::vector<ClubId>& clubs) {
    if(clubs.empty() || clubs.size()%2)throw std::invalid_argument("world12: invalid initial cup field");
    std::vector<Fixture> fixtures;
    for(size_t i=0;i<clubs.size();i+=2)fixtures.push_back({clubs[i],clubs[i+1]});return fixtures;
}
void home_order(std::vector<Fixture>& fixtures,const World& world,Country country) {
    order_national_cup_pairs(fixtures,[&](ClubId id){
        const auto& club=world.clubs.at(id);
        if(club.country!=country)throw std::invalid_argument("world12: foreign club in domestic draw");
        const int local=club.league.valid()?world.catalogue.league(club.league).local_index:9;
        return CupDrawClass{local>=2,cup_league_weight(local)};
    });
}
}
std::vector<KnockoutStage> initial_domestic_cup_fields(const World& world,Country country,
    const CountryCupRanking& ranking,const LeImage& source,GameRandom& game) {
    validate_ranking(world,country,ranking);
    auto staged=game;std::vector<KnockoutStage> fields;
    if(country==Country::germany) {
        std::vector<ClubId> entrants;
        for(size_t l=0;l<4;++l)entrants.insert(entrants.end(),ranking[l].begin(),ranking[l].begin()+(l<2?18:14));
        shuffle(entrants,staged);auto fixtures=consecutive_pairs(entrants);home_order(fixtures,world,country);
        fields.push_back(KnockoutStage::from_source(DomesticCup::german_cup,source,0,fixtures,world));
    } else if(country==Country::england) {
        for(const auto cup:{DomesticCup::fa_cup,DomesticCup::league_cup}) {
            std::vector<ClubId> pool;
            const size_t last=cup==DomesticCup::fa_cup?3:2;
            for(size_t l=0;l<=last;++l) {
                const size_t count=l<last?20:cup==DomesticCup::fa_cup?8:4;
                pool.insert(pool.end(),ranking[l].begin(),ranking[l].begin()+count);
            }
            std::vector<ClubId> seeds(pool.begin(),pool.begin()+20),entrants(pool.begin()+20,pool.end());
            shuffle(seeds,staged);shuffle(entrants,staged);
            // The Original stores seeds before the qualifying list, and reads
            // that reserved prefix backwards when the stage is completed.
            std::reverse(seeds.begin(),seeds.end());
            auto fixtures=consecutive_pairs(entrants);home_order(fixtures,world,country);
            fields.push_back(KnockoutStage::from_source(cup,source,cup==DomesticCup::fa_cup?0:25,fixtures,world,seeds));
        }
    } else throw std::invalid_argument("world12: unsupported initial cup country");
    validate_cup_programmes(world,fields);game=staged;return fields;
}
CupLegResult draw_after_cup_leg(const World& world,const KnockoutStage& stage,
    const std::vector<uint16_t>& scores,const LeImage& source,WatcomRandom& random) {
    auto qualified=cup_leg_qualifiers(stage,scores,world);
    const auto verified=KnockoutStage::from_source(stage.cup,source,stage.source_entry,stage.rounds.front().fixtures,world,stage.seeded);
    if(verified.flags!=stage.flags || verified.planned_rounds!=stage.planned_rounds || verified.next_entry!=stage.next_entry)
        throw std::invalid_argument("world12: active cup rules disagree with their original source");
    CupLegResult result;result.cup=stage.cup;result.source_entry=stage.source_entry;
    result.revision=stage.revision;result.scores=scores;
    if(qualified.empty())return result;
    const bool stage_end=stage.rounds.size()==stage.planned_rounds;
    if(stage_end && !stage.next_entry)return result;
    if(qualified.size()%2)throw std::invalid_argument("world12: next knockout field has an odd entrant count");
    auto staged=random;
    const auto entry=stage_end?stage.next_entry:stage.source_entry;
    const auto at=bmh::cup_entry_address(entry);
    if(!source.contains(at+0x27))throw std::invalid_argument("world12: missing next cup stage rules");
    const auto flags=source.u8(at+0x14);
    if(!stage_end && flags!=stage.flags)throw std::invalid_argument("world12: active cup flags disagree with the source");
    if(flags&0x20)subsequent_cup_shuffle(int(qualified.size()),staged,[&](int a,int b){std::swap(qualified[size_t(a)],qualified[size_t(b)]);});
    std::vector<Fixture> fixtures;
    for(size_t i=0;i<qualified.size()/2;++i)fixtures.push_back({qualified[i],qualified[qualified.size()-1-i]});
    if(entry==0)home_order(fixtures,world,cup_country(stage.cup));
    if(stage_end)result.next_stage=KnockoutStage::from_source(stage.cup,source,entry,fixtures,world);
    else result.next_draw=std::move(fixtures);
    // Validate the generated receipt before publishing either the draw or RNG.
    auto preview=stage;commit_cup_leg(preview,result,world);random=staged;return result;
}
}
